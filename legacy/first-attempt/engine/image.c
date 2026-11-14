/* Texture file decoders (see image.h). The stock game and the installed mods use only TGA (types 1, 2, 3, 10, 11; 8, 16, 24 and
   32 bits; both row orders). BMP and DDS are for drop-in community ships. DDS: the top mip level of DXT1, DXT3, DXT5 or uncompressed
   RGB(A) with bit masks; DX10 headers and cube maps are not read. Compressed textures are decoded on the CPU (approximation: no BC
   upload to the GPU yet). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"

static unsigned rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned char *fail(char *err, int n, const char *m) { if (err && n) snprintf(err, n, "%s", m); return NULL; }

/* ------------------------------------------------ TGA ------------------------------------------------ */
static void tga_px(const unsigned char *s, int bpp, int gray, unsigned char *o)
{
    unsigned v;
    switch (bpp) {
    case 8:  o[0] = o[1] = o[2] = s[0]; o[3] = 255; (void)gray; break;
    case 15: case 16: v = rd16(s); o[0] = (unsigned char)(((v >> 10) & 31) * 255 / 31); o[1] = (unsigned char)(((v >> 5) & 31) * 255 / 31);
        o[2] = (unsigned char)((v & 31) * 255 / 31); o[3] = 255; break;                   /* the attribute bit is not an alpha in these files */
    case 24: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255; break;
    default: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3]; break;
    }
}
static unsigned char *tga(const unsigned char *b, long len, int *pw, int *ph, char *err, int en)
{
    int type, w, h, bpp, top, cmt, cmfirst, cmlen, cmbits, pb, cb = 0, i, x, y, bad = 0; long o; unsigned char *out, *cmap = NULL, px[4];
    if (len < 18) return fail(err, en, "short TGA header");
    cmt = b[1]; type = b[2]; cmfirst = (int)rd16(b + 3); cmlen = (int)rd16(b + 5); cmbits = b[7];
    w = (int)rd16(b + 12); h = (int)rd16(b + 14); bpp = b[16]; top = (b[17] & 0x20) != 0;
    if (type != 1 && type != 2 && type != 3 && type != 9 && type != 10 && type != 11) return fail(err, en, "unsupported TGA type");
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return fail(err, en, "bad TGA size");
    if (bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return fail(err, en, "unsupported TGA depth");
    pb = (bpp + 7) / 8;
    o = 18 + b[0];
    if (cmt) {                                                     /* colour map, read as RGBA */
        cb = (cmbits + 7) / 8;
        if (cb < 2 || cb > 4 || o + (long)cmlen * cb > len) return fail(err, en, "bad TGA colour map");
        cmap = malloc((size_t)(cmlen ? cmlen : 1) * 4);
        for (i = 0; i < cmlen; i++) tga_px(b + o + (long)i * cb, cmbits == 15 ? 16 : cmbits, 0, cmap + 4 * i);
        o += (long)cmlen * cb;
    }
    if ((type == 1 || type == 9) && (!cmap || pb != 1)) { free(cmap); return fail(err, en, "TGA colour-mapped without a map"); }
    out = malloc((size_t)w * h * 4);
    for (i = 0; i < w * h; ) {
        int run = 1, raw = 1;
        if (type >= 9) { if (o >= len) { bad = 1; break; } run = (b[o] & 0x7f) + 1; raw = !(b[o] & 0x80); o++; }
        else run = w * h;
        for (x = 0; x < run && i < w * h; x++, i++) {
            const unsigned char *s;
            if (raw || x == 0) { if (o + pb > len) { bad = 1; break; } s = b + o; o += pb;
                if (type == 1 || type == 9) { int k = s[0] - cmfirst; if (k < 0 || k >= cmlen) k = 0; memcpy(px, cmap + 4 * k, 4); }
                else tga_px(s, bpp, type == 3 || type == 11, px); }
            y = top ? i / w : h - 1 - i / w;
            memcpy(out + 4 * ((long)y * w + i % w), px, 4);
        }
        if (bad) break;
    }
    free(cmap);
    if (bad || i < w * h) { free(out); return fail(err, en, "truncated TGA data"); }
    *pw = w; *ph = h; return out;
}

/* ------------------------------------------------ BMP ------------------------------------------------ */
static unsigned char *bmp(const unsigned char *b, long len, int *pw, int *ph, char *err, int en)
{
    long off, hs, stride, o; int w, h, bpp, comp, top, ncol, x, y; unsigned char *out, pal[256 * 4];
    if (len < 54) return fail(err, en, "short BMP header");
    off = rd32(b + 10); hs = rd32(b + 14); w = (int)rd32(b + 18); h = (int)rd32(b + 22); bpp = (int)rd16(b + 28); comp = (int)rd32(b + 30);
    top = h < 0; if (top) h = -h;
    if (hs < 40 || w <= 0 || h <= 0 || w > 16384 || h > 16384) return fail(err, en, "bad BMP header");
    if (comp != 0 && comp != 3) return fail(err, en, "compressed BMP not supported");
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return fail(err, en, "unsupported BMP depth");
    ncol = (int)rd32(b + 46); if (!ncol && bpp <= 8) ncol = 1 << bpp; if (ncol > 256) ncol = 256;
    memset(pal, 0, sizeof pal);
    if (bpp <= 8) { if (14 + hs + 4L * ncol > len) return fail(err, en, "bad BMP palette"); memcpy(pal, b + 14 + hs, 4 * (size_t)ncol); }
    stride = (((long)w * bpp + 31) / 32) * 4;
    if (off + stride * h > len) return fail(err, en, "truncated BMP data");
    out = malloc((size_t)w * h * 4);
    for (y = 0; y < h; y++) {
        const unsigned char *row = b + off + stride * (top ? y : h - 1 - y);
        for (x = 0; x < w; x++) {
            unsigned char *d = out + 4 * ((long)y * w + x); unsigned v; int k;
            switch (bpp) {
            case 1: case 4: case 8: k = (row[(long)x * bpp / 8] >> (8 - bpp - (x * bpp) % 8)) & ((1 << bpp) - 1);
                d[0] = pal[4 * k + 2]; d[1] = pal[4 * k + 1]; d[2] = pal[4 * k]; d[3] = 255; break;
            case 16: v = rd16(row + 2 * x); d[0] = (unsigned char)(((v >> 10) & 31) * 255 / 31); d[1] = (unsigned char)(((v >> 5) & 31) * 255 / 31);
                d[2] = (unsigned char)((v & 31) * 255 / 31); d[3] = 255; break;                /* 5-5-5; bit-field 5-6-5 files are not handled */
            default: o = (long)x * (bpp / 8); d[0] = row[o + 2]; d[1] = row[o + 1]; d[2] = row[o]; d[3] = 255; break;
            }
        }
    }
    (void)o; *pw = w; *ph = h; return out;
}

/* ------------------------------------------------ DDS ------------------------------------------------ */
static void c565(unsigned v, int *c) { c[0] = (int)((v >> 11) & 31) * 255 / 31; c[1] = (int)((v >> 5) & 63) * 255 / 63; c[2] = (int)(v & 31) * 255 / 31; }
static void dxt_color(const unsigned char *s, unsigned char *blk, int four)   /* 4x4 RGBA block from an 8-byte colour block */
{
    unsigned a = rd16(s), b = rd16(s + 2), bits = rd32(s + 4); int c[4][4], i, k;
    c565(a, c[0]); c565(b, c[1]); c[0][3] = c[1][3] = 255;
    for (k = 0; k < 3; k++) {
        if (four || a > b) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; }
        else { c[2][k] = (c[0][k] + c[1][k]) / 2; c[3][k] = 0; }
    }
    c[2][3] = 255; c[3][3] = (four || a > b) ? 255 : 0;
    for (i = 0; i < 16; i++) { int *p = c[(bits >> (2 * i)) & 3]; for (k = 0; k < 4; k++) blk[4 * i + k] = (unsigned char)p[k]; }
}
static unsigned char *dds(const unsigned char *b, long len, int *pw, int *ph, char *err, int en)
{
    int w, h, x, y, i, k, fourcc, flags, bits; long o = 128, need; unsigned char *out, blk[64];
    if (len < 128 || rd32(b + 4) != 124) return fail(err, en, "bad DDS header");
    h = (int)rd32(b + 12); w = (int)rd32(b + 16); flags = (int)rd32(b + 80); fourcc = (int)rd32(b + 84); bits = (int)rd32(b + 88);
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return fail(err, en, "bad DDS size");
    out = malloc((size_t)w * h * 4);
    if (flags & 4) {                                                       /* DDPF_FOURCC */
        int kind = fourcc == 0x31545844 ? 1 : fourcc == 0x32545844 || fourcc == 0x33545844 ? 3 : fourcc == 0x34545844 || fourcc == 0x35545844 ? 5 : 0;
        int bs = kind == 1 ? 8 : 16;
        if (!kind) { free(out); return fail(err, en, "unsupported DDS compression"); }
        need = o + (long)((w + 3) / 4) * ((h + 3) / 4) * bs;
        if (need > len) { free(out); return fail(err, en, "truncated DDS data"); }
        for (y = 0; y < h; y += 4) for (x = 0; x < w; x += 4, o += bs) {
            const unsigned char *s = b + o;
            dxt_color(s + (kind == 1 ? 0 : 8), blk, kind != 1);
            if (kind == 3) for (i = 0; i < 16; i++) blk[4 * i + 3] = (unsigned char)(((s[i / 2] >> (4 * (i & 1))) & 15) * 17);
            if (kind == 5) {
                int a[8]; unsigned long long ab = 0;
                a[0] = s[0]; a[1] = s[1];
                for (k = 2; k < 8; k++) a[k] = a[0] > a[1] ? ((8 - k) * a[0] + (k - 1) * a[1]) / 7 : k < 6 ? ((6 - k) * a[0] + (k - 1) * a[1]) / 5 : k == 6 ? 0 : 255;
                for (k = 0; k < 6; k++) ab |= (unsigned long long)s[2 + k] << (8 * k);
                for (i = 0; i < 16; i++) blk[4 * i + 3] = (unsigned char)a[(ab >> (3 * i)) & 7];
            }
            for (i = 0; i < 16; i++) { int px = x + i % 4, py = y + i / 4; if (px < w && py < h) memcpy(out + 4 * ((long)py * w + px), blk + 4 * i, 4); }
        }
    } else if ((flags & 0x40) && (bits == 16 || bits == 24 || bits == 32)) {  /* DDPF_RGB with masks */
        unsigned m[4]; int sh[4], wd[4], pb = bits / 8;
        for (k = 0; k < 4; k++) { m[k] = rd32(b + 92 + 4 * k); sh[k] = 0; wd[k] = 0; if (m[k]) { while (!((m[k] >> sh[k]) & 1)) sh[k]++; while ((m[k] >> (sh[k] + wd[k])) & 1) wd[k]++; } }
        if (!(flags & 1)) m[3] = 0;                                         /* no DDPF_ALPHAPIXELS */
        if (o + (long)w * h * pb > len) { free(out); return fail(err, en, "truncated DDS data"); }
        for (i = 0; i < w * h; i++, o += pb) {
            unsigned v = pb == 2 ? rd16(b + o) : pb == 3 ? (unsigned)(b[o] | b[o + 1] << 8 | b[o + 2] << 16) : rd32(b + o);
            for (k = 0; k < 4; k++) out[4 * i + k] = m[k] ? (unsigned char)(((v & m[k]) >> sh[k]) * 255 / ((1u << wd[k]) - 1)) : 255;
        }
    } else { free(out); return fail(err, en, "unsupported DDS pixel format"); }
    *pw = w; *ph = h; return out;
}

unsigned char *img_decode(const unsigned char *buf, long len, int *w, int *h, char *err, int errlen)
{
    if (len >= 4 && !memcmp(buf, "DDS ", 4)) return dds(buf, len, w, h, err, errlen);
    if (len >= 2 && buf[0] == 'B' && buf[1] == 'M') return bmp(buf, len, w, h, err, errlen);
    return tga(buf, len, w, h, err, errlen);
}
unsigned char *img_load(const char *path, int *w, int *h, char *err, int errlen)
{
    FILE *fp = fopen(path, "rb"); long n; unsigned char *buf, *r;
    if (!fp) return fail(err, errlen, "cannot open file");
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    buf = malloc(n > 0 ? n : 1); if (n <= 0 || fread(buf, 1, n, fp) != (size_t)n) n = 0; fclose(fp);
    r = img_decode(buf, n, w, h, err, errlen); free(buf); return r;
}
