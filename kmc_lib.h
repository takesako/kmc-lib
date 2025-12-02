/**
 * @file kmc_lib.h
 * @brief 神山まるごと高専プログラミング演習用ライブラリ 0.1
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>

/**
 * @brief タイピングモードにする（キー入力を即座に読み取り、エコーしない）
 *
 * 標準入力端末の属性を変更して、次のモードにします。
 * - 非カノニカルモード：改行を待たずに１文字ずつ読み取る
 * - エコー無効　　　　：入力した文字が画面に表示されない
 * @c setbuf(stdout, NULL) で標準出力のバッファリングを無効化することで
 * 改行なしの @c printf 直後に画面へ即座に表示されるようになります。
 */
void kmc_typingmode(void) {
    struct termios t; setbuf(stdout, NULL);
    tcgetattr(STDIN_FILENO, &t); t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}
/**
 * @brief timeout 秒以内に標準入力にキー入力があったかどうかを調べる
 *
 * 標準入力（@c stdin ）に対して @c select(2) を用い、
 * @p timeout 秒以内に読み取り可能なデータが到着したかどうかを判定します。
 * この関数は入力文字を消費しません。実際の読み取りは呼び出し側で
 * @c getchar() などを用いて行ってください。
 *
 * @param timeout キー入力待ちの秒数（0 以上の整数）。
 *                0 を指定すると即時判定（ポーリング）になります。
 *
 * @retval 0     入力可能なデータが無い、または内部エラーが発生した
 * @retval != 0  読み取り可能なデータが到着している
 */
int kmc_keypressed(int timeout) {
    struct timeval tv = {timeout, 0};
    fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}
