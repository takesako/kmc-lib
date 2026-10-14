// fami_mml.h - C99 header-only MML-to-WAV synthesizer for NES-style chiptune music
// Copyright (c) 2026 Yoshinori Takesako
// SPDX-License-Identifier: MIT OR BSD-3-Clause OR GPL-1.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MML_RATE 44100u
#define MML_CPU_HZ ((float)21477272u / 12.0f)
#define MML_MAX_FRAMES (600u * MML_RATE)
#define MML_MAX_ITEMS 100000u
#define MML_MAX_STEPS 1000000u
#define MML_TICK 4096u
#define MML_CHANNELS 16

typedef uint64_t MMLTick;
typedef struct { uint32_t attack, decay, release; float sustain; } MMLEnvelope;
typedef struct { int depth, form, sub, cycles, target; MMLTick width, delay; uint32_t period_frames, delay_frames; } MMLLFO;
typedef struct { int detune, bend_from, bend_to, bend_ms; MMLLFO lfo; } MMLPitch;
typedef struct { MMLTick tick, gate; uint32_t start, off, origin; int period, velocity, expression, volume_mode, duty, noise_freq, wave, short_noise, legato, form; MMLEnvelope env; const char *at; MMLPitch mod; int continuous, glide_cents, glide_rate; MMLTick glide_length; uint32_t glide_frames; } MMLNote;
typedef struct { MMLTick tick; uint32_t frame; int value; } MMLExpression;
typedef struct { MMLNote *note; unsigned count, cap; MMLExpression *expr; unsigned expr_count, expr_cap; } MMLTrack;
typedef struct { MMLTick tick; int bpm100; uint32_t frame; uint64_t fraction; const char *at; } MMLTempo;
typedef struct { int octave, velocity, expression, volume_mode, velocity_step, shift, env_explicit, gate, gate_sub, form, duty, noise_freq, wave, slot, slur; MMLTick tick, length; MMLEnvelope env; MMLPitch mod; int raw_timer, continuous, last_pitch, port_from, port_pending, midi_on, midi_rate, midi_base; } MMLSettings;
typedef struct { const MMLNote *note; unsigned next, expr_next; int counter, step, volume, period, expression; unsigned lfsr, dmc_bit, dmc_level; float phase, noise_phase, noise_sample; } MMLVoice;
typedef struct { float pulse[31]; float previous, highpass, lowpass; } MMLMixer;
typedef struct { unsigned char *data; unsigned size, initial, loop; } MMLDPCM;

typedef struct { const char *text, *p, *at; MMLTrack track[MML_CHANNELS]; MMLTempo *tempo; unsigned tempo_count, tempo_cap; int used[MML_CHANNELS]; MMLDPCM dpcm[16]; MMLTick end; uint32_t frames; } MML;


typedef struct { const char *start, *branch; unsigned left; } MMLRepeat;

static inline int _isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int _toupper(int c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }

static inline int _note_index(int c) {
    if (c < 'A' || c > 'G') return -1;
    const char *s = "C D EF G A B";
    const char *p = strchr(s, c);
    return p ? (int)(p - s) : -1;
}

static inline float _exp2(float x) {
    int n = (int)x;
    if (x < (float)n) --n;
    x -= (float)n;
    float y = 1.0f + x * (0.69314718056f + x * (0.24022650696f 
        + x * (0.05550410866f + x * (0.00961812911f + x * (0.00133335581f 
        + x * (0.00015403530f + x * 0.00001525273f))))));
    while (n > 0) { y *= 2.0f; --n; }
    while (n < 0) { y *= 0.5f; ++n; }
    return y;
}

static inline int _round(float x) {
    return (int)(x + (x < 0.0f ? -0.5f : 0.5f));
}

static inline float _volume(int value, int mode) {
    if (!value) return 0.0f;
    return mode ? _exp2((float)(value - 127) * 0.06278f) : (float)value / 127.0f;
}

static inline int _lrint(float x) {
    int n = (int)x;
    float f = x - (float)n;
    if (f >  0.5f || (f ==  0.5f && n % 2)) ++n;
    if (f < -0.5f || (f == -0.5f && n % 2)) --n;
    return n;
}

static inline void _mml_error(MML *m, const char *message) {
    if (m->text) {
        unsigned line = 1, column = 1;
        for (const char *p = m->text; p < m->at; ++p) {
            if (*p == '\n') { ++line; column = 1; }
            else ++column;
        }
        fprintf(stderr, "MML %u:%u: %s (byte %u)\n", line, column, message, (unsigned)(m->at - m->text));

        const char *begin = m->at;
        while (begin > m->text && begin[-1] != '\n') --begin;
        const char *end = m->at;
        while (*end && *end != '\n') ++end;
        if (m->at - begin > 40) begin = m->at - 40;
        if (end - m->at > 40) end = m->at + 40;
        int clipped = begin > m->text && begin[-1] != '\n';
        fprintf(stderr, "  %s", clipped ? "..." : "");
        int offset = clipped ? 3 : 0;
        for (const char *p = begin; p < end; ++p) {
            if (*p == '\t') {
                fputs("    ", stderr);
                if (p < m->at) offset += 4;
            } else if (*p != '\r') {
                fputc(*p, stderr);
                if (p < m->at) ++offset;
            }
        }
        if (*end && *end != '\n') fputs("...", stderr);
        fputs("\n  ", stderr);
        for (int i = 0; i < offset; ++i) fputc(' ', stderr);
        fputs("^\n", stderr);
    } else fprintf(stderr, "FC MML: %s\n", message);
    exit(EXIT_FAILURE);
}

static inline void _mml_skip_space(MML *m) {
    for (;;) {
        while (*m->p == ' ' || *m->p == '\t' || *m->p == '\r' || *m->p == '\n') ++m->p;
        if (m->p[0] != '/' || m->p[1] != '*') return;
        const char *begin = m->p;
        m->p += 2;
        while (*m->p && !(m->p[0] == '*' && m->p[1] == '/')) {
            if (m->p[0] == '/' && m->p[1] == '*') {
                m->at = m->p;
                _mml_error(m, "Nested comment");
            }
            ++m->p;
        }
        if (!*m->p) { m->at = begin; _mml_error(m, "Unclosed comment"); }
        m->p += 2;
    }
}

static inline int _mml_read_int(MML *m, int low, int high) {
    int n = 0;
    if (!_isdigit(*m->p)) { m->at = m->p; _mml_error(m, "Expected number"); }
    while (_isdigit(*m->p)) {
        int d = *m->p++ - '0';
        if (d > high || n > (high - d) / 10) _mml_error(m, "Number out of range");
        n = n * 10 + d;
    }
    if (n < low) _mml_error(m, "Number out of range");
    return n;
}

static inline void _mml_comma(MML *m) {
    _mml_skip_space(m);
    if (*m->p != ',') { m->at = m->p; _mml_error(m, "Expected ','"); }
    ++m->p;
    _mml_skip_space(m);
}

static inline void *_mml_grow(MML *m, void *p, unsigned n, unsigned *cap, unsigned size) {
    if (n >= MML_MAX_ITEMS) _mml_error(m, "Too many events");
    if (n < *cap) return p;
    unsigned c = *cap ? *cap * 2 : 64;
    void *next = realloc(p, c * size);
    if (!next) _mml_error(m, "Out of memory");
    *cap = c;
    return next;
}

static inline MMLTick _mml_read_length(MML *m, MMLTick fallback) {
    MMLTick length = fallback;
    if (*m->p == '%') {
        ++m->p;
        length = (MMLTick)_mml_read_int(m, 1, 1000000) * MML_TICK;
    } else if (_isdigit(*m->p)) {
        int n = _mml_read_int(m, 1, 384);
        if (384 % n && n != 256) _mml_error(m, "Invalid note length (use %ticks)");
        length = (MMLTick)384 * MML_TICK / n;
    }
    MMLTick part = length;
    unsigned dots = 0;
    while (*m->p == '.') {
        ++m->p;
        if (++dots > 8) _mml_error(m, "Too many dots");
        part /= 2;
        length += part;
    }
    return length;
}

static inline MMLEnvelope _mml_read_envelope(MML *m) {
    MMLEnvelope e;
    if (_mml_read_int(m, 1, 2) != 1) _mml_error(m, "@E2 not supported");
    _mml_comma(m); e.attack  = (uint32_t)_mml_read_int(m, 0, 127 * 600) * MML_RATE / 127u;
    _mml_comma(m); e.decay   = (uint32_t)_mml_read_int(m, 0, 127 * 600) * MML_RATE / 127u;
    _mml_comma(m); e.sustain = (float)_mml_read_int(m, 0, 127) / 127.0f;
    _mml_comma(m); e.release = (uint32_t)_mml_read_int(m, 0, 127 * 600) * MML_RATE / 127u;
    if (!e.release) e.release = MML_RATE / 127u;
    _mml_skip_space(m);
    if (*m->p == ',') _mml_error(m, "Expected four ADSR values");
    return e;
}

static inline int _compare_tempo(const void *a, const void *b) {
    MMLTick x = ((const MMLTempo *)a)->tick, y = ((const MMLTempo *)b)->tick;
    return x < y ? -1 : x > y;
}

static inline void _mml_add_tempo(MML *m, MMLTick tick) {
    int bpm100 = _mml_read_int(m, 1, 999) * 100;
    if (*m->p == '.') {
        ++m->p;
        if (!_isdigit(*m->p)) _mml_error(m, "Expected tempo decimal");
        bpm100 += (*m->p++ - '0') * 10;
        if (_isdigit(*m->p)) bpm100 += *m->p++ - '0';
        if (_isdigit(*m->p)) _mml_error(m, "Too many tempo decimals");
    }
    if (bpm100 > 99900) _mml_error(m, "Tempo out of range");
    m->tempo = _mml_grow(m, m->tempo, m->tempo_count, &m->tempo_cap, sizeof(*m->tempo));
    m->tempo[m->tempo_count++] = (MMLTempo){tick, bpm100, 0, 0, m->at};
}

static inline int _mml_signed(MML *m, int limit) {
    int sign = 1;
    if (*m->p == '-' || *m->p == '+') {
        if (*m->p == '-') sign = -1;
        ++m->p;
    }
    return sign * _mml_read_int(m, 0, limit);
}

static inline int _base64(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static inline void _mml_wav9(MML *m) {
    if (_toupper(m->p[1]) != 'W' || _toupper(m->p[2]) != 'A' ||
        _toupper(m->p[3]) != 'V' || m->p[4] != '9') _mml_error(m, "Unsupported directive");
    m->p += 5;
    _mml_skip_space(m);
    int id = _mml_read_int(m, 0, 15);
    _mml_comma(m);
    int initial = _mml_read_int(m, 0, 127);
    _mml_comma(m);
    int loop = _mml_read_int(m, 0, 1);
    _mml_comma(m);
    MMLDPCM *s = &m->dpcm[id];
    if (s->data) _mml_error(m, "Duplicate #WAV9");
    s->data = malloc(4081);
    if (!s->data) _mml_error(m, "Out of memory");
    s->initial = (unsigned)initial;
    s->loop = (unsigned)loop;
    unsigned bits = 0, value = 0, chars = 0, padding = 0;
    while (*m->p && *m->p != '\n' && *m->p != '\r') {
        int c = *m->p++;
        if (c == ' ' || c == '\t') continue;
        if (c == '=') { ++padding; continue; }
        int n = _base64(c);
        if (n < 0 || padding) _mml_error(m, "Invalid #WAV9 Base64");
        value = (value << 6) | (unsigned)n;
        bits += 6;
        ++chars;
        if (bits >= 8) {
            bits -= 8;
            if (s->size == 4081) _mml_error(m, "DPCM sample too long");
            s->data[s->size++] = (unsigned char)(value >> bits);
            value &= (1u << bits) - 1;
        }
    }
    if (!chars || padding > 2 || (chars + padding) % 4 ||
        (padding && chars % 4 != 4 - padding) || value ||
        (s->size - 1) % 16) _mml_error(m, "Invalid #WAV9 data");
}

static inline void _mml_read_at(MML *m, MMLSettings *s) {
    int c = _toupper(*m->p);
    if (c >= 'A' && c <= 'Z') ++m->p;
    switch (c) {
        case 'V': s->velocity = _mml_read_int(m, 0, 127); s->velocity_step = 1; break;
        case 'X': {
            s->expression = _mml_read_int(m, 0, 127);
            if (s->slot >= 0) {
                MMLTrack *t = &m->track[s->slot];
                t->expr = _mml_grow(m, t->expr, t->expr_count, &t->expr_cap, sizeof(*t->expr));
                t->expr[t->expr_count++] = (MMLExpression){s->tick, 0, s->expression};
            }
            break;
        }
        case 'Q': s->gate_sub = _mml_read_int(m, 0, 1000000); break;
        case 'E': s->env = _mml_read_envelope(m); s->env_explicit = 1; break;
        case 'T':
            s->raw_timer = _mml_signed(m, 2047);
            if (s->raw_timer < -1) _mml_error(m, "@T out of range");
            break;
        case 'C': s->continuous = _mml_read_int(m, 0, 1); break;
        case 'D': s->mod.detune = _mml_signed(m, 1200); break;
        case 'B':
            s->mod.bend_from = _mml_signed(m, 2400);
            _mml_comma(m);
            s->mod.bend_to = _mml_signed(m, 2400);
            _mml_comma(m);
            s->mod.bend_ms = _mml_read_int(m, 0, 600000);
            break;
        case 'L': {
            MMLLFO *l = &s->mod.lfo;
            l->depth = _mml_read_int(m, 0, 2400);
            _mml_comma(m);
            l->width = (MMLTick)_mml_read_int(m, 1, 1000000) * MML_TICK;
            l->form = l->sub = l->cycles = l->target = 0;
            l->delay = 0;
            if (*m->p == ',') {
                _mml_comma(m);
                l->form = _mml_signed(m, 3);
                if (*m->p == '-') { ++m->p; l->sub = _mml_read_int(m, 0, 2); }
                if (*m->p == ',') { _mml_comma(m); l->delay = (MMLTick)_mml_read_int(m, 0, 1000000) * MML_TICK; }
                if (*m->p == ',') { _mml_comma(m); l->cycles = _mml_read_int(m, 0, 10000); }
                if (*m->p == ',') { _mml_comma(m); l->target = _mml_read_int(m, 0, 1); }
            }
            int wave = l->form < 0 ? -l->form : l->form;
            if (l->sub && !((wave == 0 && l->sub <= 2) || ((wave == 1 || wave == 2) && l->sub == 1)))
                _mml_error(m, "Unsupported LFO waveform");
            break;
        }
        case 'U': {
            int mode = _mml_read_int(m, 0, 3);
            if (mode <= 1) s->midi_on = mode;
            if (mode == 2) { _mml_comma(m); s->midi_rate = _mml_read_int(m, 0, 127); }
            if (mode == 3) {
                _mml_comma(m);
                int octave = s->octave;
                if (_toupper(*m->p) == 'O') { ++m->p; octave = _mml_read_int(m, 0, 8); }
                if (_note_index(_toupper(*m->p)) >= 0) {
                    s->midi_base = octave * 12 + _note_index(_toupper(*m->p++));
                    if (*m->p == '+' || *m->p == '#' || *m->p == '-') s->midi_base += *m->p++ == '-' ? -1 : 1;
                } else s->midi_base = _mml_read_int(m, 0, 127);
            }
            break;
        }
        case 'N':
            if (_toupper(*m->p) == 'S') {
                ++m->p;
                int shift = s->shift + _mml_signed(m, 127);
                if (shift < -127 || shift > 127) _mml_error(m, "Note shift out of range");
                s->shift = shift;
            } else s->noise_freq = _mml_read_int(m, 0, 127);
            break;
        case 'W': {
            int n = _mml_signed(m, 100);
            if (s->form == 3 && n >= -99 && n <= 99 && n != 0) s->duty = n;
            else if (s->form == 5 && (n == 1 || n == 2 || n == 4 || n == 6))
                s->duty = n == 1 ? 0 : n == 2 ? 1 : n == 4 ? 2 : 3;
            else _mml_error(m, "Invalid @W");
            break;
        }
        default: {
            if (!_isdigit(c)) _mml_error(m, "Unsupported @ command");
            int form = _mml_read_int(m, 0, 14);
            if (form > 9) _mml_error(m, "GB, wave memory and FM not supported");
            if (s->slot >= 0 && form != s->form &&
                !(form <= 4 && s->form <= 4) &&
                !((form == 7 || form == 8) && (s->form == 7 || s->form == 8)))
                _mml_error(m, "Channel type changed");
            if (form == 3 && s->form != 3) s->duty = 50;
            if (form == 5 && s->form != 5) s->duty = 2;
            s->form = form;
            s->wave = 0;
            if (*m->p == '-') {
                const char *offset = m->p++;
                s->wave = _mml_read_int(m, 0, 99);
                if (!((form == 0 && s->wave <= 2) ||
                    ((form == 1 || form == 2 || form == 6) && s->wave == 1) ||
                    (form == 9 && s->wave <= 15))) {
                    m->at = offset;
                    _mml_error(m, "Invalid waveform offset");
                }
            }
        }
    }
}

static inline void _mml_read_command(MML *m, MMLSettings *s, int c) {
    switch (c) {
        case 'T': _mml_add_tempo(m, s->tick); break;
        case 'O': s->octave = _mml_read_int(m, 0, 8); break;
        case 'N':
            if (_toupper(*m->p++) != 'S') _mml_error(m, "Invalid NS command");
            s->shift = _mml_signed(m, 127);
            break;
        case '<': case '>':
            s->octave += c == '<' ? 1 : -1;
            if (s->octave < 0 || s->octave > 8) _mml_error(m, "Octave out of range");
            break;
        case 'L':
            if (!_isdigit(*m->p) && *m->p != '%') _mml_error(m, "Expected length after L");
            s->length = _mml_read_length(m, s->length); break;
        case 'V': s->velocity = _mml_read_int(m, 0, 15) * 8 + 7; s->velocity_step = 8; break;
        case 'X': s->volume_mode = _mml_read_int(m, 0, 1); break;
        case '(': case ')':
            s->velocity += c == '(' ? s->velocity_step : -s->velocity_step;
            if (s->velocity > 127) s->velocity = 127;
            if (s->velocity < (s->velocity_step == 8 ? 7 : 0)) s->velocity = s->velocity_step == 8 ? 7 : 0;
            break;
        case 'Q': s->gate = _mml_read_int(m, 0, 16); break;
        case '@': _mml_read_at(m, s); break;
        default: _mml_error(m, "Invalid MML command");
    }
}

static inline int _mml_period(MML *m, int form, int pitch) {
    static const int noise[16] = {
        4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068
    };
    if (form == 9) {
        static const int dmc[16] = {428,380,340,320,286,254,226,214,190,160,142,128,106,85,72,54};
        if (pitch < 0 || pitch > 15) _mml_error(m, "DPCM pitch out of range");
        return dmc[pitch];
    }
    if (form == 4) return 1;
    if (form >= 7) {
        if (pitch < 0 || pitch > 15) _mml_error(m, "Noise pitch out of range");
        return noise[pitch];
    }
    if (pitch < 0 || pitch > 107) _mml_error(m, "Pitch out of range");
    float hz = 440.0f * _exp2((float)(pitch - 69) / 12.0f);
    if (form <= 4) return _round(MML_CPU_HZ / hz);
    int timer = _round(MML_CPU_HZ / ((form == 5 ? 16.0f : 32.0f) * hz)) - 1;
    if (timer < (form == 5 ? 8 : 2) || timer > 2047)
        _mml_error(m, "Pitch timer out of range");
    return (timer + 1) * (form == 5 ? 2 : 1);
}

static inline int _mml_channel(MML *m, MMLSettings *s) {
    if (s->slot >= 0) return s->slot;
    int ch = s->form == 5 ? 0 : s->form == 6 ? 2 : s->form == 9 ? 4 : s->form >= 7 ? 3 : 5;
    if (ch == 0 && m->used[0]) ch = 1;
    if (ch == 5) while (ch < MML_CHANNELS && m->used[ch]) ++ch;
    if (ch == MML_CHANNELS || m->used[ch]) _mml_error(m, "No free channel");
    m->used[ch] = 1;
    return s->slot = ch;
}

static inline void _mml_read_note(MML *m, MMLSettings *s, int c) {
    const char *begin = m->at;
    int rest = c == 'R', legato = s->slur;
    int pitch = s->octave * 12 + (rest ? 0 : _note_index(c)) + (rest ? 0 : s->shift);

    if (rest && legato) _mml_error(m, "Slur cannot end in rest");
    if (!rest) while (*m->p == '+' || *m->p == '#' || *m->p == '-') {
        pitch += *m->p++ == '-' ? -1 : 1;
        if (pitch < -128 || pitch > 255) _mml_error(m, "Pitch out of range");
    }
    if (*m->p == '*') {
        if (rest || s->port_pending) _mml_error(m, "Invalid portamento");
        s->port_from = pitch;
        s->port_pending = 1;
        ++m->p;
        return;
    }
    if (rest && s->port_pending) _mml_error(m, "Portamento needs a note");
    MMLTick length = _mml_read_length(m, s->length);
    s->slur = 0;
    _mml_skip_space(m);
    while (*m->p == '<' || *m->p == '>') {
        const char *next = m->p + 1;
        while (*next == ' ' || *next == '\t' || *next == '\n') ++next;
        if (*next != '&') break;
        _mml_read_command(m, s, *m->p++);
        _mml_skip_space(m);
    }
    while (*m->p == '&') {
        m->at = m->p++;
        _mml_skip_space(m);
        if (_isdigit(*m->p) || *m->p == '%') {
            length += _mml_read_length(m, s->length);
            if (length > (MMLTick)1000000 * MML_TICK) _mml_error(m, "Tie is too long");
            _mml_skip_space(m);
        } else {
            if (rest) _mml_error(m, "Rest cannot start slur");
            s->slur = 1;
            break;
        }
    }
    m->at = begin;
    if (s->tick + length > (MMLTick)600 * 999 * 96 * MML_TICK / 60)
        _mml_error(m, "Song too long");
    if (!rest) {
        int ch = _mml_channel(m, s);
        MMLTrack *t = &m->track[ch];
        MMLNote n = {0};
        MMLTick on = s->slur ? length : length * (unsigned)s->gate / 16u;
        MMLTick sub = s->slur ? 0 : (MMLTick)s->gate_sub * 2 * MML_TICK;
        on = on > sub ? on - sub : 0;
        n.tick = s->tick; n.gate = s->tick + on;
        n.period = s->raw_timer >= 0 && (s->form == 5 || s->form == 6) ?
            (s->raw_timer + 1) * (s->form == 5 ? 2 : 1) : _mml_period(m, s->form, pitch);
        n.continuous = s->continuous;
        n.form = s->form;
        if (s->raw_timer >= 0 && s->form != 5 && s->form != 6)
            _mml_error(m, "@T requires pulse or triangle");
        n.velocity = s->velocity; n.expression = s->expression; n.volume_mode = s->volume_mode;
        n.duty = s->duty;
        n.noise_freq = s->noise_freq; n.wave = s->wave;
        n.short_noise = s->form == 8; n.legato = legato;
        n.env = s->form == 6 && !s->env_explicit ? (MMLEnvelope){0,0,0,1.0f} : s->env;
        n.at = begin;
        n.mod = s->mod;
        int from = s->port_pending ? s->port_from : s->midi_base >= 0 ? s->midi_base : s->last_pitch;
        if (from >= 0 && (s->port_pending || s->midi_on)) {
            n.glide_cents = (from - pitch) * 100;
            n.glide_rate = s->port_pending ? -1 : s->midi_rate;
            n.glide_length = s->port_pending ? length : 0;
        }
        if ((s->form >= 7 || s->form == 4) && (n.mod.detune || n.mod.bend_from || n.mod.bend_to || (n.mod.lfo.target == 0 && n.mod.lfo.depth) || (s->form == 9 && n.mod.lfo.depth) || n.glide_cents))
            _mml_error(m, "Noise pitch modulation not supported");
        if (s->form < 7 && (n.mod.detune || n.mod.bend_from || n.mod.bend_to || n.mod.lfo.depth || n.glide_cents)) {
            int amplitude = n.mod.lfo.target == 0 ? n.mod.lfo.depth : 0;
            int glide = n.glide_cents;
            int lo = n.mod.detune + (n.mod.bend_from < n.mod.bend_to ? n.mod.bend_from : n.mod.bend_to) - amplitude + (glide < 0 ? glide : 0);
            int hi = n.mod.detune + (n.mod.bend_from > n.mod.bend_to ? n.mod.bend_from : n.mod.bend_to) + amplitude + (glide > 0 ? glide : 0);
            float divisor = s->form == 5 ? 2.0f : 1.0f;
            int slow = _round((float)n.period / divisor * _exp2(-(float)lo / 1200.0f));
            int fast = _round((float)n.period / divisor * _exp2(-(float)hi / 1200.0f));
            if ((s->form >= 5 && (slow > 2048 || fast < (s->form == 5 ? 9 : 3))) || fast <= 0)
                _mml_error(m, "Modulated pitch out of range");
        }
        t->note = _mml_grow(m, t->note, t->count, &t->cap, sizeof(n));
        t->note[t->count++] = n;
    }
    if (!rest) { s->last_pitch = pitch; s->midi_base = -1; }
    s->port_pending = 0;
    s->tick += length;
}

static inline void _mml_skip_repeat(MML *m) {
    int depth = 0;
    for (const char *p = m->p + 1; *p && *p != ';'; ++p) {
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) ++p;
            if (!*p) break;
            ++p;
        } else if (p[0] == '/' && p[1] == ':') { ++depth; ++p; }
        else if (p[0] == ':' && p[1] == '/') {
            if (!depth) { m->p = p + 2; return; }
            --depth;
            ++p;
        }
    }
    _mml_error(m, "Unclosed repeat");
}

static inline void _mml_parse(MML *m, const char *text) {
    int tracks = 0;
    unsigned steps = 0;
    m->text = m->p = m->at = text;
    for (;;) {
        MMLSettings s = {4,100,127,0,8,0,0,15,0,3,50,0,0,-1,0,0,96u*MML_TICK,{0,60u*MML_RATE/127u,MML_RATE/127u,30.0f/127.0f},{0},-1,0,-1,0,0,0,0,-1};
        _mml_skip_space(m);
        m->at = m->p;
        if (!*m->p) break;
        if (*m->p == '#') {
            if (++steps > MML_MAX_STEPS) _mml_error(m, "Too many commands");
            _mml_wav9(m);
            continue;
        }
        if (++tracks > MML_CHANNELS) _mml_error(m, "Too many tracks");
        MMLRepeat repeat[8] = {{0}};
        int depth = 0;
        for (;;) {
            _mml_skip_space(m);
            m->at = m->p;
            int c = _toupper(*m->p);
            if (!c || c == ';') break;
            if (++steps > MML_MAX_STEPS) _mml_error(m, "Too many commands");
            if (c == '#') { _mml_wav9(m); continue; }
            if (m->p[0] == '/' && m->p[1] == ':') {
                m->p += 2;
                int count = _isdigit(*m->p) ? _mml_read_int(m, 1, 10000) : 2;
                if (depth == 8) _mml_error(m, "Repeat too deep");
                repeat[depth++] = (MMLRepeat){m->p, NULL, (unsigned)count};
                continue;
            }
            if (m->p[0] == ':' && m->p[1] == '/') {
                if (!depth) _mml_error(m, "Unexpected repeat end");
                if (--repeat[depth - 1].left) m->p = repeat[depth - 1].start;
                else { --depth; m->p += 2; }
                continue;
            }
            if (c == '/') {
                if (!depth) _mml_error(m, "Unexpected repeat branch");
                MMLRepeat *r = &repeat[depth - 1];
                if (r->branch && r->branch != m->p) _mml_error(m, "Duplicate repeat branch");
                r->branch = m->p;
                if (r->left == 1) { _mml_skip_repeat(m); --depth; }
                else ++m->p;
                continue;
            }
            ++m->p;
            if (_note_index(c) >= 0 || c == 'R') _mml_read_note(m, &s, c);
            else _mml_read_command(m, &s, c);
        }
        if (depth) _mml_error(m, "Unclosed repeat");
        if (s.slur) _mml_error(m, "Unfinished slur");
        if (s.port_pending) _mml_error(m, "Unfinished portamento");
        if (s.tick > m->end) m->end = s.tick;
        if (*m->p == ';') ++m->p;
    }
    for (unsigned i = 0; i < m->track[4].count; ++i) {
        MMLNote *n = &m->track[4].note[i];
        if (!m->dpcm[n->wave].size) { m->at = n->at; _mml_error(m, "Missing #WAV9 sample"); }
    }
    if (!m->end) _mml_error(m, "Empty song");
}

static inline void _mml_advance(MML *m, MMLTick ticks, int bpm100, uint32_t *frame, uint64_t *fraction) {
    uint64_t d = (uint64_t)bpm100 * 96u * MML_TICK;
    uint64_t n = ticks * MML_RATE * 6000u;
    uint64_t whole = n / d;
    uint64_t part = ((n % d) << 24) / d + *fraction;
    whole += part >> 24;
    if (whole > MML_MAX_FRAMES || whole + *frame > MML_MAX_FRAMES) {
        m->text = NULL;
        _mml_error(m, "Song exceeds 600 seconds");
    }
    *frame += (uint32_t)whole;
    *fraction = part & 0xffffffu;
}

static inline void _mml_build_tempos(MML *m) {
    unsigned count = 0;
    MMLTick tick = 0;
    uint32_t frame = 0;
    uint64_t fraction = 0;
    int bpm100 = 12000;
    if (m->tempo_count > 1) qsort(m->tempo, m->tempo_count, sizeof(*m->tempo), _compare_tempo);
    for (unsigned i = 0; i < m->tempo_count; ++i) {
        MMLTempo t = m->tempo[i];
        if (count && t.tick == m->tempo[count - 1].tick) {
            if (t.bpm100 != m->tempo[count - 1].bpm100) {
                m->at = t.at;
                _mml_error(m, "Conflicting tempo");
            }
            continue;
        }
        m->tempo[count++] = t;
    }
    m->tempo_count = count;
    for (unsigned i = 0; i < count; ++i) {
        MMLTempo *t = &m->tempo[i];
        _mml_advance(m, t->tick - tick, bpm100, &frame, &fraction);
        t->frame = frame;
        t->fraction = fraction;
        tick = t->tick;
        bpm100 = t->bpm100;
    }
}

static inline uint32_t _mml_frame(MML *m, MMLTick tick) {
    unsigned lo = 0, hi = m->tempo_count;
    uint32_t frame = 0;
    uint64_t fraction = 0;
    MMLTick origin = 0;
    int bpm100 = 12000;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (m->tempo[mid].tick <= tick) lo = mid + 1; else hi = mid;
    }
    if (lo) {
        MMLTempo *t = &m->tempo[lo-1];
        frame = t->frame; fraction = t->fraction;
        origin = t->tick; bpm100 = t->bpm100;
    }
    _mml_advance(m, tick - origin, bpm100, &frame, &fraction);
    return frame + (fraction >= ((uint64_t)1 << 23));
}

static inline void _mml_schedule(MML *m) {
    _mml_build_tempos(m);
    m->frames = _mml_frame(m, m->end);
    for (int ch = 0; ch < MML_CHANNELS; ++ch) {
        MMLTrack *t = &m->track[ch];
        for (unsigned j = 0; j < t->expr_count; ++j) t->expr[j].frame = _mml_frame(m, t->expr[j].tick);
        unsigned i = 0;
        while (i < t->count) {
            unsigned last = i;
            while (last + 1 < t->count && t->note[last+1].legato) ++last;
            uint32_t origin = _mml_frame(m, t->note[i].tick);
            uint32_t off = _mml_frame(m, t->note[last].gate);
            MMLEnvelope env = t->note[i].env;
            int velocity = t->note[i].velocity;
            for (unsigned j = i; j <= last; ++j) {
                MMLNote *n = &t->note[j];
                n->start = _mml_frame(m, n->tick);
                n->origin = origin; n->off = off;
                if (n->glide_length) n->glide_frames = _mml_frame(m, n->tick + n->glide_length) - n->start;
                MMLLFO *l = &n->mod.lfo;
                if (l->depth) {
                    l->period_frames = _mml_frame(m, n->tick + l->width) - n->start;
                    l->delay_frames = _mml_frame(m, n->tick + l->delay) - n->start;
                }
                n->env = env; n->velocity = velocity;
            }
            uint32_t end = off + env.release;
            if (last + 1 < t->count) {
                uint32_t next = _mml_frame(m, t->note[last+1].tick);
                if (end > next) end = next;
            }
            if (end > m->frames) m->frames = end;
            i = last + 1;
        }
    }
    if (m->frames > MML_MAX_FRAMES) {
        m->text = NULL;
        _mml_error(m, "Song exceeds 600 seconds");
    }
    m->frames += MML_RATE / 10u;
}

static inline float _held(const MMLEnvelope *e, uint32_t age) {
    if (age < e->attack) return (float)age / (float)e->attack;
    age -= e->attack;
    if (age < e->decay) return 1.0f + (e->sustain - 1.0f) * (float)age / (float)e->decay;
    return e->sustain;
}

static inline float _level(const MMLNote *n, uint32_t frame) {
    if (frame < n->off) return _held(&n->env, frame - n->origin);
    uint32_t age = frame - n->off;
    if (age >= n->env.release) return 0.0f;
    float from = _held(&n->env, n->off - n->origin);
    return from * (1.0f - (float)age / (float)n->env.release);
}

static inline float _mml_lfo(const MMLNote *n, uint32_t frame) {
    const MMLLFO *l = &n->mod.lfo;
    if (!l->depth || !l->period_frames || frame < n->origin) return 0.0f;
    uint64_t age = (uint64_t)frame - n->origin;
    if (age < l->delay_frames) return 0.0f;
    age -= l->delay_frames;
    if (l->cycles && age >= (uint64_t)l->cycles * l->period_frames) return 0.0f;
    float p = (float)(age % l->period_frames) / (float)l->period_frames;
    int wave = l->form < 0 ? -l->form : l->form;
    if (l->sub && (wave == 1 || wave == 2)) {
        p += wave == 1 ? 0.5f : 0.25f;
        if (p >= 1.0f) p -= 1.0f;
    }
    float x = 0.0f;
    switch (wave) {
        case 0: {
            float t = p < 0.5f ? p * 2.0f : (p - 1.0f) * 2.0f;
            x = 4.0f * t * (1.0f - (t < 0 ? -t : t));
            x = 0.775f * x + 0.225f * x * (x < 0 ? -x : x);
            if (l->sub && x < 0.0f) x = l->sub == 1 ? 0.0f : -x;
            break;
        }
        case 1: x = 2.0f * p - 1.0f; break;
        case 2: x = p < 0.5f ? 1.0f - 4.0f * p : 4.0f * p - 3.0f; break;
        case 3: x = p < 0.5f ? 1.0f : -1.0f; break;
    }
    return x * (float)l->depth * (l->form < 0 ? -1.0f : 1.0f);
}

static inline void _update_voice(MML *m, MMLVoice *v, MMLTrack *t, int ch, uint32_t frame) {
    while (v->next < t->count && t->note[v->next].start <= frame) {
        const MMLNote *n = &t->note[v->next++];
        v->note = n;
        v->expression = n->expression;
        v->period = 0;
        if (ch < 2 && !n->legato && !n->continuous) v->step = 0;
        if (ch == 2 && n->wave == 1 && !n->legato) v->step = 15;
        if (ch >= 5 && !n->legato && !n->continuous) v->phase = 0.0f;
        if (ch >= 5 && !n->legato) v->noise_phase = 0.0f;
        if (ch == 4) {
            v->dmc_bit = 0;
            v->dmc_level = m->dpcm[n->wave].initial;
            v->counter = n->period;
        }
    }
    if (ch == 4 && v->note && frame >= v->note->off && m->dpcm[v->note->wave].loop)
        v->dmc_bit = (unsigned)-1;
    while (v->expr_next < t->expr_count && t->expr[v->expr_next].frame <= frame)
        v->expression = t->expr[v->expr_next++].value;
    if (v->note && (!v->period || frame % 48u == 0)) {
        const MMLNote *n = v->note;
        float age = (float)(frame - n->start) * (1000.0f / (float)MML_RATE);
        float ratio = n->mod.bend_ms ? age / (float)n->mod.bend_ms : 1.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        float cents = (float)n->mod.detune + (float)n->mod.bend_from + (float)(n->mod.bend_to - n->mod.bend_from) * ratio;
        if (n->glide_cents) {
            float slide = (float)n->glide_cents;
            if (n->glide_rate < 0) slide *= n->glide_frames && frame - n->start < n->glide_frames ?
                1.0f - (float)(frame - n->start) / (float)n->glide_frames : 0.0f;
            else {
                float rate = n->glide_rate ? (8.0f - (float)n->glide_rate * 7.99f / 128.0f) / n->glide_rate : 100000.0f;
                float delta = rate * (float)(frame - n->start);
                slide = slide > 0.0f ? (slide > delta ? slide - delta : 0.0f) : (slide < -delta ? slide + delta : 0.0f);
            }
            cents += slide;
        }
        if (n->mod.lfo.target == 0) cents += _mml_lfo(n, frame);
        int factor = ch < 2 ? 2 : 1;
        v->period = (ch == 3 || ch == 4) || cents == 0.0f ? n->period :
            factor * _round((float)(n->period / factor) * _exp2(-cents / 1200.0f));
    }
    if (v->note && ch < 5) {
        const MMLNote *n = v->note;
        float level = _level(n, frame);
        float volume = (float)n->velocity * 15.0f / 127.0f * level;
        if (n->volume_mode) volume = 15.0f * _volume(n->velocity, 1) *
            _volume(_round(level * 127.0f), 1);
        if (v->expression != 127) volume *= _volume(v->expression, n->volume_mode);
        if (n->mod.lfo.target == 1) volume *= 1.0f + _mml_lfo(n, frame) / 127.0f;
        if (volume < 0.0f) volume = 0.0f;
        v->volume = _round(volume);
    }
}

static inline int _mml_clock(MML *m, MMLVoice *v, int ch) {
    static const unsigned duty[4] = {0x02,0x06,0x1e,0xf9};
    const MMLNote *n = v->note;
    if (!n) return 0;
    if (ch == 4) {
        const MMLDPCM *s = &m->dpcm[n->wave];
        if (v->dmc_bit == s->size * 8 && s->loop) v->dmc_bit = 0;
        if (v->dmc_bit < s->size * 8 && --v->counter <= 0) {
            unsigned bit = (s->data[v->dmc_bit / 8] >> (v->dmc_bit % 8)) & 1u;
            v->counter = n->period;
            if (bit && v->dmc_level <= 125) v->dmc_level += 2;
            if (!bit && v->dmc_level >= 2) v->dmc_level -= 2;
            ++v->dmc_bit;
        }
        return (int)v->dmc_level;
    }
    if (--v->counter <= 0) {
        v->counter = v->period;
        if (ch == 3) {
            unsigned bit = (v->lfsr ^ (v->lfsr >> (n->short_noise ? 6 : 1))) & 1u;
            v->lfsr = (v->lfsr >> 1) | (bit << 14);
        } else if (ch < 2 || v->volume) v->step = (v->step + 1) & (ch == 2 ? 31 : 7);
    }
    if (ch == 2) return v->step < 16 ? 15 - v->step : v->step - 16;
    if (ch == 3) return (v->lfsr & 1u) ? 0 : v->volume;
    return ((duty[n->duty] >> v->step) & 1u) ? v->volume : 0;
}

static inline float _mml_soft(MMLVoice *v, uint32_t frame) {
    const MMLNote *n = v->note;
    if (!n || !v->period) return 0.0f;
    float p = v->phase;
    if (n->form == 1 && n->wave == 1) {
        p += 0.5f;
        if (p >= 1.0f) p -= 1.0f;
    }
    if (n->form != 4) {
        v->phase += MML_CPU_HZ / ((float)v->period * (float)MML_RATE);
        if (v->phase >= 1.0f) v->phase -= (int)v->phase;
    }
    float x = 0.0f;
    switch (n->form) {
        case 0: {
            float t = p < 0.5f ? p * 2.0f : (p - 1.0f) * 2.0f;
            float a = t < 0.0f ? -t : t;
            x = 4.0f * t * (1.0f - a);
            x = 0.775f * x + 0.225f * x * (x < 0.0f ? -x : x);
            if (n->wave == 1 && x < 0.0f) x = 0.0f;
            if (n->wave == 2 && x < 0.0f) x = -x;
            break;
        }
        case 1: x = 2.0f * p - 1.0f; break;
        case 2: x = n->wave ? (p < 0.25f ? -4.0f*p : p < 0.75f ? 4.0f*p-2.0f : 4.0f-4.0f*p)
                              : (p < 0.5f ? 1.0f-4.0f*p : 4.0f*p-3.0f); break;
        case 3: x = p * 100.0f < (float)(n->duty < 0 ? -n->duty : n->duty) ? 1.0f : -1.0f; break;
    }
    if (n->form == 4 || (n->form == 3 && n->duty < 0)) {
        if (v->noise_phase <= 0.0f) {
            v->lfsr ^= v->lfsr << 13;
            v->lfsr ^= v->lfsr >> 17;
            v->lfsr ^= v->lfsr << 5;
            v->noise_sample = (float)(v->lfsr & 65535u) / 32767.5f - 1.0f;
            v->noise_phase += 1.0f;
        }
        v->noise_phase -= (float)(128 - n->noise_freq) / 128.0f;
        if (n->form == 4 || x < 0.0f) x = v->noise_sample;
    }
    float volume = (float)n->velocity / 127.0f * _level(n, frame);
    if (n->volume_mode) volume = _volume(n->velocity, 1) * _volume(_round(_level(n, frame) * 127.0f), 1);
    if (v->expression != 127) volume *= _volume(v->expression, n->volume_mode);
    if (n->mod.lfo.target == 1) volume *= 1.0f + _mml_lfo(n, frame) / 127.0f;
    return x * (volume < 0.0f ? 0.0f : volume);
}

static inline void _make_mixer(MMLMixer *x) {
    for (int i = 1; i < 31; ++i) x->pulse[i] = 95.88f / (8128.0f / (float)i + 100.0f);
}

static inline int16_t _filter(MMLMixer *x, float sample) {
    x->highpass = 0x1.fea94ep-1f * (x->highpass + sample - x->previous);
    x->previous = sample;
    x->lowpass += 0x1.ae14c8p-1f * (x->highpass - x->lowpass);
    sample = x->lowpass * 30000.0f;
    if (sample < -32768.0f) sample = -32768.0f;
    if (sample > 32767.0f) sample = 32767.0f;
    return (int16_t)_lrint(sample);
}

static inline void _write_le(FILE *f, uint32_t n, int bytes) {
    while (bytes--) { fputc((int)(n & 255u), f); n >>= 8; }
}

static inline void _mml_write_header(MML *m, FILE *f) {
    fwrite("RIFF",1,4,f); _write_le(f,36u+m->frames*2u,4); fwrite("WAVEfmt ",1,8,f);
    _write_le(f,16,4); _write_le(f,1,2); _write_le(f,1,2); _write_le(f,MML_RATE,4);
    _write_le(f,MML_RATE*2u,4); _write_le(f,2,2); _write_le(f,16,2);
    fwrite("data",1,4,f); _write_le(f,m->frames*2u,4);
}

static inline int16_t _pcm(float x) {
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    return (int16_t)_lrint(x * 32767.0f);
}

static inline void _mml_render(MML *m, const char *path) {
    FILE *f = fopen(path, "wb");
    MMLMixer mixer = {0};
    MMLVoice voice[MML_CHANNELS] = {{0}};
    uint64_t fraction = 0;
    int fc_used = m->used[0] || m->used[1] || m->used[2] || m->used[3] || m->used[4];
    if (!f) { m->text = NULL; _mml_error(m, "Cannot open WAV"); }
    _make_mixer(&mixer);
    for (int ch = 0; ch < MML_CHANNELS; ++ch) voice[ch].lfsr = 1u + (unsigned)ch;
    voice[2].step = 15;
    _mml_write_header(m, f);
    for (uint32_t frame = 0; frame < m->frames; ++frame) {
        float sum = 0.0f;
        for (int ch = 0; ch < MML_CHANNELS; ++ch) _update_voice(m, &voice[ch], &m->track[ch], ch, frame);
        if (fc_used) {
            fraction += 21477272u;
            unsigned cycles = (unsigned)(fraction / (12u * MML_RATE));
            fraction %= 12u * MML_RATE;
            float tri_gain = (float)voice[2].volume / 15.0f;
            for (unsigned i = 0; i < cycles; ++i) {
                int a = _mml_clock(m,&voice[0],0), b = _mml_clock(m,&voice[1],1);
                int t = _mml_clock(m,&voice[2],2), n = _mml_clock(m,&voice[3],3);
                int d = _mml_clock(m,&voice[4],4);
                float level = (float)t * tri_gain / 8227.0f + (float)n / 12241.0f + (float)d / 22638.0f;
                sum += mixer.pulse[a+b] + (level ? 159.79f / (1.0f / level + 100.0f) : 0.0f);
            }
            sum /= (float)cycles;
        }
        for (int ch = 5; ch < MML_CHANNELS; ++ch) sum += 0.25f * _mml_soft(&voice[ch], frame);
        _write_le(f, (uint16_t)(fc_used ? _filter(&mixer, sum) : _pcm(sum)), 2);
    }
    int error = ferror(f);
    if (fclose(f)) error = 1;
    if (error) { m->text = NULL; _mml_error(m, "Cannot write WAV"); }
}

static inline void _mml_free(MML *m) {
    for (int ch = 0; ch < MML_CHANNELS; ++ch) {
        free(m->track[ch].note);
        free(m->track[ch].expr);
    }
    free(m->tempo);
    for (int i = 0; i < 16; ++i) free(m->dpcm[i].data);
}

static inline void mml_text2wav(const char *wav_file, const char *text) {
    MML m = {0};
    if (!wav_file || !text) _mml_error(&m, "Null argument");
    _mml_parse(&m, text);
    _mml_schedule(&m);
    _mml_render(&m, wav_file);
    _mml_free(&m);
}

static inline void mml_file2wav(const char *wav_file, const char *mml_file) {
    MML m = {0};
    if (!wav_file || !mml_file) _mml_error(&m, "Null argument");
    FILE *f = fopen(mml_file, "rb");
    if (!f) _mml_error(&m, "Cannot open MML file");
    if (fseek(f, 0, SEEK_END)) _mml_error(&m, "Cannot seek MML file");
    long bytes = ftell(f);
    if (bytes < 0) _mml_error(&m, "Cannot get file size");
    if (bytes > 16 * 1024 * 1024) _mml_error(&m, "MML file exceeds 16 MB");
    rewind(f);
    unsigned length = (unsigned)bytes;
    unsigned char *text = malloc(length + 1u);
    if (!text) _mml_error(&m, "Out of memory");
    unsigned got = (unsigned)fread(text, 1, length, f);
    int bad = got != length || ferror(f);
    if (fclose(f)) bad = 1;
    if (bad) _mml_error(&m, "Cannot read MML file");
    if (memchr(text, '\0', got)) _mml_error(&m, "NUL in MML file");
    text[got] = '\0';
    unsigned bom = got >= 3 && text[0] == 0xef && text[1] == 0xbb && text[2] == 0xbf ? 3 : 0;
    mml_text2wav(wav_file, (const char *)(text + bom));
    free(text);
}
