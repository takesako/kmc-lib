// mml_lib.h として保存する
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// サンプリング周波数
#ifndef SR
#define SR 48000
#endif

// 定数マクロ円周率
#define PI  3.14159265358979323846

// 英小文字を大文字にする三項演算子マクロ
#define UC(c) ((c)>='a'&&(c)<='z') ? ((c)-32) : (c)

// 数字の読み取り
double read_number(const char **pp){
  const char *p=*pp; double v=0; int any=0;
  while(*p>='0'&&*p<='9'){ v=v*10+(*p-'0'); p++; any=1; }
  if(any){ *pp=p; return v; }
  return -1;
}

// 音のインデックス（C=0..B=11）
int note_index_from_char(char c) {
  switch (c) {
    case 'C': return 0;  // ド
    case 'D': return 2;  // レ
    case 'E': return 4;  // ミ
    case 'F': return 5;  // ファ
    case 'G': return 7;  // ソ
    case 'A': return 9;  // ラ
    case 'B': return 11; // シ
    default:  return -1; // 他
  }
}

// 近似sin（5次多項式）: xは[-PI/2,PI/2]に正規化する
double sin5(double x){
  double k = x / (2.0 * PI); // x ≒ x - round(x/(2π))*2π
  long n = (long)(k >= 0.0 ? k + 0.5 : k - 0.5); // round
  x -= n * 2.0 * PI;  // これで x はほぼ [-π, π] に入る
  if (x >  PI/2.0) x =  PI - x;
  if (x < -PI/2.0) x = -PI - x;
  double x2 = x * x;
  return x * (1.0 + x2 * (-1.0/6.0 + x2 * (1.0/120.0)));
}

// A4基準の12-TET計算：半音差nに対して1.059463を反復乗除
double a4_ratio_pow(int n){
  static const double ratio[12] = {
    1.000000000000, 1.059463094359, 1.122462048309, 
    1.189207115002, 1.259921049895, 1.334839854170, 
    1.414213562373, 1.498307076876, 1.587401051968, 
    1.681792830507, 1.781797436281, 1.887748625363,
  };
  int oct = n / 12; // 何オクターブ分か
  int mod = n % 12; // 余り（負も考慮）
  if(mod < 0){ mod += 12; oct -= 1; }
  double v = ratio[mod];
  if(oct > 0){
     while(oct--) v *= 2.0;
  } else {
     while(oct++) v /= 2.0;
  }
  return v;
}

// ノートの周波数
double note_freq(int octave, int note_index, double baseA4){
  int semis=(octave-4)*12 + note_index - 9;
  return baseA4 * a4_ratio_pow(semis);
}

// WAVファイルの書き出し
void write_wav(const char *path, const int16_t *data, size_t nsamp){
  FILE *fp=fopen(path, "wb");
  if(!fp){
    fprintf(stderr, "[ERROR] %s: cannot write file.\n", path);
    exit(1);
  }
  uint32_t chunkSize = 36 + (uint32_t)(nsamp*2);
  uint16_t audioFormat=1, numChannels=1, bitsPerSample=16;
  uint32_t sampleRate=SR, byteRate=SR*numChannels*bitsPerSample/8;
  uint16_t blockAlign=numChannels*bitsPerSample/8;
  uint32_t sub1=16;
  uint32_t sub2=(uint32_t)(nsamp*2);
  fwrite("RIFF",1,4,fp); fwrite(&chunkSize,4,1,fp);
  fwrite("WAVE",1,4,fp);
  fwrite("fmt ",1,4,fp); fwrite(&sub1,4,1,fp);
  fwrite(&audioFormat,2,1,fp); fwrite(&numChannels,2,1,fp);
  fwrite(&sampleRate,4,1,fp); fwrite(&byteRate,4,1,fp);
  fwrite(&blockAlign,2,1,fp); fwrite(&bitsPerSample,2,1,fp);
  fwrite("data",1,4,fp); fwrite(&sub2,4,1,fp);
  fwrite(data,2,nsamp,fp);
  fclose(fp);
}

// MMLからWAVファイルを作成
void mml_to_wav(const char *MML, const char *WAV_PATH){
  int octave=4, defLen=4;
  double bpm=120.0;
  int volume=8;
  double baseA4=440.0;
  int waveform=1; // @0..6: 0=sin,1/2/3=pulse,4=triangle,5=saw,6=noise
  int gate8=7;    // Qコマンド（1..8）: 鳴る割合 gate8/8

  size_t cap=SR*120, n=0; // とりあえず120秒を確保
  int16_t *pcm=(int16_t*)calloc(cap,sizeof(int16_t));

  const char *p=MML;
  while(*p){
    // 空白スキップと `&` の読み飛ばし（スラーは未実装）
    if(*p==' '||*p=='\t'||*p=='\n'||*p=='&'){ p++; continue; }
    char c = UC(*p); // 大文字に揃える

    // パラメータ処理
    if(c=='T'){ p++; double v=read_number(&p); if(v>0) bpm=v; continue; }
    if(c=='O'){ p++; double v=read_number(&p); if(v>=0) octave=(int)v; continue; }
    if(c=='L'){ p++; double v=read_number(&p); if(v>0) defLen=(int)v; continue; }
    if(c=='V'){ p++; double v=read_number(&p); if(v>=0&&v<=15) volume=(int)v; else if(v>=0) volume=15; continue; }
    if(c=='P'){ p++; double v=read_number(&p); if(v>0) baseA4=v; continue; }
    if(c=='Q'){ p++; double v=read_number(&p); if(v>0) gate8=(int)v; continue; }
    if(c=='@'){ p++; double v=read_number(&p); if(v>=0) waveform=(int)v; if(waveform<0)waveform=0; if(waveform>6)waveform=6; continue; }
    if(c=='>'){ octave++; p++; continue; }
    if(c=='<'){ octave--; p++; continue; }

    // 音階処理
    int isRest=0; int idx=-1; int acc=0;
    if(c=='R'){ isRest=1; p++; } // 休符
    else {
      idx=note_index_from_char(c); // 音符
      if(idx>=0){
        p++;
        if(*p=='#'||*p=='+'){acc=+1;p++;} // シャープ
        else if(*p=='-'){acc=-1;p++;} // フラット
        idx += acc;
      }
    }

    // 発音の長さの処理
    if(isRest || idx>=0){
      int len=defLen; double x=read_number(&p); if(x>=0){ if(x>0) len=(int)x; }
      double dur=(len>0)? ((60.0/bpm)*(4.0/len)) : 0.0;
      // 付点（何個でも）: 元長さの1/2, 1/4, 1/8...を順に加算
      double extra = dur * 0.5;
      while(*p=='.'){ dur += extra; p++; extra *= 0.5; }
      // ^タイ：同じ音に長さだけ連結（休符には無視）
      if(!isRest){
        for(;;){
          while(*p==' '||*p=='\t'||*p=='\n'){ p++; }
          if(*p!='^') break;
          p++; // '^'
          int len2=defLen; double x2=read_number(&p); if(x2>=0){ if(x2>0) len2=(int)x2; }
          double dur2=(len2>0)? ((60.0/bpm)*(4.0/len2)) : 0.0;
          // 付点（何個でも）をタイ側にも適用
          double extra2 = dur2 * 0.5;
          while(*p=='.'){ dur2 += extra2; p++; extra2 *= 0.5; }
          dur += dur2;
        }
      }
      if(dur<=0.0){ continue; }

      // メモリが足りなくなったらrealloc
      size_t ns=(size_t)(dur*SR);
      if(n+ns>cap){
        size_t newcap=(n+ns)*2;
        int16_t *np=(int16_t*)realloc(pcm,newcap*sizeof(int16_t));
        if(!np){ break; }
        pcm=np; cap=newcap;
      }

      // 周波数・位相
      double f = (isRest)? 0.0 : note_freq(octave, (idx%12+12)%12, baseA4);
      double phase=0.0, inc=(f>0)?(f/SR):0.0;

      // デューティ比：@1=1:1(0.5), @2=1:3(0.25), @3=1:7(0.125)
      double duty = 0.5; if(waveform==2) duty=0.25; else if(waveform==3) duty=0.125;

      // 一定振幅（エンベロープなし）
      double Amax = (double)volume/16.0;
      
      // 音を鳴らす
      double gate_ratio = (gate8>=1 && gate8<=8) ? (gate8/8.0) : 1.0;
      size_t play_ns = (size_t)(ns * gate_ratio);
      if(play_ns > ns) play_ns = ns;
      for(size_t j=0;j<ns;j++){
        double v=0.0;
        if(f>0 && j<play_ns){
          if(phase>=1.0) phase-=1.0; if(phase<0.0) phase+=1.0;
          if(waveform==0){ v = sin5(PI*2.0*phase); }
          else if(waveform==1||waveform==2||waveform==3){ v = (phase<duty)?1.0:-1.0; }
          else if(waveform==4){ v = (phase<0.5)? (phase*4.0-1.0) : (-4.0*phase+3.0); }
          else if(waveform==5){ v = phase*2.0 - 1.0; }
          else if(waveform==6){ v = (rand()/(double)RAND_MAX)*2.0-1.0; }
          phase += inc;
        } else {
          v = 0.0;
        }
        // 符号付16bitの音にエンコード（一定振幅）
        double s = v * Amax;
        int iv = (int)(s * 32767.0);
        if(iv>32767)iv=32767; if(iv<-32768)iv=-32768;
        pcm[n+j] = (int16_t)iv;
      }
      n += ns;
      continue;
    }
    p++;
  }
  write_wav(WAV_PATH, pcm, n);
  free(pcm);
}
