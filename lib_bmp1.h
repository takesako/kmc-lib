#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// 構造体のパディングを1バイト単位で詰める
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
    uint16_t biBitCount;      // 1bit/pixel
    uint32_t biCompression;   // 0 = BI_RGB (非圧縮)
    uint32_t biSizeImage;     // 画像データサイズ（バイト）
    int32_t  biXPelsPerMeter; // 解像度（省略で 0）
    int32_t  biYPelsPerMeter;
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
//  1bit BMP の情報（グローバルに1枚だけ持つ）
//=====================================================
typedef struct {
    int width;   // 幅
    int height;  // 高さ
    int rowSize; // 1行あたりのバイト数（4バイト境界）
    unsigned char *pixels; // 画素データ（ボトムアップ）
} Bitmap1bpp;

Bitmap1bpp bmp1; // グローバル変数

//=====================================================
//  作成: 背景色 bgColor (0=白, 1=黒)
//=====================================================
int bmp1_create(int width, int height, int bgColor) {
    if (width <= 0 || height <= 0) return -1;

    bmp1.width  = width;
    bmp1.height = height;

    int bpp = 1; // 1bit
    bmp1.rowSize = ((width * bpp + 31) / 32) * 4;

    size_t imageSize = (size_t)bmp1.rowSize * (size_t)height;
    bmp1.pixels = (unsigned char *)malloc(imageSize);
    if (!bmp1.pixels) {
        return -1;
    }

    // 背景を塗りつぶす
    memset(bmp1.pixels, bgColor ? 0xFF : 0x00, imageSize);
    return 0;
}


//=====================================================
//  ピクセル設定: (x,y) に color(0=白,1=黒) をセット
//  座標系: (0,0) が左上、x 右・y 下向き
//=====================================================
void bmp1_set_pixel(int x, int y, int color) {
    if (!bmp1.pixels) return;
    if (x < 0 || x >= bmp1.width) return;
    if (y < 0 || y >= bmp1.height) return;

    // BMP はボトムアップ：y=0(上) はメモリでは最後の行
    int srcY = bmp1.height - 1 - y;
    unsigned char *row = bmp1.pixels + (size_t)srcY * bmp1.rowSize;

    int byteIndex = x / 8;
    int bitIndex  = 7 - (x % 8); // 左端が MSB
    unsigned char mask = (unsigned char)(1 << bitIndex);

    if (color)
        row[byteIndex] |= mask; // 黒 (=1)
    else
        row[byteIndex] &= (unsigned char)(~mask); // 白 (=0)
}


//=====================================================
//  矩形塗りつぶし: (x1,y1)～(x2,y2) を color で塗る
//=====================================================
void bmp1_fill_rect(int x1, int y1, int x2, int y2, int color) {
    if (!bmp1.pixels) return;

    // 座標順序の正規化
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }

    // 画面外なら何もしない
    if (x2 < 0 || y2 < 0) return;
    if (x1 >= bmp1.width || y1 >= bmp1.height) return;

    // 画面内にクリップ
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= bmp1.width)  x2 = bmp1.width - 1;
    if (y2 >= bmp1.height) y2 = bmp1.height - 1;

    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            bmp1_set_pixel(x, y, color);
        }
    }
}


//=====================================================
//  直線描画: Bresenham アルゴリズム
//  (x1,y1)～(x2,y2) を color で描く
//=====================================================
void bmp1_draw_line(int x1, int y1, int x2, int y2, int color) {
    if (!bmp1.pixels) return;

    int dx = x2 - x1;
    int dy = y2 - y1;

    int sx = (dx >= 0) ? 1 : -1;
    int sy = (dy >= 0) ? 1 : -1;

    dx = dx >= 0 ? dx : -dx;
    dy = dy >= 0 ? dy : -dy;

    bmp1_set_pixel(x1, y1, color);

    if (dx >= dy) {
        // x が主軸
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
            bmp1_set_pixel(x, y, color);
        }
    } else {
        // y が主軸
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
            bmp1_set_pixel(x, y, color);
        }
    }
}

//=====================================================
//  BMP 保存
//=====================================================
int bmp1_save(const char *filename) {
    if (!bmp1.pixels) return -1;

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    int width  = bmp1.width;
    int height = bmp1.height;
    int imageSize = bmp1.rowSize * height;

    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;

    fileHeader.bfType      = 0x4D42; // 'BM'
    fileHeader.bfSize      = sizeof(BITMAPFILEHEADER)
                           + sizeof(BITMAPINFOHEADER)
                           + 2 * sizeof(RGBQUAD)
                           + imageSize;
    fileHeader.bfReserved1 = 0;
    fileHeader.bfReserved2 = 0;
    fileHeader.bfOffBits   = sizeof(BITMAPFILEHEADER)
                           + sizeof(BITMAPINFOHEADER)
                           + 2 * sizeof(RGBQUAD);

    infoHeader.biSize          = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth         = width;
    infoHeader.biHeight        = height;
    infoHeader.biPlanes        = 1;
    infoHeader.biBitCount      = 1; // 1bit/pixel
    infoHeader.biCompression   = 0; // BI_RGB
    infoHeader.biSizeImage     = imageSize;
    infoHeader.biXPelsPerMeter = 0;
    infoHeader.biYPelsPerMeter = 0;
    infoHeader.biClrUsed       = 2; // 白・黒
    infoHeader.biClrImportant  = 0;

    // ヘッダ書き込み
    fwrite(&fileHeader, sizeof(fileHeader), 1, fp);
    fwrite(&infoHeader, sizeof(infoHeader), 1, fp);

    // パレット: index 0 = 白, index 1 = 黒
    RGBQUAD palette[2] = {
        {255, 255, 255, 0}, // 白
        {0,   0,   0,   0}  // 黒
    };
    fwrite(palette, sizeof(RGBQUAD), 2, fp);

    // 画素データ
    fwrite(bmp1.pixels, 1, imageSize, fp);

    fclose(fp);
    return 0;
}


//=====================================================
//  解放
//=====================================================
void bmp1_free(void) {
    if (bmp1.pixels) {
        free(bmp1.pixels);
        bmp1.pixels = NULL;
    }
}
