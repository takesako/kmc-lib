// lib_macio.h - macOS keyboard events and polyphonic sound, C99 header only.
// Copyright (c) 2026 Yoshinori Takesako
// SPDX-License-Identifier: MIT OR BSD-3-Clause OR GPL-1.0-or-later
// Keys use macOS virtual keycodes (kVK_ANSI_*, kVK_JIS_*, kVK_Escape).
// Requires Input Monitoring permission for Terminal.app or VS Code.
#ifndef LIB_MACIO_H
#define LIB_MACIO_H
#ifndef __APPLE__
#error lib_macio.h requires macOS
#endif

#include <ApplicationServices/ApplicationServices.h>
#include <AudioToolbox/AudioToolbox.h>
#include <Carbon/Carbon.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define TONE_TRIANGLE 0
#define TONE_SQUARE   1
#define TONE_SAW      2
#define TONE_PULSE    3
#define _MACIO_N 128

static int _macio_notes[_MACIO_N];
static unsigned char _macio_held[_MACIO_N], _macio_down[_MACIO_N], _macio_up[_MACIO_N];
static CFMachPortRef _macio_tap;
static CFRunLoopSourceRef _macio_source;
static AudioUnit _macio_audio;
static struct termios _macio_term;
static int _macio_tty, _macio_changed;
static volatile sig_atomic_t _macio_running;

static void _macio_signal(int sig) {
    (void)sig;
    _macio_running = 0;
}

// Pitch in hundredths of Hz and waveform in the bottom two bits.
static inline void macio_note_on(int key, float hz, int wave) {
    if (key < 0 || key >= _MACIO_N) return;
    if (hz < 0) hz = 0;
    if (hz > 10000) hz = 10000;
    if (wave < TONE_TRIANGLE || wave > TONE_PULSE) wave = TONE_TRIANGLE;
    int pitch = (int)(hz * 100 + 0.5f);
    __atomic_store_n(&_macio_notes[key], pitch ? (pitch << 2) | wave : 0,
                     __ATOMIC_RELAXED);
}

static inline void macio_note_off(int key) {
    if (key >= 0 && key < _MACIO_N)
        __atomic_store_n(&_macio_notes[key], 0, __ATOMIC_RELAXED);
}

static inline int macio_keydown(int key) {
    return key >= 0 && key < _MACIO_N && _macio_down[key];
}
static inline int macio_keyup(int key) {
    return key >= 0 && key < _MACIO_N && _macio_up[key];
}
static inline int macio_pressed(int key) {
    return key >= 0 && key < _MACIO_N && _macio_held[key];
}

static CGEventRef _macio_keyevent(CGEventTapProxy proxy, CGEventType type,
                                  CGEventRef event, void *data) {
    (void)proxy; (void)data;
    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
        memset(_macio_held, 0, sizeof(_macio_held));
        memset(_macio_down, 0, sizeof(_macio_down));
        memset(_macio_up, 0, sizeof(_macio_up));
        for (int i = 0; i < _MACIO_N; i++) macio_note_off(i);
        _macio_changed = 1;
        if (_macio_tap) CGEventTapEnable(_macio_tap, true);
        return event;
    }
    if (type != kCGEventKeyDown && type != kCGEventKeyUp) return event;
    int key = (int)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    if (key < 0 || key >= _MACIO_N) return event;
    if (type == kCGEventKeyDown) {
        if (CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat))
            return event;
        if (!_macio_held[key]) {
            _macio_held[key] = _macio_down[key] = 1;
            _macio_changed = 1;
        }
    } else if (_macio_held[key]) {
        _macio_held[key] = 0;
        _macio_up[key] = _macio_changed = 1;
    }
    return event;
}

typedef struct {
    float phase, step, volume, target;
    int wave;
} _macio_voice;

static OSStatus _macio_sound(void *data, AudioUnitRenderActionFlags *flags,
                             const AudioTimeStamp *time, UInt32 bus,
                             UInt32 frames, AudioBufferList *out) {
    (void)data; (void)flags; (void)time; (void)bus;
    static _macio_voice voice[_MACIO_N];
    int playing[_MACIO_N], count = 0;
    for (int i = 0; i < _MACIO_N; i++) {
        int note = __atomic_load_n(&_macio_notes[i], __ATOMIC_RELAXED);
        _macio_voice *v = &voice[i];
        v->target = note ? 1 : 0;
        if (note) {
            v->step = (note >> 2) / 4410000.0f;
            v->wave = note & 3;
        }
        if (note || v->volume > 0.0001f) playing[count++] = i;
    }
    float gain = count ? 0.25f / (1 + 0.32f * (count - 1)) : 0;
    float *p = (float *)out->mBuffers[0].mData;
    for (UInt32 i = 0; i < frames; i++) {
        float sample = 0;
        for (int j = 0; j < count; j++) {
            _macio_voice *v = &voice[playing[j]];
            v->volume += (v->target - v->volume) * 0.008f;
            v->phase += v->step;
            if (v->phase >= 1) v->phase -= 1;
            float w;
            if (v->wave == TONE_SQUARE)
                w = v->phase < 0.5f ? 1 : -1;
            else if (v->wave == TONE_SAW)
                w = 2 * v->phase - 1;
            else if (v->wave == TONE_PULSE)
                w = v->phase < 0.25f ? 1 : -1;
            else
                w = 1 - 4 * (v->phase < 0.5f ? v->phase : 1 - v->phase);
            sample += v->volume * w;
        }
        sample *= gain;
        p[2 * i] = p[2 * i + 1] = sample;
    }
    return noErr;
}

// Wait for the next key press or release. Returns 0 after Ctrl+C.
static inline int macio_wait(void) {
    memset(_macio_down, 0, sizeof(_macio_down));
    memset(_macio_up, 0, sizeof(_macio_up));
    _macio_changed = 0;
    while (_macio_running && !_macio_changed)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    return _macio_running != 0;
}

static inline void macio_close(void) {
    for (int i = 0; i < _MACIO_N; i++) macio_note_off(i);
    if (_macio_audio) {
        AudioOutputUnitStop(_macio_audio);
        AudioUnitUninitialize(_macio_audio);
        AudioComponentInstanceDispose(_macio_audio);
        _macio_audio = NULL;
    }
    if (_macio_source) {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), _macio_source,
                              kCFRunLoopCommonModes);
        CFRelease(_macio_source);
        _macio_source = NULL;
    }
    if (_macio_tap) {
        CFMachPortInvalidate(_macio_tap);
        CFRelease(_macio_tap);
        _macio_tap = NULL;
    }
    if (_macio_tty) {
        tcflush(STDIN_FILENO, TCIFLUSH);
        tcsetattr(STDIN_FILENO, TCSANOW, &_macio_term);
        _macio_tty = 0;
    }
}

static void _macio_fail(const char *message) {
    macio_close();
    fprintf(stderr, "macio_init: %s\n", message);
    exit(EXIT_FAILURE);
}

static inline void macio_init(void) {
    _macio_running = 1;
    _macio_changed = 0;
    memset(_macio_held, 0, sizeof(_macio_held));
    memset(_macio_down, 0, sizeof(_macio_down));
    memset(_macio_up, 0, sizeof(_macio_up));
    memset(_macio_notes, 0, sizeof(_macio_notes));
    if (!CGPreflightListenEventAccess()) CGRequestListenEventAccess();
    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) |
                       CGEventMaskBit(kCGEventKeyUp);
    _macio_tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                                  kCGEventTapOptionListenOnly, mask,
                                  _macio_keyevent, NULL);
    if (!_macio_tap)
        _macio_fail("enable Input Monitoring for Terminal or VS Code.");
    _macio_source = CFMachPortCreateRunLoopSource(NULL, _macio_tap, 0);
    if (!_macio_source) _macio_fail("cannot create event source.");
    CFRunLoopAddSource(CFRunLoopGetCurrent(), _macio_source,
                       kCFRunLoopCommonModes);
    CGEventTapEnable(_macio_tap, true);

    AudioComponentDescription desc = {
        kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
        kAudioUnitManufacturer_Apple, 0, 0
    };
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    if (!component || AudioComponentInstanceNew(component, &_macio_audio))
        _macio_fail("cannot open audio output.");
    AudioStreamBasicDescription format = {0};
    format.mSampleRate = 44100;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = format.mBytesPerPacket = 8;
    AURenderCallbackStruct callback = {_macio_sound, NULL};
    if (AudioUnitSetProperty(_macio_audio, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &format, sizeof(format)) ||
        AudioUnitSetProperty(_macio_audio, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &callback, sizeof(callback)) ||
        AudioUnitInitialize(_macio_audio) || AudioOutputUnitStart(_macio_audio)) {
        _macio_fail("cannot start audio output.");
    }
    _macio_tty = tcgetattr(STDIN_FILENO, &_macio_term) == 0;
    if (_macio_tty) {
        struct termios t = _macio_term;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN] = t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    signal(SIGINT, _macio_signal);
    signal(SIGTERM, _macio_signal);
}
#endif // LIB_MACIO_H
