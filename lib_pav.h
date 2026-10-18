// lib_pav.h - PAV video codec header-only library for C99.
// Input: 8-bit non-interlaced PNG sprite file
// Compression: KEY/DELTA + RLE/SKIP + Huffman
// Copyright (c) 2026 Yoshinori Takesako
// SPDX-License-Identifier: MIT OR BSD-3-Clause OR GPL-1.0-or-later

#ifndef LIB_PAV_H
#define LIB_PAV_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#define PAV_MAX_PIXEL 16000
#define PAV_MAX_BLOCK 32768

typedef struct {
    int width, height, fps, colors;
    uint32_t frame_count, frame_index;
    unsigned char palette[256][3];
    unsigned char pixel[PAV_MAX_PIXEL];
    FILE *_file;
    unsigned char _data[PAV_MAX_BLOCK], _tokens[PAV_MAX_BLOCK];
    int _pos, _len, _frames_left, _have_previous, _failed;
} PavDecoder;

// Encode from PNG sprite sheet to PAV. Return 1 on success, 0 on failure.
static inline int pav_encode_png(const char *png_path, const char *pav_path, int frame_width, int frame_height, int colors, int fps);

// Open the PAV file. Return NULL on error.
static inline PavDecoder *pav_decoder_open(const char *path);

// Read one frame into decoder->pixel. Return 1 for a frame, 0 at end, -1 on error.
static inline int pav_decoder_next(PavDecoder *decoder);

// Close the PAV file and free the decoder. NULL is allowed.
static inline void pav_decoder_close(PavDecoder *decoder);

// PNG decoder.
typedef struct { const unsigned char *p; size_t n, pos; unsigned bits, count; int err; } _pav_png_bits;
typedef struct { unsigned short count[16], symbol[320]; } _pav_png_huff;
static inline unsigned _pav_png_be(const unsigned char *p) { return (unsigned)p[0]<<24 | (unsigned)p[1]<<16 | (unsigned)p[2]<<8 | p[3]; }
static inline unsigned _pav_png_get(_pav_png_bits *b, unsigned n) {
    while (b->count<n) {
        if (b->pos>=b->n) { b->err=1; return 0; }
        b->bits |= (unsigned)b->p[b->pos++] << b->count;
        b->count+=8;
    }
    unsigned v=b->bits & ((1u<<n)-1);
    b->bits >>= n; b->count-=n;
    return v;
}
static inline int _pav_png_tree(_pav_png_huff *h, const unsigned char *length, int n) {
    unsigned short offs[16]; int left=1;
    memset(h,0,sizeof(*h));
    for (int i=0;i<n;i++) { if (length[i]>15) return 0; h->count[length[i]]++; }
    h->count[0]=0;
    for (int i=1;i<=15;i++) { left=left*2-h->count[i]; if (left<0) return 0; }
    offs[1]=0;
    for (int i=1;i<15;i++) offs[i+1]=offs[i]+h->count[i];
    for (int i=0;i<n;i++) if (length[i]) h->symbol[offs[length[i]]++]=i;
    return 1;
}
static inline int _pav_png_symbol(_pav_png_bits *b, const _pav_png_huff *h) {
    unsigned code=0, first=0, index=0;
    for (int len=1;len<=15;len++) {
        code |= _pav_png_get(b,1);
        if (b->err) return -1;
        unsigned count=h->count[len];
        if (code-first<count) return h->symbol[index+code-first];
        index+=count; first=(first+count)*2; code*=2;
    }
    b->err=1; return -1;
}
static inline int _pav_png_inflate(const unsigned char *src, size_t size, unsigned char *out, size_t cap) {
    static const int length_base[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const int length_extra[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const int dist_base[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const int dist_extra[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    static const int order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    if (size<6 || (src[0]&15)!=8 || (src[0]>>4)>7 || ((src[0]<<8)|src[1])%31 || (src[1]&32)) return 0;
    _pav_png_bits b={src+2,size-6,0,0,0,0};
    size_t used=0;
    int last=0;
    do {
        last=_pav_png_get(&b,1); int type=_pav_png_get(&b,2);
        if (type==0) {
            unsigned skip=b.count%8; _pav_png_get(&b,skip);
            unsigned len=_pav_png_get(&b,16), inv=_pav_png_get(&b,16);
            if ((len^inv)!=65535 || len>cap-used || b.err) return 0;
            for (unsigned i=0;i<len;i++) out[used++]=_pav_png_get(&b,8);
        } else if (type==1 || type==2) {
            unsigned char lens[320]={0}, dlens[32]={0};
            int nl=288, nd=32;
            if (type==1) {
                for (int i=0;i<144;i++) lens[i]=8;
                for (int i=144;i<256;i++) lens[i]=9;
                for (int i=256;i<280;i++) lens[i]=7;
                for (int i=280;i<288;i++) lens[i]=8;
                for (int i=0;i<32;i++) dlens[i]=5;
            } else {
                nl=257+_pav_png_get(&b,5); nd=1+_pav_png_get(&b,5);
                int nc=4+_pav_png_get(&b,4);
                if (nl>286 || nd>32) return 0;
                unsigned char cl[19]={0}, all[320]={0};
                for (int i=0;i<nc;i++) cl[order[i]]=_pav_png_get(&b,3);
                _pav_png_huff ch;
                if (!_pav_png_tree(&ch,cl,19)) return 0;
                for (int i=0;i<nl+nd;) {
                    int s=_pav_png_symbol(&b,&ch), repeat=1, value=s;
                    if (s<0) return 0;
                    if (s==16) { if (!i) return 0; value=all[i-1]; repeat=3+_pav_png_get(&b,2); }
                    if (s==17) { value=0; repeat=3+_pav_png_get(&b,3); }
                    if (s==18) { value=0; repeat=11+_pav_png_get(&b,7); }
                    if (i+repeat>nl+nd) return 0;
                    while (repeat--) all[i++]=value;
                }
                memcpy(lens,all,nl); memcpy(dlens,all+nl,nd);
            }
            _pav_png_huff lh,dh;
            if (!lens[256] || !_pav_png_tree(&lh,lens,nl) || !_pav_png_tree(&dh,dlens,nd)) return 0;
            for (;;) {
                int s=_pav_png_symbol(&b,&lh);
                if (s<0) return 0;
                if (s<256) { if (used>=cap) return 0; out[used++]=s; }
                else if (s==256) break;
                else {
                    if (s>285) return 0;
                    int k=s-257, len=length_base[k]+_pav_png_get(&b,length_extra[k]);
                    int d=_pav_png_symbol(&b,&dh);
                    if (d<0 || d>=30) return 0;
                    int dist=dist_base[d]+_pav_png_get(&b,dist_extra[d]);
                    if ((size_t)dist>used || (size_t)len>cap-used) return 0;
                    while (len--) { out[used]=out[used-dist]; used++; }
                }
            }
        } else return 0;
        if (b.err) return 0;
    } while (!last);
    unsigned a=1,c=0; // Check Adler-32
    for (size_t i=0;i<used;i++) { a=(a+out[i])%65521; c=(c+a)%65521; }
    return used==cap && ((c<<16)|a)==_pav_png_be(src+size-4);
}
static inline int _pav_png_paeth(int a,int b,int c) {
    int p=a+b-c, x=abs(p-a),y=abs(p-b),z=abs(p-c);
    return x<=y && x<=z ? a : y<=z ? b : c;
}
static inline unsigned char *_pav_png_decode(const unsigned char *file,size_t n,int *w,int *h) {
    static const unsigned char sig[8]={137,80,78,71,13,10,26,10};
    if (n<33 || memcmp(file,sig,8)) return NULL;
    unsigned width=0,height=0,channels=0,kind=0,seen=0;
    unsigned char palette[256][3]={{0}},alpha[256];
    for (int i=0;i<256;i++) alpha[i]=255;
    unsigned char *z=NULL,*raw=NULL,*rgb=NULL; size_t zn=0;
    for (size_t pos=8;pos+12<=n;) {
        unsigned len=_pav_png_be(file+pos); pos+=4;
        if ((size_t)len>n-pos-8) goto fail;
        const unsigned char *tag=file+pos,*data=file+pos+4; pos+=len+8;
        if (!memcmp(tag,"IHDR",4)) {
            if (len!=13 || seen) goto fail;
            width=_pav_png_be(data); height=_pav_png_be(data+4); kind=data[9];
            if (!width || !height || width>16384 || height>16384 || data[8]!=8 || data[10] || data[11] || data[12]) goto fail;
            channels=kind==0?1:kind==2?3:kind==3?1:kind==4?2:kind==6?4:0;
            if (!channels) goto fail;
            seen=1;
        } else if (!memcmp(tag,"PLTE",4)) {
            if (!seen || len>768 || len%3) goto fail;
            for (unsigned i=0;i<len/3;i++) memcpy(palette[i],data+i*3,3);
        } else if (!memcmp(tag,"tRNS",4)) {
            if (kind==3) { if (len>256) goto fail; memcpy(alpha,data,len); }
            // Grayscale/RGB color-key transparency is not supported.
        } else if (!memcmp(tag,"IDAT",4)) {
            if (!seen || (size_t)len>SIZE_MAX-zn) goto fail;
            unsigned char *tmp=realloc(z,zn+len+(zn+len==0));
            if (!tmp) goto fail;
            z=tmp; memcpy(z+zn,data,len); zn+=len;
        } else if (!memcmp(tag,"IEND",4)) break;
        else if (!(tag[0]&32)) goto fail; // Unknown required PNG chunk.
    }
    if (!seen || !zn || (size_t)width>SIZE_MAX/channels || (size_t)width>SIZE_MAX/3/height) goto fail;
    size_t stride=(size_t)width*channels;
    if (stride==SIZE_MAX || (size_t)height>SIZE_MAX/(stride+1)) goto fail;
    size_t total=(stride+1)*(size_t)height;
    if (stride==SIZE_MAX || total/(stride+1)!=height) goto fail;
    raw=malloc(total); rgb=malloc((size_t)width*height*3);
    if (!raw || !rgb || !_pav_png_inflate(z,zn,raw,total)) goto fail;
    for (unsigned y=0;y<height;y++) {
        unsigned char *line=raw+y*(stride+1)+1;
        unsigned char *prev=y?raw+(y-1)*(stride+1)+1:NULL;
        int filter=raw[y*(stride+1)];
        if (filter>4) goto fail;
        for (size_t x=0;x<stride;x++) {
            int a=x>=channels?line[x-channels]:0, b=prev?prev[x]:0;
            int c=prev && x>=channels?prev[x-channels]:0;
            int v=filter==1?a:filter==2?b:filter==3?(a+b)/2:filter==4?_pav_png_paeth(a,b,c):0;
            line[x]=(unsigned char)(line[x]+v);
        }
        for (unsigned x=0;x<width;x++) {
            const unsigned char *p=line+(size_t)x*channels;
            unsigned char *q=rgb+((size_t)y*width+x)*3;
            if (kind==0 || kind==4) q[0]=q[1]=q[2]=p[0];
            if (kind==2 || kind==6) memcpy(q,p,3);
            if (kind==3) memcpy(q,palette[p[0]],3);
            // Blend transparent pixel on white.
            unsigned a=kind==6?p[3]:kind==4?p[1]:kind==3?alpha[p[0]]:255;
            if (a!=255) for (int c=0;c<3;c++) q[c]=(q[c]*a+255*(255-a))/255;
        }
    }
    *w=(int)width; *h=(int)height;
    free(z); free(raw); return rgb;
fail:
    free(z); free(raw); free(rgb); return NULL;
}
static inline unsigned char *_pav_png_load(const char *filename,int *width,int *height) {
    FILE *f=fopen(filename,"rb"); if (!f) return NULL;
    if (fseek(f,0,SEEK_END)) { fclose(f); return NULL; }
    long n=ftell(f);
    if (n<0 || fseek(f,0,SEEK_SET)) { fclose(f); return NULL; }
    unsigned char *data=malloc(n?n:1); if (!data) { fclose(f); return NULL; }
    if (fread(data,1,n,f)!=(size_t)n) { free(data); fclose(f); return NULL; }
    fclose(f);
    unsigned char *rgb=_pav_png_decode(data,n,width,height);
    free(data); return rgb;
}

// Build one palette for all frames.
typedef struct { uint32_t count; uint64_t r,g,b; } _pav_hist;
typedef struct { unsigned char r,g,b; uint32_t w; } _pav_cell;
typedef struct { int start,end,axis; uint64_t weight,score; } _pav_box;
typedef struct {
    _pav_hist hist[65536];
    _pav_cell cell[65536];
    _pav_box boxes[256];
    unsigned char palette[256][3], rgb_map[65536];
} _pav_quant;
static inline unsigned _pav_bucket(unsigned r,unsigned g,unsigned b) {
    return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);
}
static inline int _pav_cell_axis(const _pav_cell *a, int axis) {
    return axis==0?a->r:axis==1?a->g:a->b;
}
static inline void _pav_sort_cells(_pav_cell *a, int lo, int hi, int axis) {
    while (lo<hi) {
        int i=lo, j=hi, mid=_pav_cell_axis(a+(lo+hi)/2,axis);
        while(i<=j) {
            while(_pav_cell_axis(a+i,axis)<mid)i++;
            while(_pav_cell_axis(a+j,axis)>mid)j--;
            if(i<=j) { _pav_cell temp=a[i];a[i]=a[j];a[j]=temp;i++;j--; }
        }
        if(j-lo<hi-i) { if(lo<j)_pav_sort_cells(a,lo,j,axis);lo=i; }
        else { if(i<hi)_pav_sort_cells(a,i,hi,axis);hi=j; }
    }
}
static inline void _pav_box_score(_pav_quant *ctx, _pav_box *q) {
    int lo[3]={255,255,255},hi[3]={0,0,0}; q->weight=0;
    for(int i=q->start;i<q->end;i++) {
        _pav_cell *c=ctx->cell+i; int v[3]={c->r,c->g,c->b};
        for(int k=0;k<3;k++) {if(v[k]<lo[k])lo[k]=v[k];if(v[k]>hi[k])hi[k]=v[k];}
        q->weight+=c->w;
    }
    int range[3]={3*(hi[0]-lo[0])*(hi[0]-lo[0]),
                  4*(hi[1]-lo[1])*(hi[1]-lo[1]),
                  2*(hi[2]-lo[2])*(hi[2]-lo[2])};
    q->axis=range[1]>range[0]?1:0;
    if(range[2]>range[q->axis])q->axis=2;
    q->score=(q->end-q->start<2)?0:q->weight*(uint64_t)range[q->axis];
}
static inline int _pav_nearest_rgb(_pav_quant *ctx,int r,int g,int b,int n) {
    int best=0,score=INT_MAX;
    for(int i=0;i<n;i++) {
        int dr=r-ctx->palette[i][0],dg=g-ctx->palette[i][1],db=b-ctx->palette[i][2];
        int d=3*dr*dr+4*dg*dg+2*db*db;
        if(d<score) {score=d;best=i;}
    }
    return best;
}
static inline int _pav_quantize(_pav_quant *ctx,const unsigned char *rgb,size_t pixel,int n) {
    int active=0;
    memset(ctx->hist,0,sizeof(ctx->hist));
    for(size_t i=0;i<pixel;i++) {
        const unsigned char *p=rgb+i*3;
        _pav_hist *h=ctx->hist+_pav_bucket(p[0],p[1],p[2]);
        h->count++; h->r+=p[0];h->g+=p[1];h->b+=p[2];
    }
    for(int k=0;k<65536;k++) if(ctx->hist[k].count) {
        _pav_hist *h=ctx->hist+k;_pav_cell *c=ctx->cell+active++;
        c->r=(unsigned char)(h->r/h->count);
        c->g=(unsigned char)(h->g/h->count);
        c->b=(unsigned char)(h->b/h->count);
        c->w=h->count>4096?4096:h->count;
    }
    if(!active)return 0;
    if(n>active)n=active;
    ctx->boxes[0]=(_pav_box){.start=0,.end=active}; _pav_box_score(ctx,ctx->boxes);
    for(int k=1;k<n;k++) {
        int best=-1;
        for(int j=0;j<k;j++)if(ctx->boxes[j].score && (best<0 || ctx->boxes[j].score>ctx->boxes[best].score))best=j;
        if(best<0){n=k;break;}
        _pav_box *q=ctx->boxes+best;_pav_sort_cells(ctx->cell,q->start,q->end-1,q->axis);
        uint64_t total=0;int mid=q->start+1;
        while(mid<q->end-1) {
            total+=ctx->cell[mid-1].w;
            if(total>=q->weight/2)break;
            mid++;
        }
        ctx->boxes[k]=(_pav_box){.start=mid,.end=q->end};q->end=mid;
        _pav_box_score(ctx,q);_pav_box_score(ctx,ctx->boxes+k);
    }
    for(int k=0;k<n;k++) {
        uint64_t sum[3]={0},weight=0;
        _pav_box *q=ctx->boxes+k;
        for(int i=q->start;i<q->end;i++) {
            _pav_cell *c=ctx->cell+i; weight+=c->w;
            sum[0]+=(uint64_t)c->w*c->r;
            sum[1]+=(uint64_t)c->w*c->g;
            sum[2]+=(uint64_t)c->w*c->b;
        }
        for(int j=0;j<3;j++)ctx->palette[k][j]=(unsigned char)((sum[j]+weight/2)/weight);
    }
    for(int pass=0;pass<8;pass++) {
        uint64_t sum[256][4]={{0}};
        for(int i=0;i<active;i++){
            _pav_cell *c=ctx->cell+i;int p=_pav_nearest_rgb(ctx,c->r,c->g,c->b,n);
            sum[p][0]+=(uint64_t)c->w*c->r;
            sum[p][1]+=(uint64_t)c->w*c->g;
            sum[p][2]+=(uint64_t)c->w*c->b;sum[p][3]+=c->w;
        }
        int changed=0;
        for(int k=0;k<n;k++)if(sum[k][3]) for(int j=0;j<3;j++){
            unsigned char v=(unsigned char)((sum[k][j]+sum[k][3]/2)/sum[k][3]);
            if(ctx->palette[k][j]!=v)changed=1;
            ctx->palette[k][j]=v;
        }
        if(!changed)break;
    }
    for(int k=0;k<65536;k++) {
        _pav_hist *h=ctx->hist+k;
        int r=h->count?(int)(h->r/h->count):((k>>11)*8+4);
        int g=h->count?(int)(h->g/h->count):(((k>>5)&63)*4+2);
        int b=h->count?(int)(h->b/h->count):((k&31)*8+4);
        ctx->rgb_map[k]=(unsigned char)_pav_nearest_rgb(ctx,r,g,b,n);
    }
    return n;
}

// Encode frame tokens and Huffman data.
typedef struct {
    unsigned char frame[PAV_MAX_PIXEL], prev[PAV_MAX_PIXEL];
    unsigned char tok[PAV_MAX_BLOCK], key[2*PAV_MAX_PIXEL+256], delta[2*PAV_MAX_PIXEL+256];
    unsigned char packed[PAV_MAX_BLOCK+512];
    int P, C, frames_in_block, tokens_in_block, have_prev;
} _pav_encoder;
static inline void _pav_w16(FILE *f, unsigned x) { fputc(x&255,f); fputc(x>>8,f); }
static inline void _pav_w32(FILE *f, uint32_t x) { _pav_w16(f,x&65535); _pav_w16(f,x>>16); }
// 0..127: literal, 128..191: RLE, 192..255: SKIP.
static inline int _pav_frame_tokens(_pav_encoder *ctx,unsigned char *out, int bg, int is_delta) {
    int i=0,j=0,n,r,start;
    out[j++]=is_delta?1:0;
    if(!is_delta)out[j++]=(unsigned char)bg;
    while(i<ctx->P){
        n=0;
        while(i+n<ctx->P && n<64 && ctx->frame[i+n]==(is_delta?ctx->prev[i+n]:bg))n++;
        if(n>=2){out[j++]=0xc0+n-1;i+=n;continue;}
        r=0;while(i+r<ctx->P && r<66 && ctx->frame[i+r]==ctx->frame[i])r++;
        if(r>=3){out[j++]=0x80+r-3;out[j++]=ctx->frame[i];i+=r;continue;}
        start=i++;
        while(i<ctx->P && i-start<128){
            n=0;while(i+n<ctx->P && n<2 && ctx->frame[i+n]==(is_delta?ctx->prev[i+n]:bg))n++;
            r=0;while(i+r<ctx->P && r<3 && ctx->frame[i+r]==ctx->frame[i])r++;
            if(n>=2||r>=3)break;
            i++;
        }
        out[j++]=i-start-1;
        memcpy(out+j,ctx->frame+start,(size_t)(i-start)); j+=i-start;
    }
    return j;
}

// Canonical Huffman. Use raw tokens when codes exceed 15 bits.
static inline int _pav_make_huff(const unsigned char *s,int n,unsigned char *lens,
                     unsigned short *code,int *nbytes){
    unsigned long wt[512]={0};
    int left[512],right[512],used[512]={0},count[16]={0},next[16]={0};
    int nodes=256,i,a,b,depth,stack[512],dep[512],top=0;
    unsigned long sum, bits=0;
    memset(lens,0,256);
    for(i=0;i<n;i++)wt[s[i]]++;
    for(;;){
        a=b=-1;
        for(i=0;i<nodes;i++)if(!used[i]&&wt[i]){
            if(a<0||wt[i]<wt[a]){b=a;a=i;}
            else if(b<0||wt[i]<wt[b])b=i;
        }
        if(b<0)break;
        wt[nodes]=wt[a]+wt[b];left[nodes]=a;right[nodes]=b;
        used[a]=used[b]=1;nodes++;
    }
    if(a<0)return 0;
    stack[top]=a;dep[top++]=0;
    while(top){
        --top;i=stack[top];depth=dep[top];
        if(i<256){if(depth>15)return 0;lens[i]=depth?depth:1;}
        else{stack[top]=left[i];dep[top++]=depth+1;
             stack[top]=right[i];dep[top++]=depth+1;}
    }
    for(i=0;i<256;i++)if(lens[i]){count[lens[i]]++;bits+=wt[i]*lens[i];}
    *nbytes=(int)((bits+7)/8);
    sum=0;
    for(i=1;i<=15;i++){sum=(sum+count[i-1])*2;next[i]=(int)sum;}
    for(i=0;i<256;i++)code[i]=lens[i]?(unsigned short)next[lens[i]]++:0;
    return 1;
}
static inline int _pav_huff_compress(const unsigned char *src,int n,unsigned char *dst,
                         unsigned char *lens,const unsigned short *code){
    uint32_t v=0;int bits=0,j=128,i,k;
    memset(dst,0,128);
    for(i=0;i<256;i++){
        if(i&1)dst[i>>1]|=lens[i]<<4;
        else dst[i>>1]=lens[i];
    }
    for(i=0;i<n;i++){
        k=lens[src[i]];
        v=(v<<k)|code[src[i]];bits+=k;
        while(bits>=8){bits-=8;dst[j++]=(unsigned char)(v>>bits);}
        v&=(1u<<bits)-1u;
    }
    if(bits)dst[j++]=(unsigned char)(v<<(8-bits));
    return j;
}
static inline int _pav_flush(_pav_encoder *ctx,FILE *out,int count,int toklen,int *raw,int *huff){
    unsigned char lens[256];unsigned short code[256];
    int n,method=0,bytes=toklen;
    const unsigned char *data=ctx->tok;
    if(!count)return 1;
    if(_pav_make_huff(ctx->tok,toklen,lens,code,&n) && n+128<bytes){
        bytes=_pav_huff_compress(ctx->tok,toklen,ctx->packed,lens,code);
        method=1;data=ctx->packed;
    }
    fputc(count,out);fputc(method,out);
    _pav_w16(out,toklen);_pav_w16(out,bytes);
    if(fwrite(data,1,bytes,out)!=(size_t)bytes)return 0;
    if(method)(*huff)++;
    else (*raw)++;
    return !ferror(out);
}

// Write frames and PAV file header.
static inline int _pav_write_header(FILE *f, int w,int h,int colors,int fps,int nframes,
                                      const unsigned char *palette) {
    if(fwrite("PAV\001",1,4,f)!=4)return 0;
    _pav_w16(f,(unsigned)w);_pav_w16(f,(unsigned)h);
    _pav_w16(f,(unsigned)fps);_pav_w16(f,(unsigned)colors);
    _pav_w32(f,(uint32_t)nframes);_pav_w32(f,0);
    return fwrite(palette,1,(size_t)colors*3,f)==(size_t)colors*3 && !ferror(f);
}
static inline int _pav_append_frame(_pav_encoder *e,FILE *f) {
    int hist[256]={0},bg=0,keylen,deltalen,j,raw=0,huff=0;
    for(j=0;j<e->P;j++){
        if(e->frame[j]>=e->C)return 0;
        hist[e->frame[j]]++;
    }
    for(j=1;j<e->C;j++)if(hist[j]>hist[bg])bg=j;
    keylen=_pav_frame_tokens(e,e->key,bg,0);
    deltalen=e->have_prev?_pav_frame_tokens(e,e->delta,bg,1):INT_MAX;
    if(deltalen<keylen){keylen=deltalen;memcpy(e->key,e->delta,(size_t)keylen);}
    if(e->tokens_in_block+keylen>PAV_MAX_BLOCK || e->frames_in_block==255){
        if(!_pav_flush(e,f,e->frames_in_block,e->tokens_in_block,&raw,&huff))return 0;
        e->tokens_in_block=e->frames_in_block=0;
    }
    if(keylen>PAV_MAX_BLOCK)return 0;
    memcpy(e->tok+e->tokens_in_block,e->key,(size_t)keylen);
    e->tokens_in_block+=keylen;e->frames_in_block++;
    memcpy(e->prev,e->frame,(size_t)e->P);e->have_prev=1;
    return 1;
}
static inline int _pav_finish(_pav_encoder *e,FILE *out) {
    int raw=0,huff=0;
    return _pav_flush(e,out,e->frames_in_block,e->tokens_in_block,&raw,&huff) && !ferror(out);
}
static inline int _pav_dimensions(int w,int h,int colors) {
    return w>0 && h>0 && w<=65535 && h<=65535 &&
           (uint64_t)w*h<=PAV_MAX_PIXEL && colors>=1 && colors<=256;
}

// Encode from PNG sprite sheet.
static inline int pav_encode_png(const char *png_path,const char *pav_path,
                                  int frame_width,int frame_height,int colors,int fps) {
    int iw=0,ih=0,cols=0,nframes=0,ok=0,output_created=0;
    unsigned char *image=NULL;
    _pav_quant *q=NULL;
    _pav_encoder *e=NULL;
    FILE *out=NULL;
    if(!png_path||!pav_path||!_pav_dimensions(frame_width,frame_height,colors)||fps<1||fps>65535)return 0;
    image=_pav_png_load(png_path,&iw,&ih);
    if(!image)goto cleanup;
    if(iw%frame_width||ih%frame_height)goto cleanup;
    cols=iw/frame_width;
    if(!cols || (uint64_t)cols*(ih/frame_height)>INT_MAX)goto cleanup;
    nframes=cols*(ih/frame_height);
    q=(_pav_quant*)calloc(1,sizeof(*q));
    e=(_pav_encoder*)calloc(1,sizeof(*e));
    if(!q||!e)goto cleanup;
    colors=_pav_quantize(q,image,(size_t)iw*ih,colors);
    if(!colors)goto cleanup;
    e->P=frame_width*frame_height;e->C=colors;
    out=fopen(pav_path,"wb");
    if(!out)goto cleanup;
    output_created=1;
    if(!_pav_write_header(out,frame_width,frame_height,colors,fps,nframes,&q->palette[0][0]))goto cleanup;
    for(int fi=0;fi<nframes;fi++) {
        int x=fi%cols*frame_width,y=fi/cols*frame_height;
        for(int row=0;row<frame_height;row++)for(int col=0;col<frame_width;col++) {
            const unsigned char *p=image+3*((size_t)(y+row)*iw+x+col);
            e->frame[row*frame_width+col]=q->rgb_map[_pav_bucket(p[0],p[1],p[2])];
        }
        if(!_pav_append_frame(e,out))goto cleanup;
    }
    if(!_pav_finish(e,out))goto cleanup;
    ok=1;
cleanup:
    if(out && fclose(out))ok=0;
    if(!ok && output_created)remove(pav_path);
    free(image);free(q);free(e);
    return ok;
}
static inline unsigned _pav_le16(const unsigned char *p) {
    return (unsigned)p[0]|(unsigned)p[1]<<8;
}
static inline uint32_t _pav_le32(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline int _pav_huff_decode(const unsigned char *src,int slen,
                                    unsigned char *dst,int len) {
    int counts[16]={0},first[16]={0},base[16]={0},next[16],symbol[256];
    int i,k,l,j=0,bit=0,code,idx,pos=128;
    if(slen<128)return 0;
    for(i=0;i<256;i++)counts[(src[i>>1]>>(i&1?4:0))&15]++;
    counts[0]=0;
    for(l=1;l<=15;l++){
        if(l>1){first[l]=(first[l-1]+counts[l-1])*2;
                base[l]=base[l-1]+counts[l-1];}
        if(first[l]+counts[l]>(1<<l))return 0;
        next[l]=base[l];
    }
    for(i=0;i<256;i++){
        k=(src[i>>1]>>(i&1?4:0))&15;
        if(k)symbol[next[k]++]=i;
    }
    for(j=0;j<len;j++){
        code=0;
        for(l=1;l<=15;l++){
            if(pos>=slen)return 0;
            code=(code<<1)|((src[pos]>>(7-bit))&1);
            if(++bit==8){bit=0;pos++;}
            idx=code-first[l];
            if(idx>=0&&idx<counts[l]){
                dst[j]=(unsigned char)symbol[base[l]+idx];break;
            }
        }
        if(l>15)return 0;
    }
    if(pos+(bit?1:0)!=slen)return 0;
    if(bit&&(src[pos]&((1<<(8-bit))-1)))return 0;
    return 1;
}
static inline int _pav_load_block(PavDecoder *d) {
    unsigned char b[6];
    int method,rawlen,packedlen,count;
    if(d->_frames_left || d->_pos!=d->_len)return 0;
    if(fread(b,1,6,d->_file)!=6)return 0;
    count=b[0];method=b[1];rawlen=(int)_pav_le16(b+2);
    packedlen=(int)_pav_le16(b+4);
    if(count<1||count>255||(unsigned)count>d->frame_count-d->frame_index ||
       method>1||rawlen<1||rawlen>PAV_MAX_BLOCK||packedlen<1||
       packedlen>PAV_MAX_BLOCK||(method==1&&packedlen<128)||
       (method==0&&packedlen!=rawlen))return 0;
    if(fread(d->_data,1,(size_t)packedlen,d->_file)!=(size_t)packedlen)return 0;
    if(method==0)memcpy(d->_tokens,d->_data,(size_t)rawlen);
    else if(!_pav_huff_decode(d->_data,packedlen,d->_tokens,rawlen))return 0;
    d->_frames_left=count;d->_pos=0;d->_len=rawlen;
    return 1;
}
static inline int _pav_decode_frame(PavDecoder *d) {
    int i=d->_pos,pos=0,kind,bg,t,n,j,c,size=d->width*d->height;
    if(i>=d->_len)return 0;
    kind=d->_tokens[i++];
    if(kind==0) {
        if(i>=d->_len)return 0;
        bg=d->_tokens[i++];
        if(bg>=d->colors)return 0;
        memset(d->pixel,bg,(size_t)size);
    } else if(kind!=1||!d->_have_previous)return 0;
    while(pos<size){
        if(i>=d->_len)return 0;
        t=d->_tokens[i++];
        if(t<128){
            n=t+1;
            if(n>size-pos || n>d->_len-i)return 0;
            for(j=0;j<n;j++)if(d->_tokens[i+j]>=d->colors)return 0;
            memcpy(d->pixel+pos,d->_tokens+i,(size_t)n);pos+=n;i+=n;
        } else if(t<192){
            n=(t&63)+3;
            if(i>=d->_len||n>size-pos)return 0;
            c=d->_tokens[i++];if(c>=d->colors)return 0;
            memset(d->pixel+pos,c,(size_t)n);pos+=n;
        } else {
            n=(t&63)+1;
            if(n>size-pos)return 0;
            pos+=n;
        }
    }
    d->_pos=i;
    d->_frames_left--;
    if(!d->_frames_left && d->_pos!=d->_len)return 0;
    d->_have_previous=1;
    d->frame_index++;
    return 1;
}

// Decode frames.
static inline PavDecoder *pav_decoder_open(const char *path) {
    PavDecoder *d;
    unsigned char hdr[20];
    if(!path)return NULL;
    d=(PavDecoder*)calloc(1,sizeof(*d));
    if(!d)return NULL;
    d->_file=fopen(path,"rb");
    if(!d->_file||fread(hdr,1,20,d->_file)!=20||memcmp(hdr,"PAV\001",4))goto fail;
    d->width=(int)_pav_le16(hdr+4);
    d->height=(int)_pav_le16(hdr+6);
    d->fps=(int)_pav_le16(hdr+8);
    d->colors=(int)_pav_le16(hdr+10);
    d->frame_count=_pav_le32(hdr+12);
    if(!_pav_dimensions(d->width,d->height,d->colors)||!d->fps||
       !d->frame_count||_pav_le32(hdr+16))goto fail;
    if(fread(d->palette,3,(size_t)d->colors,d->_file)!=(size_t)d->colors)goto fail;
    return d;
fail:
    if(d->_file)fclose(d->_file);
    free(d);
    return NULL;
}
static inline int pav_decoder_next(PavDecoder *d) {
    if(!d||d->_failed)return -1;
    if(d->frame_index==d->frame_count){
        if(d->_frames_left || d->_pos!=d->_len || fgetc(d->_file)!=EOF || ferror(d->_file)){
            d->_failed=1;return -1;
        }
        return 0;
    }
    if(!d->_frames_left && !_pav_load_block(d)){d->_failed=1;return -1;}
    if(!_pav_decode_frame(d)){d->_failed=1;return -1;}
    return 1;
}
static inline void pav_decoder_close(PavDecoder *d) {
    if(d){if(d->_file)fclose(d->_file);free(d);}
}
#endif
