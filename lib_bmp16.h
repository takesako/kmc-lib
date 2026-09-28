#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// 構造体のパディングを1バイト単位で詰める pack(pop)まで
#pragma pack(push, 1)

// BMP ファイルヘッダ (14バイト)
typedef struct {
    uint16_t bfType;      // "BM" (0x4D42)
    uint32_t bfSize;      // ファイル全体サイズ
    uint16_t bfReserved1; // 0
    uint16_t bfReserved2; // 0
    uint32_t bfOffBits;   // 先頭から画素データまでのオフセット
} BITMAPFILEHEADER;

// BITMAPINFOHEADER (40バイト)
typedef struct {
    uint32_t biSize;          // 構造体サイズ (40)
    int32_t  biWidth;         // 幅（ピクセル）
    int32_t  biHeight;        // 高さ（ピクセル）正:ボトムアップ
    uint16_t biPlanes;        // 常に 1
    uint16_t biBitCount;      // 4bit/pixel
    uint32_t biCompression;   // 0 = BI_RGB (非圧縮)
    uint32_t biSizeImage;     // 画像データサイズ（バイト）
    int32_t  biXPelsPerMeter; // 解像度（省略で 0）
    int32_t  biYPelsPerMeter; // 解像度（省略で 0）
    uint32_t biClrUsed;       // パレット色数
    uint32_t biClrImportant;  // 重要な色数
} BITMAPINFOHEADER;

// 1パレット4バイト (B, G, R, 予約)
typedef struct {
    uint8_t rgbBlue;
    uint8_t rgbGreen;
    uint8_t rgbRed;
    uint8_t rgbReserved;
} RGBQUAD;

#pragma pack(pop)

//=====================================================
//  4bit (16色) BMP の情報（グローバルに1枚だけ持つ）
//=====================================================
typedef struct {
    int width;   // 幅
    int height;  // 高さ
    int rowSize; // 1行あたりのバイト数（4バイト境界）
    unsigned char *pixels; // 画素データ
    RGBQUAD palette[16];   // 16色パレット
} Bitmap4bpp;

Bitmap4bpp bmp16; // グローバル変数

//=====================================================
//  16色パレットをデフォルト配色で初期化
//=====================================================
void bmp16_palette_set_default(void) {
    const RGBQUAD p[16] = {
        {  0,   0,   0, 0}, //  0: black
        {128,   0,   0, 0}, //  1: maroon
        {  0, 128,   0, 0}, //  2: green
        {128, 128,   0, 0}, //  3: olive
        {  0,   0, 128, 0}, //  4: navy
        {128,   0, 128, 0}, //  5: purple
        {  0, 128, 128, 0}, //  6: teal
        {192, 192, 192, 0}, //  7: silver
        {128, 128, 128, 0}, //  8: gray
        {255,   0,   0, 0}, //  9: red
        {  0, 255,   0, 0}, // 10: lime
        {255, 255,   0, 0}, // 11: yellow
        {  0,   0, 255, 0}, // 12: blue
        {255,   0, 255, 0}, // 13: fuchsia
        {  0, 255, 255, 0}, // 14: aqua
        {255, 255, 255, 0}  // 15: white
    };
    memcpy(bmp16.palette, p, sizeof(bmp16.palette));
}

//=====================================================
//  パレットの特定indexの色を設定 (0..15)
//  r,g,b は 0..255
//=====================================================
int bmp16_palette_set_rgb(int r, int g, int b, int index) {
    if (index < 0 || index > 15) return -1;

    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;

    bmp16.palette[index].rgbRed = (uint8_t)r;
    bmp16.palette[index].rgbGreen = (uint8_t)g;
    bmp16.palette[index].rgbBlue = (uint8_t)b;
    bmp16.palette[index].rgbReserved = 0;
    return 0;
}

//=====================================================
//  作成: 背景色 bgIndex (0..15)
//=====================================================
int bmp16_create(int width, int height, int bgIndex) {
    if (width <= 0 || height <= 0) return -1;
    if (bgIndex < 0) bgIndex = 0;
    if (bgIndex > 15) bgIndex = 15;

    // 既存があれば解放してから作り直す（安全側）
    if (bmp16.pixels) {
        free(bmp16.pixels);
        bmp16.pixels = NULL;
    }

    bmp16.width  = width;
    bmp16.height = height;

    // 4bpp: 1ピクセル=4bit → 1行の生データは ceil(width/2) バイト
    // rowSize は 4バイト境界に切り上げ
    int rawRowBytes = (width + 1) / 2;
    bmp16.rowSize = (rawRowBytes + 3) / 4 * 4;

    size_t imageSize = (size_t)bmp16.rowSize * (size_t)height;
    bmp16.pixels = (unsigned char *)malloc(imageSize);
    if (!bmp16.pixels) return -1;

    // パレットをデフォルトで初期化（必要ならこの行を消して「保持」挙動にできる）
    bmp16_palette_set_default();

    // 背景で塗りつぶし（1バイト=2ピクセル）
    unsigned char fill = (unsigned char)(((bgIndex & 0x0F) << 4) | (bgIndex & 0x0F));
    memset(bmp16.pixels, fill, imageSize);

    return 0;
}

//=====================================================
//  ピクセル設定: (x,y) に color(0..15) をセット
//  座標系: (0,0) が左上、x 右・y 下向き
//=====================================================
void bmp16_set_pixel(int x, int y, int color) {
    if (!bmp16.pixels) return;
    if (x < 0 || x >= bmp16.width) return;
    if (y < 0 || y >= bmp16.height) return;

    if (color < 0) color = 0;
    if (color > 15) color = 15;

    // BMP はボトムアップ：y=0(上) はメモリでは最後の行
    int srcY = bmp16.height - 1 - y;
    unsigned char *row = bmp16.pixels + (size_t)srcY * bmp16.rowSize;

    int byteIndex = x / 2;
    int isLeftPixel = (x % 2 == 0); // trueなら上位ニブル(左側ピクセル)

    unsigned char v = row[byteIndex];
    if (isLeftPixel) {
        v = (unsigned char)((v & 0x0F) | ((color & 0x0F) << 4));
    } else {
        v = (unsigned char)((v & 0xF0) | (color & 0x0F));
    }
    row[byteIndex] = v;
}

//=====================================================
//  矩形塗りつぶし: (x1,y1)～(x2,y2) を color で塗る
//=====================================================
void bmp16_fill_rect(int x1, int y1, int x2, int y2, int color) {
    if (!bmp16.pixels) return;

    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }

    if (x2 < 0 || y2 < 0) return;
    if (x1 >= bmp16.width || y1 >= bmp16.height) return;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= bmp16.width)  x2 = bmp16.width - 1;
    if (y2 >= bmp16.height) y2 = bmp16.height - 1;

    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            bmp16_set_pixel(x, y, color);
        }
    }
}

//=====================================================
//  直線描画: Bresenham アルゴリズム
//  (x1,y1)～(x2,y2) を color で描く
//=====================================================
void bmp16_draw_line(int x1, int y1, int x2, int y2, int color) {
    if (!bmp16.pixels) return;

    int dx = x2 - x1;
    int dy = y2 - y1;

    int sx = (dx >= 0) ? 1 : -1;
    int sy = (dy >= 0) ? 1 : -1;

    dx = dx >= 0 ? dx : -dx;
    dy = dy >= 0 ? dy : -dy;

    bmp16_set_pixel(x1, y1, color);

    if (dx >= dy) {
        int err = 2 * dy - dx;
        int x = x1;
        int y = y1;
        for (int i = 0; i < dx; i++) {
            x += sx;
            if (err > 0) {
                y += sy;
                err -= 2 * dx;
            }
            err += 2 * dy;
            bmp16_set_pixel(x, y, color);
        }
    } else {
        int err = 2 * dx - dy;
        int x = x1;
        int y = y1;
        for (int i = 0; i < dy; i++) {
            y += sy;
            if (err > 0) {
                x += sx;
                err -= 2 * dy;
            }
            err += 2 * dx;
            bmp16_set_pixel(x, y, color);
        }
    }
}

//=====================================================
//  BMP画像ファイルを保存（4bpp / 16色パレット）
//=====================================================
int bmp16_save(const char *filename) {
    if (!bmp16.pixels) return -1;

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    int width  = bmp16.width;
    int height = bmp16.height;
    int imageSize = bmp16.rowSize * height;

    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;

    const uint32_t paletteCount = 16;

    fileHeader.bfType      = 0x4D42; // 'BM'
    fileHeader.bfOffBits   = sizeof(BITMAPFILEHEADER)
                           + sizeof(BITMAPINFOHEADER)
                           + paletteCount * sizeof(RGBQUAD);
    fileHeader.bfSize      = fileHeader.bfOffBits + (uint32_t)imageSize;
    fileHeader.bfReserved1 = 0;
    fileHeader.bfReserved2 = 0;

    infoHeader.biSize          = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth         = width;
    infoHeader.biHeight        = height;
    infoHeader.biPlanes        = 1;
    infoHeader.biBitCount      = 4;  // 4bit/pixel
    infoHeader.biCompression   = 0;  // BI_RGB
    infoHeader.biSizeImage     = (uint32_t)imageSize;
    infoHeader.biXPelsPerMeter = 0;
    infoHeader.biYPelsPerMeter = 0;
    infoHeader.biClrUsed       = paletteCount;
    infoHeader.biClrImportant  = 0;

    fwrite(&fileHeader, sizeof(fileHeader), 1, fp);
    fwrite(&infoHeader, sizeof(infoHeader), 1, fp);
    fwrite(bmp16.palette, sizeof(RGBQUAD), paletteCount, fp);
    fwrite(bmp16.pixels, 1, (size_t)imageSize, fp);

    fclose(fp);
    return 0;
}

//=====================================================
//  解放
//=====================================================
void bmp16_free(void) {
    if (bmp16.pixels) {
        free(bmp16.pixels);
        bmp16.pixels = NULL;
    }
}

/*
使い方:

#include "lib_bmp16.h"

int main(void){
    bmp16_create(200, 120, 15);           // 200x120 背景色=白(15)
    bmp16_palette_set_rgb(255, 128, 0, 9); // 9番(赤)をオレンジに変更
    bmp16_draw_line( 10,10,190,100, 9);    // 9番で描く→オレンジで描かれる
    bmp16_draw_line(190,10, 10,190, 9);    // 9番で描く→オレンジで描かれる
    bmp16_save("bmp_lib16.bmp"); // 画像を保存
    system("open bmp_lib16.bmp"); // 画像を開く
    return 0;
}
*/
