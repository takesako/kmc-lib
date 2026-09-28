// lib_png16.h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    int width;
    int height;
    int row_size;
    unsigned char *pixels;
    unsigned char palette[16][3];
} png16;

static int color = 0;
static int background_color = 15;
static int dot_size = 1;

// 16色パレットを初期化
static void _set_default_palette(void)
{
    const unsigned char p[16][3] = {
        {  0,  0,  0}, {128,  0,  0}, {  0,128,  0}, {128,128,  0},
        {  0,  0,128}, {128,  0,128}, {  0,128,128}, {192,192,192},
        {128,128,128}, {255,  0,  0}, {  0,255,  0}, {255,255,  0},
        {  0,  0,255}, {255,  0,255}, {  0,255,255}, {255,255,255}
    };
    memcpy(png16.palette, p, sizeof p);
}

// 描画色を指定
void set_color(int n)
{
    if (n < 0 || n > 15) return;
    color = n;
}

// 背景色を指定
void set_background_color(int n)
{
    if (n < 0 || n > 15) return;
    background_color = n;
}

// パレットのRGBを変更
void set_palette_color_rgb(int n, int r, int g, int b)
{
    if (n < 0 || n > 15) return;
    if (r < 0 || r > 255) return;
    if (g < 0 || g > 255) return;
    if (b < 0 || b > 255) return;
    png16.palette[n][0] = r;
    png16.palette[n][1] = g;
    png16.palette[n][2] = b;
}

// 点と線の太さを指定
void set_dot_size(int n)
{
    if (n < 1) n = 1;
    dot_size = n;
}

// 画像を作成
int create_image(int width, int height)
{
    if (width <= 0 || height <= 0) return -1;
    free(png16.pixels);
    png16.pixels = NULL;
    png16.width = width;
    png16.height = height;
    png16.row_size = (width + 1) / 2;
    png16.pixels = malloc((int)png16.row_size * height);
    if (!png16.pixels) return -1;
    _set_default_palette();
    unsigned char fill = (background_color << 4) | background_color;
    memset(png16.pixels, fill, (int)png16.row_size * height);
    return 0;
}

// 1ピクセル描画
void put_pixel(int x, int y)
{
    if (!png16.pixels) return;
    if (x < 0 || x >= png16.width) return;
    if (y < 0 || y >= png16.height) return;
    unsigned char *p = png16.pixels + y * png16.row_size + x / 2;
    if (x & 1)
        *p = (*p & 0xf0) | color;
    else
        *p = (*p & 0x0f) | (color << 4);
}

// 点を描画
void draw_dot(int x, int y)
{
    int a = (dot_size - 1) / 2;
    int b = dot_size / 2;
    for (int yy = y - a; yy <= y + b; yy++)
        for (int xx = x - a; xx <= x + b; xx++)
            put_pixel(xx, yy);
}

// 長方形を塗りつぶす
void fill_rect(int x1, int y1, int x2, int y2)
{
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y1 > y2) {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            put_pixel(x, y);
}

// 直線を描画
void draw_line(int x1, int y1, int x2, int y2)
{
    int dx = abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        draw_dot(x1, y1);
        if (x1 == x2 && y1 == y2) break;
        int e = 2 * err;
        if (e >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

// 32bit整数をビッグエンディアンで書く
static void _write_be32(FILE *fp, unsigned int n)
{
    fputc(n >> 24, fp);
    fputc(n >> 16, fp);
    fputc(n >> 8, fp);
    fputc(n, fp);
}

static void _put_be32(unsigned char *p, unsigned int n)
{
    p[0] = n >> 24;
    p[1] = n >> 16;
    p[2] = n >> 8;
    p[3] = n;
}

// PNGで使うCRC32
static unsigned int _crc32(unsigned int c, const void *data, int n)
{
    const unsigned char *p = data;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++)
            c = (c & 1) ? (c >> 1) ^ 0xedb88320U : c >> 1;
    }
    return c;
}

// PNGチャンクを書く
static void _write_chunk(FILE *fp, const char type[4], const void *data, unsigned int size)
{
    unsigned int c = 0xffffffffU;
    _write_be32(fp, size);
    fwrite(type, 1, 4, fp);
    if (size) fwrite(data, 1, size, fp);
    c = _crc32(c, type, 4);
    c = _crc32(c, data, size);
    _write_be32(fp, ~c);
}

// PNGを保存
int save_image(const char *filename)
{
    if (!png16.pixels) return -1;
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;
    int row = png16.row_size + 1;
    int raw_size = (int)row * png16.height;
    unsigned char *raw = malloc(raw_size);
    if (!raw) {
        fclose(fp);
        return -1;
    }
    // 各行の先頭にPNGフィルタ0を付ける
    for (int y = 0; y < png16.height; y++) {
        raw[y * row] = 0;
        memcpy(raw + y * row + 1,
               png16.pixels + y * png16.row_size,
               png16.row_size);
    }
    // zlib + 無圧縮DEFLATE
    int blocks = (raw_size + 65534) / 65535;
    int zmax = raw_size + blocks * 5 + 6;
    unsigned char *z = malloc(zmax);
    if (!z) {
        free(raw);
        fclose(fp);
        return -1;
    }
    int q = 0;
    int pos = 0;
    z[q++] = 0x78;
    z[q++] = 0x01;
    while (pos < raw_size) {
        unsigned int n = raw_size - pos > 65535 ? 65535 : raw_size - pos;
        unsigned int nn = 0xffff - n;
        z[q++] = pos + n == raw_size;
        z[q++] = n;
        z[q++] = n >> 8;
        z[q++] = nn;
        z[q++] = nn >> 8;
        memcpy(z + q, raw + pos, n);
        q += n;
        pos += n;
    }
    // Adler-32
    unsigned int a = 1;
    unsigned int b = 0;
    for (int i = 0; i < raw_size; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    _put_be32(z + q, (b << 16) | a);
    q += 4;
    // PNGを書き出す
    const unsigned char signature[8] = {
        137,80,78,71,13,10,26,10
    };
    unsigned char ihdr[13] = {0};
    fwrite(signature, 1, 8, fp);
    _put_be32(ihdr, png16.width);
    _put_be32(ihdr + 4, png16.height);
    ihdr[8] = 4;
    ihdr[9] = 3;
    _write_chunk(fp, "IHDR", ihdr, 13);
    _write_chunk(fp, "PLTE", png16.palette, 16 * 3);
    _write_chunk(fp, "IDAT", z, q);
    _write_chunk(fp, "IEND", NULL, 0);
    free(z);
    free(raw);
    fclose(fp);
    return 0;
}
