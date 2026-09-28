# `lib_png16.h` ライブラリ

`lib_png16.h` は、16色のパレットを使って簡単なドット絵を描き、その画像をPNGファイルとして保存するためのライブラリです。
ファイルの先頭で以下の1行を追加すると使えるようになります。
```c
#include "lib_png16.h"
```

## 関数一覧

- `int create_image(int width, int height)` : 横幅 `width`、縦幅 `height` の新しい画像を作成します。
- `int save_image(const char *filename)` : 作成した画像をPNGファイルとして保存します。
- `void set_color(int n)` : これから描画するときに使う色を0〜15で指定します。
- `void set_background_color(int n)` : 新しい画像を作るときの背景色を0〜15で指定します。
- `void set_palette_color_rgb(int n, int r, int g, int b)` : パレットの `n` 番の色をRGBで変更します。
- `void set_dot_size(int n)` : `draw_dot()` と `draw_line()` で使う点の大きさを指定します。
- `void put_pixel(int x, int y)` : 座標 `(x, y)` に1ピクセルだけ描画します。
- `void draw_dot(int x, int y)` : 座標 `(x, y)` に指定した大きさの点を描画します。
- `void draw_line(int x1, int y1, int x2, int y2)` : `(x1, y1)` から `(x2, y2)` まで直線を描画します。
- `void fill_rect(int x1, int y1, int x2, int y2)` : `(x1, y1)` から `(x2, y2)` までの長方形を塗りつぶします。

## 座標について

画像の左上が `(0, 0)` です。

`x` は右に行くほど大きくなり、`y` は下に行くほど大きくなります。

```text
(0,0) ─────────────────→ x (199, 0)
  │
  │
  │
  ↓
  y
(0, 149)
```

たとえば、200×150ピクセルの画像では次のようになります。

```text
左上     (0, 0)
右上     (199, 0)
左下     (0, 149)
右下     (199, 149)
```

## 色について

色は0〜15のパレット番号で指定します。

初期状態では以下の16色が使えます。

```text
番号 (  R,   G,   B)   色の説明
 0   (  0,   0,   0)   黒
 1   (128,   0,   0)   暗い赤
 2   (  0, 128,   0)   暗い緑
 3   (128, 128,   0)   オリーブ
 4   (  0,   0, 128)   暗い青
 5   (128,   0, 128)   紫
 6   (  0, 128, 128)   青緑
 7   (192, 192, 192)   明るい灰色
 8   (128, 128, 128)   灰色
 9   (255,   0,   0)   赤
10   (  0, 255,   0)   緑
11   (255, 255,   0)   黄
12   (  0,   0, 255)   青
13   (255,   0, 255)   マゼンタ
14   (  0, 255, 255)   シアン
15   (255, 255, 255)   白
```

描画色の初期値は `0` の黒です。

背景色の初期値は `15` の白です。

## `create_image()`

```c
int create_image(int width, int height);
```

サイズを指定して新しい画像を作ります。

```c
create_image(200, 150);
```

これは横200ピクセル、縦150ピクセルの画像を作ります。

背景色を変更したい場合は、`create_image()` より前に `set_background_color()` を呼びます。

```c
set_background_color(12);
create_image(200, 150);
```

この例では、青い背景の画像を作ります。

`create_image()` をもう一度呼ぶと、それまでの画像は消えて新しい画像になります。

正常に作成できた場合は `0`、失敗した場合は `-1` を返します。

## `save_image()`

```c
int save_image(const char *filename);
```

作成した画像をPNGファイルとして保存します。

```c
save_image("picture.png");
```

実行すると `picture.png` が作成されます。

保存後も画像のデータは残っているので、さらに描画を続けて別のファイルとしても保存できます。

```c
save_image("before.png");

set_color(9);
draw_line(10, 10, 100, 100);

save_image("after.png");
```

正常に保存できた場合は `0`、失敗した場合は `-1` を返します。

## `set_color()`

```c
void set_color(int n);
```

これから描く色を0〜15のパレット番号で指定します。

```c
set_color(9);
```

これ以降、描画する色は9番になります。

```c
set_color(9);
put_pixel(10, 20);
put_pixel(11, 20);

set_color(12);
put_pixel(12, 20);
```

最初の2ピクセルは9番、最後の1ピクセルは12番の色になります。

初期値は `0` の黒です。

## `set_background_color()`

```c
void set_background_color(int n);
```

新しい画像を作るときの背景色を指定します。

```c
set_background_color(14);
create_image(200, 150);
```

背景色は `create_image()` より前に指定します。

初期値は `15` の白です。

## `set_palette_color_rgb()`

```c
void set_palette_color_rgb(int n, int r, int g, int b);
```

指定したパレット番号の色をRGB値で自由に変更します。

`n` は変更するパレット番号で、0〜15を指定します。

`r`、`g`、`b` は赤・緑・青の強さで、それぞれ0〜255を指定します。

```text
r = 赤の強さ (0〜255)
g = 緑の強さ (0〜255)
b = 青の強さ (0〜255)
```

以下の例では、9番の色をオレンジに変更しています。

```c
set_palette_color_rgb(9, 255, 128, 0);
```

パレットは `create_image()` を呼ぶと初期状態に戻ります。

パレットを変更するときは `create_image()` の後に指定します。

```c
create_image(200, 150);

set_palette_color_rgb(9, 255, 128, 0);
set_color(9);
```

## `set_dot_size()`

```c
void set_dot_size(int n);
```

`draw_dot()` と `draw_line()` で使う点の大きさを指定します。

```c
set_dot_size(1);
```

なら1ピクセルの大きさです。

```c
set_dot_size(5);
```

なら5×5ピクセルの大きな点になります。

初期値は `1` です。

`put_pixel()` と `fill_rect()` には影響しません。

## `put_pixel()`

```c
void put_pixel(int x, int y);
```

座標 `(x, y)` に1ピクセルだけ描画します。

```c
set_color(9);
put_pixel(50, 30);
```

座標 `(50, 30)` に9番の色で1ピクセル描きます。

細かい模様や星などを描くのに向いています。

```c
set_color(15);

put_pixel(20, 20);
put_pixel(40, 15);
put_pixel(60, 30);
put_pixel(100, 10);
```

`put_pixel()` は `set_dot_size()` の影響を受けません。

常に1ピクセルです。

## `draw_dot()`

```c
void draw_dot(int x, int y);
```

座標 `(x, y)` に点を描きます。

点の大きさは `set_dot_size()` で指定します。

```c
set_color(9);
set_dot_size(5);
draw_dot(50, 50);
```

大きな点を描く場合は次のようにします。

```c
set_dot_size(15);
draw_dot(80, 60);
```

キャラクターの目、ほっぺ、太陽などを描くときに使えます。

## `draw_line()`

```c
void draw_line(int x1, int y1, int x2, int y2);
```

2つの座標を結ぶ直線を描きます。

```c
set_color(9);
draw_line(10, 20, 100, 80);
```

これは `(10, 20)` から `(100, 80)` まで線を描きます。

線の太さは `set_dot_size()` で変更できます。

```c
set_color(12);
set_dot_size(1);
draw_line(10, 20, 100, 80);

set_color(9);
set_dot_size(5);
draw_line(10, 40, 100, 100);
```

## `fill_rect()`

```c
void fill_rect(int x1, int y1, int x2, int y2);
```

指定した範囲を長方形で塗りつぶします。

```c
set_color(9);
fill_rect(20, 30, 80, 60);
```

`(20, 30)` から `(80, 60)` までを9番の色で塗ります。

キャラクターの顔や体、建物、背景などを描くときに便利です。

`fill_rect()` は `set_dot_size()` の影響を受けません。

## 基本的な使い方

以下のような順番で関数を呼び出して画像を作成します。

```c
#include "lib_png16.h"

int main(void)
{
    set_background_color(15);
    create_image(200, 150);

    set_color(9);
    fill_rect(50, 50, 100, 100);

    save_image("picture.png");

    return 0;
}
```

パレットの色や線の太さを変更する場合は、次のように書けます。

```c
#include "lib_png16.h"

int main(void)
{
    set_background_color(14);
    create_image(200, 150);

    set_palette_color_rgb(9, 255, 128, 0);

    set_color(9);
    set_dot_size(3);

    draw_dot(50, 50);
    draw_line(20, 20, 100, 80);
    fill_rect(120, 30, 150, 60);

    save_image("picture.png");

    return 0;
}
```

基本的なプログラムの流れとしては、

```text
背景色を決める
↓
画像を作る
↓
パレットの色を変更する
↓
描画色や点の大きさを決める
↓
絵を描く
↓
PNGとして保存する
```

という順番で使います。
