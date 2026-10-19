# lib_macio.h — macOSのキー入力・音声出力ライブラリ

`lib_macio.h` は、macOSでキーが押されているかどうかをリアルタイムで調べたり、音を鳴らしたりするためのC言語ライブラリです。ヘッダファイルを1つ読み込むだけで使えます。複数の音を同時に鳴らす「和音」にも対応しています。

- 対応環境：macOS（Terminal.app、Visual Studio Codeのターミナルなど）
- 言語：C言語（C99）
- キー入力：macOSの仮想キーコード（`kVK_ANSI_A`、`kVK_Escape` など）を使用

## 1. コンパイルと実行

Cのプログラムと `lib_macio.h` を同じフォルダに置きます。

```c
#include "lib_macio.h"

int main(void) {
               // ド レ ミ ファ ソ ラ  シ  ド
               //  Z  X  C  V   B   N   M   ,
    int keys[] = { 6, 7, 8, 9, 11, 45, 46, 43};
    float hz[] = {261.63, 293.66, 329.63, 349.23,
                  392.00, 440.00, 493.88, 523.25};
    macio_init();
    printf("Piano: Z X C V B N M , (Esc to quit)\n");
    while (macio_wait()) {
        if (macio_keydown(kVK_Escape)) break;
        for (int i = 0; i < 8; i++) {
            if (macio_keydown(keys[i])) {
                macio_note_on(i, hz[i], TONE_TRIANGLE);
            }
            if (macio_keyup(keys[i])) {
                macio_note_off(i);
            }
        }
    }
    macio_close();
    return 0;
}
```

### コンパイル方法

`lib_macio.h` は、macOSのキーボード入力と音声出力の機能を利用しています。そのため、コンパイル時には 4つのフレームワークを指定する必要があります。

```
cc piano.c -o piano -framework ApplicationServices -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
```

| フレームワーク               | 役割                          |
| --------------------- | --------------------------- |
| `ApplicationServices` | キーボードの押下・解放イベントを取得する        |
| `AudioToolbox`        | 音声出力に必要なオーディオ機能を利用する        |
| `AudioUnit`           | 波形を生成してスピーカーから音を鳴らす         |
| `CoreFoundation`      | キーボードイベントを待ち受け、処理する仕組みを利用する |

`-framework` は、macOSが提供するフレームワークをプログラムにリンクするためのオプションです。

### 実行方法

コンパイル後は、次のコマンドで普通に実行できます。

```
./piano
```

> **初回実行時の注意：** キーイベントの取得には、macOSの「システム設定 → プライバシーとセキュリティ → 入力監視」で、使用するターミナルアプリ（TerminalやVS Code）への許可が必要になる場合があります。許可後、該当アプリを再起動してください。キーイベントは、ターミナル以外のアプリで押したキーも検出する方式となっています。

## 2. 公開関数の一覧

| 関数 | 役割 |
|---|---|
| `macio_init()` | キー入力と音声出力を使えるようにする |
| `macio_close()` | 音声出力を停止し、後片付けする |
| `macio_note_on(key, hz, wave)` | 指定した高さ・音色で音を鳴らし始める |
| `macio_note_off(key)` | 指定した音を止める |
| `macio_wait()` | 次のキー入力イベントまで待つ |
| `macio_keydown(key)` | キーが押されたか調べる |
| `macio_keyup(key)` | キーが離されたか調べる |
| `macio_pressed(key)` | キーが現在押されているか調べる |

### `macio_init()` — 初期化

```c
macio_init();
```

最初に1回呼び出します。キー入力の監視と音声出力を開始します。初期化に失敗した場合は、エラーメッセージを表示してプログラムを終了します。

### `macio_close()` — 終了処理

```c
macio_close();
```

最後に呼び出します。鳴っている音を止め、キー入力の監視と音声出力を終了します。

### `macio_note_on(key, hz, wave)` — 音を鳴らす

```c
macio_note_on(0, 440, TONE_TRIANGLE);
```

| 引数 | 意味 | 例 |
|---|---|---|
| `key` | 音を区別する番号（0～127） | `0` |
| `hz` | 音の高さ（周波数、Hz） | `440` = ラ |
| `wave` | 音色（波形） | `TONE_TRIANGLE` |

`key` は音を区別する番号です。同じ番号に別の音を指定すると、その番号の音が切り替わります。別々の番号を使えば複数の音を同時に鳴らせます。番号の割り振りは自分で決めて問題ありません。単一の音だけ鳴らしたい場合は `0` を指定してください。

周波数は0～10000Hzの範囲に収めて扱われます。`hz` を0にすると、その番号の音は鳴らなくなります。

#### 選べる音色

| 定数 | 波形 | 音の特徴 |
|---|---|---|
| `TONE_TRIANGLE` | 三角波 | 柔らかい音 |
| `TONE_SQUARE` | 矩形波 | 電子ゲーム風の音 |
| `TONE_SAW` | ノコギリ波 | 明るく鋭い音 |
| `TONE_PULSE` | パルス波（25%） | 細く特徴的な音 |

### `macio_note_off(key)` — 音を止める

```c
macio_note_off(0);
```

`macio_note_on()` で指定した番号の音を止めます。急な音の途切れを和らげるため、内部で短いフェードアウトが行われます。

### `macio_wait()` — キー入力を待つ

```c
while (macio_wait()) {
    // キーが押されたとき、または離されたときに処理する
}
```

次のキー押下・解放イベントが来るまで待ちます。キー入力があったときは `1`、Ctrl+Cなどによる終了指示を受けたときは `0` を返します。

`macio_keydown()` と `macio_keyup()` は、**`macio_wait()` が戻った直後**に調べます。これらのイベント情報は、次の `macio_wait()` の開始時にリセットされます。

### `macio_keydown(key)` — 押した瞬間を調べる

```c
if (macio_keydown(kVK_ANSI_A)) {
    printf("Aキーが押されました\n");
}
```

指定したキーが新たに押されていれば `1`、そうでなければ `0` を返します。キーを長押ししたときの自動リピートは無視します。

### `macio_keyup(key)` — 離した瞬間を調べる

```c
if (macio_keyup(kVK_ANSI_A)) {
    printf("Aキーが離されました\n");
}
```

押していたキーが離されたときに `1` を返します。プログラム起動前から押されていたキーの解放は、通常は検出対象になりません。

### `macio_pressed(key)` — 押している間を調べる

```c
if (macio_pressed(kVK_ANSI_A)) {
    printf("Aキーを押しています\n");
}
```

指定したキーを現在押していれば `1`、押していなければ `0` を返します。`keydown` / `keyup` が瞬間的なイベントなのに対し、`pressed` は現在の状態です。

キー番号にはmacOS標準の定数を使います。例：`kVK_ANSI_Z`、`kVK_ANSI_A`、`kVK_ANSI_Comma`、`kVK_Space`、`kVK_Escape`。文字 `'A'` のASCIIコードをそのまま指定することはできません。

## 3. サンプルプログラム

### 例1：ラ（440Hz）を3秒鳴らす

`tone.c`

```c
#include "lib_macio.h"
// cc tone.c -o tone -framework ApplicationServices -framework AudioToolbox -framework AudioUnit -framework CoreFoundation

int main(void) {
    macio_init();

    macio_note_on(0, 440, TONE_TRIANGLE);
    sleep(3);
    macio_note_off(0);

    macio_close();
    return 0;
}
```

`sleep(3)` は3秒間待つ関数です。`lib_macio.h` から利用できます。

### 例2：ドレミファソラシドを鳴らす

`doremi.c`

```c
#include "lib_macio.h"
// cc doremi.c -o doremi -framework ApplicationServices -framework AudioToolbox -framework AudioUnit -framework CoreFoundation

int main(void) {
    float hz[] = {261.63, 293.66, 329.63, 349.23,
                  392.00, 440.00, 493.88, 523.25};
    macio_init();
    for (int i = 0; i < 8; i++) {
        macio_note_on(0, hz[i], TONE_TRIANGLE);
        usleep(500000);
        macio_note_off(0);
    }
    macio_close();
    return 0;
}
```

`usleep(500000)` は50万マイクロ秒（0.5秒）待つ関数です。

### 例3：ド・ミ・ソの和音を鳴らす

`chord.c`

```c
#include "lib_macio.h"
// cc chord.c -o chord -framework ApplicationServices -framework AudioToolbox -framework AudioUnit -framework CoreFoundation

int main(void) {
    macio_init();

    macio_note_on(0, 261.63, TONE_TRIANGLE);
    macio_note_on(1, 329.63, TONE_TRIANGLE);
    macio_note_on(2, 392.00, TONE_TRIANGLE);
    sleep(3);
    macio_note_off(0);
    macio_note_off(1);
    macio_note_off(2);

    macio_close();
    return 0;
}
```

番号を `0`、`1`、`2` と分けることで、3音を同時に鳴らしています。

### 例4：Zキーを押している間だけ音を鳴らす

`keyboard.c`

```c
#include "lib_macio.h"
// cc keyboard.c -o keyboard -framework ApplicationServices -framework AudioToolbox -framework AudioUnit -framework CoreFoundation

int main(void) {
    macio_init();
    printf("Zでド、Escキーを押すと終了\n");

    while (macio_wait()) {
        if (macio_keydown(kVK_Escape)) {
            break;
        }
        if (macio_keydown(kVK_ANSI_Z)) {
            macio_note_on(0, 261.63, TONE_TRIANGLE);
        }
        if (macio_keyup(kVK_ANSI_Z)) {
            macio_note_off(0);
        }
    }

    macio_close();
    return 0;
}
```

キーを押すと発音を始め、離すと止めます。Escキーで終了します。

## 4. キーイベントを表示するサンプル

`keyevent.c` として保存します。

```c
#include "lib_macio.h"
// cc keyevent.c -o keyevent -framework ApplicationServices -framework AudioToolbox -framework AudioUnit -framework CoreFoundation

int main(void) {
    macio_init();

    printf("Press keys (Esc to quit)\n");

    while (macio_wait()) {
        for (int key = 0; key < 128; key++) {
            if (macio_keydown(key)) {
                printf("keydown: %d\n", key);
            }
            if (macio_keyup(key)) {
                printf("keyup:   %d\n", key);
            }
        }
        if (macio_keydown(kVK_Escape)) {
            break;
        }
    }

    macio_close();
    return 0;
}
```

実行例：

```text
Press keys (Esc to quit)
keydown: 0
keyup:   0
keydown: 1
keyup:   1
keydown: 53
```

- `macio_init()`：キーボードイベントの取得を開始します。
- `macio_wait()`：次のキーイベントが届くまで待ちます。終了指示を受けると `0` を返します。
- `macio_keydown(key)`：そのキーが**押された瞬間**なら `1` です。
- `macio_keyup(key)`：そのキーが**離された瞬間**なら `1` です。
- `macio_pressed(key)`：そのキーが**押されている間**は `1` です（このサンプルでは未使用）。
- `macio_close()`：使用したリソースを解放します。

長押しによる自動リピートはライブラリ側で無視します。また、起動時に押されていたEnterキーのように、対応する `keydown` がない `keyup` は表示しません。

## 5. キー番号と `kVK_` 定数の対応表

`kVK_` はmacOSが提供する**仮想キーコード**の定数です。数値を直接書く代わりに、定数名を使うと何のキーなのかが分かりやすくなります。

### アルファベット

| キー | 番号 | 定数 | キー | 番号 | 定数 |
|---|---:|---|---|---:|---|
| A | 0 | `kVK_ANSI_A` | N | 45 | `kVK_ANSI_N` |
| B | 11 | `kVK_ANSI_B` | O | 31 | `kVK_ANSI_O` |
| C | 8 | `kVK_ANSI_C` | P | 35 | `kVK_ANSI_P` |
| D | 2 | `kVK_ANSI_D` | Q | 12 | `kVK_ANSI_Q` |
| E | 14 | `kVK_ANSI_E` | R | 15 | `kVK_ANSI_R` |
| F | 3 | `kVK_ANSI_F` | S | 1 | `kVK_ANSI_S` |
| G | 5 | `kVK_ANSI_G` | T | 17 | `kVK_ANSI_T` |
| H | 4 | `kVK_ANSI_H` | U | 32 | `kVK_ANSI_U` |
| I | 34 | `kVK_ANSI_I` | V | 9 | `kVK_ANSI_V` |
| J | 38 | `kVK_ANSI_J` | W | 13 | `kVK_ANSI_W` |
| K | 40 | `kVK_ANSI_K` | X | 7 | `kVK_ANSI_X` |
| L | 37 | `kVK_ANSI_L` | Y | 16 | `kVK_ANSI_Y` |
| M | 46 | `kVK_ANSI_M` | Z | 6 | `kVK_ANSI_Z` |

### 数字・記号（主にANSI配列での名称）

| キー | 番号 | 定数 | キー | 番号 | 定数 |
|---|---:|---|---|---:|---|
| 0 | 29 | `kVK_ANSI_0` | `=` | 24 | `kVK_ANSI_Equal` |
| 1 | 18 | `kVK_ANSI_1` | `[` | 33 | `kVK_ANSI_LeftBracket` |
| 2 | 19 | `kVK_ANSI_2` | `]` | 30 | `kVK_ANSI_RightBracket` |
| 3 | 20 | `kVK_ANSI_3` | `;` | 41 | `kVK_ANSI_Semicolon` |
| 4 | 21 | `kVK_ANSI_4` | `'` | 39 | `kVK_ANSI_Quote` |
| 5 | 23 | `kVK_ANSI_5` | `,` | 43 | `kVK_ANSI_Comma` |
| 6 | 22 | `kVK_ANSI_6` | `.` | 47 | `kVK_ANSI_Period` |
| 7 | 26 | `kVK_ANSI_7` | `/` | 44 | `kVK_ANSI_Slash` |
| 8 | 28 | `kVK_ANSI_8` | `\` | 42 | `kVK_ANSI_Backslash` |
| 9 | 25 | `kVK_ANSI_9` | `` ` `` | 50 | `kVK_ANSI_Grave` |
| `-` | 27 | `kVK_ANSI_Minus` |  |  |  |

**注意：** `ANSI` は定数名の一部です。日本語（JIS）キーボードでは、記号キーの刻印とANSI配列での名前が一致しない場合があります。実際のキー番号は、上のサンプルを動かして確かめてください。

### 日本語（JIS）キーボード固有のキー

| キー | 番号 | 定数 | キー | 番号 | 定数 |
|---|---:|---|---|---:|---|
| かな | 104 | `kVK_JIS_Kana` | _（ろ） | 94 | `kVK_JIS_Underscore` |
| 英数 | 102 | `kVK_JIS_Eisu` | テンキーのカンマ | 95 | `kVK_JIS_KeypadComma` |
| ¥ | 93 | `kVK_JIS_Yen` |  |  |  |

### 操作キー・矢印キー

| キー | 番号 | 定数 | キー | 番号 | 定数 |
|---|---:|---|---|---:|---|
| Return / Enter | 36 | `kVK_Return` | F2 | 120 | `kVK_F2` |
| Tab | 48 | `kVK_Tab` | F3 | 99 | `kVK_F3` |
| Space | 49 | `kVK_Space` | F4 | 118 | `kVK_F4` |
| Delete（Backspace） | 51 | `kVK_Delete` | F5 | 96 | `kVK_F5` |
| Escape | 53 | `kVK_Escape` | F6 | 97 | `kVK_F6` |
| Command（左） | 55 | `kVK_Command` | F7 | 98 | `kVK_F7` |
| Shift（左） | 56 | `kVK_Shift` | F8 | 100 | `kVK_F8` |
| Caps Lock | 57 | `kVK_CapsLock` | F9 | 101 | `kVK_F9` |
| Option（左） | 58 | `kVK_Option` | F10 | 109 | `kVK_F10` |
| Control（左） | 59 | `kVK_Control` | F11 | 103 | `kVK_F11` |
| Shift（右） | 60 | `kVK_RightShift` | F12 | 111 | `kVK_F12` |
| Option（右） | 61 | `kVK_RightOption` | ← | 123 | `kVK_LeftArrow` |
| Control（右） | 62 | `kVK_RightControl` | → | 124 | `kVK_RightArrow` |
| Fn | 63 | `kVK_Function` | ↓ | 125 | `kVK_DownArrow` |
| F1 | 122 | `kVK_F1` | ↑ | 126 | `kVK_UpArrow` |

※ キーボードの種類によって存在しないキーもあります。Fnキーや特殊キーは環境によって通常のキーイベントとして取得できないことがあります。

## 6. キーコードを使った条件分岐

数字を直接指定する場合：

```c
if (macio_keydown(0)) {
    printf("A key pressed\n");
}
```

`kVK_` 定数を使う場合：

```c
if (macio_keydown(kVK_ANSI_A)) {
    printf("A key pressed\n");
}
```

どちらも同じ意味ですが、**定数名を使う方が読みやすく、おすすめです。**

## 7. `keydown`・`keyup`・`pressed` の違い

| 操作 | `macio_keydown()` | `macio_keyup()` | `macio_pressed()` |
|---|---:|---:|---:|
| キーを押した瞬間 | 1 | 0 | 1 |
| 押し続けている間 | 0 | 0 | 1 |
| キーを離した瞬間 | 0 | 1 | 0 |
| 離している間 | 0 | 0 | 0 |

`keydown` と `keyup` は**直近の `macio_wait()` が返したイベント群**について調べる関数です。`pressed` は現在の押下状態を調べます。`macio_wait()` をもう一度呼ぶと、前回の `keydown`・`keyup` の記録はクリアされます。

## 8. 実行時の注意

- macOS専用のライブラリです。Terminal.appとVS Codeの内蔵ターミナルでの利用を想定しています。
- macOSの「システム設定 → プライバシーとセキュリティ → 入力監視」で、実行元のアプリに許可が必要です。
- 入力監視はターミナルだけでなく、**ほかのアプリ上のキー操作も検出する**場合があります。
- `lib_macio.h` が扱うキーコードの範囲は **0〜127** です。
- `Esc` でサンプルを終了します。`Ctrl+C` による終了にも対応しています。
- キーコードは入力された文字のASCIIコードではありません。たとえば `A` キーの仮想キーコードは `0` であり、ASCIIコードの `65` とは異なります。

## 9. 使うときのポイント

- **基本の順番**：`macio_init()` → 音を鳴らす・キーを調べる → `macio_close()`。
- **和音**：異なる `key` 番号を使って `macio_note_on()` を複数回呼びます。
- **押下と解放**：`macio_wait()` のあとに `macio_keydown()` / `macio_keyup()` を調べます。
- **終了処理**：プログラムを終えるときは `macio_close()` を呼びます。
- **音の高さ**：`440` は440Hzの「ラ」。数値を大きくすると高い音になります。
- **数値の書き方**：`float` 型の周波数に `261.63` と書いても構いません。末尾の `f` は必須ではありません。
