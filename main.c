#include <stddef.h>
#include <stdint.h>
#define STB_TRUETYPE_IMPLEMENTATION

// ── 线性内存分配器 ──────────────────────────────────────────────
// 使用一块静态区域，每次 malloc 从中顺序分配，free 是空操作。
// 对于"分配→使用→整体重置"的场景（如逐字符渲染）完全够用。
#define HEAP_BASE  0x1000000
#define HEAP_SIZE  (512 * 1024)   // 512 KB，足够 stbtt 用

static uint8_t *heap_ptr = (uint8_t *)HEAP_BASE;
static uint8_t *heap_end = (uint8_t *)(HEAP_BASE + HEAP_SIZE);

static void heap_reset(void) {
    heap_ptr = (uint8_t *)HEAP_BASE;
}

static void *bump_malloc(size_t size, void *u) {
    (void)u;
    // 8 字节对齐
    size = (size + 7) & ~(size_t)7;
    if (heap_ptr + size > heap_end) return (void*)0; // OOM
    void *p = heap_ptr;
    heap_ptr += size;
    return p;
}

static void bump_free(void *p, void *u) {
    (void)p; (void)u;
    // 线性分配器不支持单独释放，通过 heap_reset() 统一回收
}

#define STBTT_malloc(x, u)  bump_malloc(x, u)
#define STBTT_free(x, u)    bump_free(x, u)
// ────────────────────────────────────────────────────────────────

void __assert_func(const char *file, int line,
                   const char *func, const char *failedexpr) {
    while(1);
}

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

size_t strlen(const char *s) {
    size_t i = 0;
    while (s[i]) i++;
    return i;
}

double fabs(double x)  { return x < 0 ? -x : x; }
float  fabsf(float x)  { return x < 0 ? -x : x; }

double sqrt(double x) {
    if (x <= 0) return 0;
    double res = x;
    for (int i = 0; i < 8; i++) res = 0.5 * (res + x / res);
    return res;
}

double fmod(double x, double y) { return x - (int)(x / y) * y; }

double pow(double x, double y) {
    if (y == 1.0/3.0) {
        double res = x;
        for (int i = 0; i < 8; i++) res = (2.0*res + x/(res*res)) / 3.0;
        return res;
    }
    return x;
}

double cos(double x)  { return 1.0; }
double acos(double x) { return 0.0; }

double floor(double x) { int i=(int)x; return (double)((x<i)?(i-1):i); }
double ceil (double x) { int i=(int)x; return (double)((x>i)?(i+1):i); }
float  floorf(float x) { int i=(int)x; return (float) ((x<i)?(i-1):i); }
float  ceilf (float x) { int i=(int)x; return (float) ((x>i)?(i+1):i); }

#include "stb_truetype.h"
#include "font_data_DejaVu.h"
#include <stdint.h>

// ── 硬件定义 ────────────────────────────────────────────────────
#define MBOX_BASE   0x3F00B880
#define MBOX_READ   ((volatile uint32_t*)(MBOX_BASE + 0x00))
#define MBOX_STATUS ((volatile uint32_t*)(MBOX_BASE + 0x18))
#define MBOX_WRITE  ((volatile uint32_t*)(MBOX_BASE + 0x20))
#define MBOX_EMPTY  0x40000000
#define MBOX_FULL   0x80000000

uint32_t __attribute__((aligned(16))) mbox[36];

#define WIDTH  1024
#define HEIGHT 768

// ── 工具 ────────────────────────────────────────────────────────
static inline int iabs(int v) { return v < 0 ? -v : v; }

// ── Bresenham 画线 ───────────────────────────────────────────────
void draw_line(uint32_t *fb, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx =  iabs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -iabs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        if (x0 >= 0 && x0 < WIDTH && y0 >= 0 && y0 < HEIGHT)
            fb[y0 * WIDTH + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// ── 矢量字符渲染 ─────────────────────────────────────────────────
// x0, y0 是字符左上角坐标（我们内部自动换算到正确基线位置）
void draw_ttf_char(uint32_t *fb, stbtt_fontinfo *font,
                   char c, int x0, int y0, float scale, uint32_t color) {

    // 获取字体度量，计算基线偏移
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(font, &ascent, &descent, &line_gap);
    // baseline 相对于 y0（左上角）的偏移
    int baseline = y0 + (int)(ascent * scale);

    // 每次绘制一个字符前重置堆，让顶点数组从头分配
    // （因为 stbtt_FreeShape 无法真正释放，统一在这里回收）
    heap_reset();

    stbtt_vertex *v;
    int n = stbtt_GetCodepointShape(font, (int)(unsigned char)c, &v);
    if (n == 0 || v == (void*)0) return;

    int cur_x = 0, cur_y = 0;
    for (int i = 0; i < n; i++) {
        // stbtt Y 轴向上，屏幕 Y 轴向下，所以 y 分量取反后加 baseline
        int vx = (int)(v[i].x  * scale) + x0;
        int vy = (int)(-v[i].y * scale) + baseline;   // ← 修正：加 baseline
        int cx = (int)(v[i].cx * scale) + x0;
        int cy = (int)(-v[i].cy * scale) + baseline;  // ← 修正

        switch (v[i].type) {
        case STBTT_vmove:
            cur_x = vx; cur_y = vy;
            break;
        case STBTT_vline:
            draw_line(fb, cur_x, cur_y, vx, vy, color);
            cur_x = vx; cur_y = vy;
            break;
        case STBTT_vcurve:
            // 二次贝塞尔，8 段细分
            for (int j = 1; j <= 8; j++) {
                float t  = j / 8.0f;
                float it = 1.0f - t;
                int px = (int)(it*it*cur_x + 2*it*t*cx + t*t*vx);
                int py = (int)(it*it*cur_y + 2*it*t*cy + t*t*vy);
                draw_line(fb, cur_x, cur_y, px, py, color);
                cur_x = px; cur_y = py;
            }
            break;
        // STBTT_vcubic 在 TrueType 中不出现，但防御性处理
        default:
            break;
        }
    }
    // stbtt_FreeShape 内部调用 STBTT_free，是空操作，无害
    stbtt_FreeShape(font, v);
}

// ── 渲染一行字符串的辅助函数 ────────────────────────────────────
void draw_string(uint32_t *fb, stbtt_fontinfo *font,
                 const char *str, int x, int y, float scale, uint32_t color) {
    for (int i = 0; str[i]; i++) {
        draw_ttf_char(fb, font, str[i], x, y, scale, color);
        // 推进 x：使用字形的 advance width
        int adv, lsb;
        stbtt_GetCodepointHMetrics(font, (unsigned char)str[i], &adv, &lsb);
        x += (int)(adv * scale);
    }
}

// ── Mailbox ──────────────────────────────────────────────────────
int mbox_call() {
    uint32_t r = (uint32_t)((size_t)mbox & ~0xFu) | 8u;
    while (*MBOX_STATUS & MBOX_FULL);
    *MBOX_WRITE = r;
    while (1) {
        while (*MBOX_STATUS & MBOX_EMPTY);
        if (*MBOX_READ == r) return mbox[1] == 0x80000000;
    }
}

// ── 主程序 ───────────────────────────────────────────────────────
void kernel_main() {

    // 正确构造 Property Channel tag list
    int idx = 0;
    mbox[idx++] = 0;         // [0]  总大小，稍后填
    mbox[idx++] = 0;         // [1]  请求码

    mbox[idx++] = 0x48003;  // Set Physical Size
    mbox[idx++] = 8;
    mbox[idx++] = 0;         // ← request/response code 必须为 0
    mbox[idx++] = WIDTH;
    mbox[idx++] = HEIGHT;

    mbox[idx++] = 0x48004;  // Set Virtual Size
    mbox[idx++] = 8;
    mbox[idx++] = 0;         // ← 0
    mbox[idx++] = WIDTH;
    mbox[idx++] = HEIGHT;

    mbox[idx++] = 0x48005;  // Set Depth
    mbox[idx++] = 4;
    mbox[idx++] = 0;         // ← 0
    mbox[idx++] = 32;

    mbox[idx++] = 0x40001;  // Allocate Buffer
    mbox[idx++] = 8;
    mbox[idx++] = 0;         // ← 0
    mbox[idx++] = 16;        // alignment = 16
    mbox[idx++] = 0;         // out: size

    mbox[idx++] = 0;         // End tag
    mbox[0] = idx * 4;       // 正确的总大小 = 22 * 4 = 88

    // 初始化字体
    stbtt_fontinfo font_info;
    if (!stbtt_InitFont(&font_info, assets_DejaVuSans_ttf, 0)) {
        while (1);
    }

    if (mbox_call() && mbox[20] != 0) {
        uint32_t *fb = (uint32_t *)((size_t)mbox[19] & 0x3FFFFFFF);

        // 填充深灰色背景
        for (int i = 0; i < WIDTH * HEIGHT; i++) fb[i] = 0x1A1A1A;

        // 计算缩放：目标字高 128px
        float scale = stbtt_ScaleForPixelHeight(&font_info, 128.0f);

        // 绘制单个字符
        draw_ttf_char(fb, &font_info, 'M', 100, 100, scale, 0xFFCC00);
        draw_ttf_char(fb, &font_info, 'C', 250, 100, scale, 0x00FF00);

        // 或者直接绘制字符串（字间距自动处理）
        draw_string(fb, &font_info, "Hello!", 100, 300, scale, 0x00CCFF);
    }

    while (1);
}