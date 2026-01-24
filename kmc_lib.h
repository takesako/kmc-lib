/**
 * @file kmc_lib.h
 * @brief 神山まるごと高専プログラミング演習用ライブラリ 0.2
 */
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#endif

/**
 * @brief タイピングモードにする（キー入力を即座に読み取り、エコーしない）
 *
 * 非Windowsの場合、標準入力端末の属性を変更して、次のモードにします。
 * - 非カノニカルモード：改行を待たずに１文字ずつ読み取る
 * - エコー無効　　　　：入力した文字が画面に表示されない
 * @c setbuf(stdout, NULL) で標準出力のバッファリングを無効化することで
 * 改行なしの @c printf 直後に画面へ即座に表示されるようになります。
 */
void kmc_typingmode(void) {
#ifdef _WIN32
    setbuf(stdout, NULL);
#else
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    setbuf(stdout, NULL);
#endif
}

/**
 * @brief timeout 秒以内に標準入力にキー入力があったかどうかを調べる
 *
 * 標準入力（@c stdin ）に対して @c select(2) を用い、
 * @p timeout 秒以内に読み取り可能なデータが到着したかどうかを判定します。
 * この関数は入力文字を消費しません。実際の読み取りは呼び出し側で
 * @c getchar() 等を使ってください。Windowsは @c _getch() を使います。
 *
 * @param timeout キー入力待ちの秒数（0 以上の整数）。
 *                0 を指定すると即時判定（ポーリング）になります。
 *
 * @retval 0     入力可能なデータが無い、または内部エラーが発生した
 * @retval != 0  読み取り可能なデータが到着している
 */
int kmc_keypressed(int timeout) {
#ifdef _WIN32
    if (timeout == 0) {
        return _kbhit();
    }
    DWORD end = GetTickCount() + timeout * 1000;
    while (GetTickCount() < end) {
        if (_kbhit()) return 1;
        Sleep(1);
    }
    return 0;
#else
    fd_set fds;
    struct timeval tv = {timeout, 0};
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
#endif
}

/**
 * @brief Windowsの場合、Enterキーを押さず文字を読み出すため @c _getch() を使う
 */
#ifdef _WIN32
#define getchar() _getch()
#endif
