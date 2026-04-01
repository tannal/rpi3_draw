#include <stdint.h>
#include <stddef.h>

#define STB_TRUETYPE_IMPLEMENTATION

// ── 裸机环境：替换 stb_truetype 依赖的标准库 ──────────────────────

#define HEAP_BASE 0x1000000
#define HEAP_SIZE (512 * 1024)

static uint8_t *heap_ptr = (uint8_t *)HEAP_BASE;
static uint8_t *heap_end = (uint8_t *)(HEAP_BASE + HEAP_SIZE);

static void heap_reset(void) { heap_ptr = (uint8_t *)HEAP_BASE; }

static void *bump_malloc(size_t size, void *u) {
    (void)u;
    size = (size + 7) & ~(size_t)7;
    if (heap_ptr + size > heap_end) return (void *)0;
    void *p = heap_ptr;
    heap_ptr += size;
    return p;
}
static void bump_free(void *p, void *u) { (void)p; (void)u; }

#define STBTT_malloc(x, u) bump_malloc(x, u)
#define STBTT_free(x, u)   bump_free(x, u)

void __assert_func(const char *f, int l, const char *fn, const char *e) { while (1); }

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}
void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}
size_t strlen(const char *s) { size_t i = 0; while (s[i]) i++; return i; }

double fabs(double x)  { return x < 0 ? -x : x; }
float  fabsf(float x)  { return x < 0 ? -x : x; }
double sqrt(double x) {
    if (x <= 0) return 0;
    double r = x;
    for (int i = 0; i < 8; i++) r = 0.5 * (r + x / r);
    return r;
}
double fmod(double x, double y) { return x - (int)(x / y) * y; }
double pow(double x, double y)  { return x; }
double cos(double x)   { return 1.0; }
double acos(double x)  { return 0.0; }
double floor(double x) { int i=(int)x; return (double)((x<i)?(i-1):i); }
double ceil (double x) { int i=(int)x; return (double)((x>i)?(i+1):i); }
float  floorf(float x) { int i=(int)x; return (float) ((x<i)?(i-1):i); }
float  ceilf (float x) { int i=(int)x; return (float) ((x>i)?(i+1):i); }

// font_data.h 由 xxd -i myfont.ttf > font_data.h 生成
// 里面会有: unsigned char myfont_ttf[] = {...}; unsigned int myfont_ttf_len = ...;
#include "font_data.h"
#include "stb_truetype.h"

// ── 硬件定义 ──────────────────────────────────────────────────────
#define MBOX_BASE    0x3F00B880
#define MBOX_READ    ((volatile uint32_t*)(MBOX_BASE + 0x00))
#define MBOX_STATUS  ((volatile uint32_t*)(MBOX_BASE + 0x18))
#define MBOX_WRITE   ((volatile uint32_t*)(MBOX_BASE + 0x20))
#define MBOX_EMPTY   0x40000000
#define MBOX_FULL    0x80000000

#define WIDTH  1024
#define HEIGHT 768

uint32_t __attribute__((aligned(16))) mbox[36];

int mbox_call() {
    uint32_t r = (((uint32_t)((size_t)&mbox) & ~0xF) | 8);
    while (*MBOX_STATUS & MBOX_FULL);
    *MBOX_WRITE = r;
    while (1) {
        while (*MBOX_STATUS & MBOX_EMPTY);
        if (r == *MBOX_READ) return mbox[1] == 0x80000000;
    }
}

// ── 绘制工具 ──────────────────────────────────────────────────────
static inline int iabs(int v) { return v < 0 ? -v : v; }

void draw_line(uint32_t *fb, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = iabs(x1-x0), sx = x0<x1?1:-1;
    int dy = -iabs(y1-y0), sy = y0<y1?1:-1;
    int err = dx+dy, e2;
    while (1) {
        if (x0>=0 && x0<WIDTH && y0>=0 && y0<HEIGHT)
            fb[y0*WIDTH+x0] = color;
        if (x0==x1 && y0==y1) break;
        e2 = 2*err;
        if (e2>=dy) { err+=dy; x0+=sx; }
        if (e2<=dx) { err+=dx; y0+=sy; }
    }
}

void draw_char(uint32_t *fb, stbtt_fontinfo *font, int codepoint,
               int x0, int y0, float scale, uint32_t color) {
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(font, &ascent, &descent, &line_gap);
    int baseline = y0 + (int)(ascent * scale);

    heap_reset();
    stbtt_vertex *v;
    int n = stbtt_GetCodepointShape(font, codepoint, &v);
    if (n == 0 || !v) return;

    int cur_x = 0, cur_y = 0;
    for (int i = 0; i < n; i++) {
        int vx  = (int)(v[i].x  * scale) + x0;
        int vy  = (int)(-v[i].y * scale) + baseline;
        int cx_ = (int)(v[i].cx * scale) + x0;
        int cy_ = (int)(-v[i].cy * scale) + baseline;
        switch (v[i].type) {
        case STBTT_vmove:
            cur_x = vx; cur_y = vy;
            break;
        case STBTT_vline:
            draw_line(fb, cur_x, cur_y, vx, vy, color);
            cur_x = vx; cur_y = vy;
            break;
        case STBTT_vcurve:
            for (int j = 1; j <= 8; j++) {
                float t = j/8.0f, it = 1.0f-t;
                int px = (int)(it*it*cur_x + 2*it*t*cx_ + t*t*vx);
                int py = (int)(it*it*cur_y + 2*it*t*cy_ + t*t*vy);
                draw_line(fb, cur_x, cur_y, px, py, color);
                cur_x = px; cur_y = py;
            }
            break;
        default: break;
        }
    }
    stbtt_FreeShape(font, v);
}

void draw_string(uint32_t *fb, stbtt_fontinfo *font, const char *str,
                 int x, int y, float scale, uint32_t color) {
    for (int i = 0; str[i]; i++) {
        draw_char(fb, font, (unsigned char)str[i], x, y, scale, color);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(font, (unsigned char)str[i], &adv, &lsb);
        x += (int)(adv * scale);
    }
}

// ── 主程序 ────────────────────────────────────────────────────────
void kernel_main() {
    mbox[0] = 35 * 4; mbox[1] = 0;
    mbox[2] = 0x48003; mbox[3] = 8; mbox[4] = 8; mbox[5] = WIDTH;  mbox[6] = HEIGHT;
    mbox[7] = 0x48004; mbox[8] = 8; mbox[9] = 8; mbox[10] = WIDTH; mbox[11] = HEIGHT;
    mbox[12] = 0x48005; mbox[13] = 4; mbox[14] = 4; mbox[15] = 32;
    mbox[16] = 0x40001; mbox[17] = 8; mbox[18] = 8; mbox[19] = 4096; mbox[20] = 0;
    mbox[21] = 0;

    if (mbox_call() && mbox[20] != 0) {
        uint32_t *fb = (uint32_t *)((size_t)mbox[19] & 0x3FFFFFFF);

        // 初始化字体
        // xxd 生成的数组名由文件名决定，myfont.ttf -> myfont_ttf
        stbtt_fontinfo font;
        if (!stbtt_InitFont(&font, assets_Minecraft_ttf, 0)) {
            while (1); // 字体加载失败，原地挂死
        }

        // 清屏为深灰
        for (int i = 0; i < WIDTH * HEIGHT; i++) fb[i] = 0x1A1A1A;

        // 绘制文字，像素高度 64px，白色
        float scale = stbtt_ScaleForPixelHeight(&font, 64.0f);
        draw_string(fb, &font, "Hello, World!", 100, 300, scale, 0xFFFFFF);
        draw_string(fb, &font, "Ni Hao", 100, 100, scale, 0xff0000);
    }
    while (1);
}