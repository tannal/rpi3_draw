#include <stddef.h>
#include <stdint.h>
#define STB_TRUETYPE_IMPLEMENTATION

// ── 线性内存分配器 ──────────────────────────────────────────────
#define HEAP_BASE  0x1000000
#define HEAP_SIZE  (512 * 1024)

static uint8_t *heap_ptr = (uint8_t *)HEAP_BASE;
static uint8_t *heap_end = (uint8_t *)(HEAP_BASE + HEAP_SIZE);

// 保存 stbtt_InitFont 完成后的堆位置，每次渲染字符后回退到这里
// 而不是回退到堆起点，避免破坏 font_info 依赖的堆数据
static uint8_t *heap_font_watermark = (uint8_t *)HEAP_BASE;

static void heap_reset_to_font_watermark(void) {
    heap_ptr = heap_font_watermark;
}

static void *bump_malloc(size_t size, void *u) {
    (void)u;
    size = (size + 7) & ~(size_t)7;
    if (heap_ptr + size > heap_end) return (void*)0;
    void *p = heap_ptr;
    heap_ptr += size;
    return p;
}

static void bump_free(void *p, void *u) { (void)p; (void)u; }

#define STBTT_malloc(x, u)  bump_malloc(x, u)
#define STBTT_free(x, u)    bump_free(x, u)

// ── libc 桩函数 ─────────────────────────────────────────────────
void __assert_func(const char *f, int l, const char *fn, const char *e) { while(1); }

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}
void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest; const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}
size_t strlen(const char *s) { size_t i=0; while(s[i]) i++; return i; }

double fabs(double x)  { return x < 0 ? -x : x; }
float  fabsf(float x)  { return x < 0 ? -x : x; }
double sqrt(double x) {
    if (x <= 0) return 0;
    double r = x;
    for (int i=0; i<8; i++) r = 0.5*(r + x/r);
    return r;
}
double fmod(double x, double y) { return x - (int)(x/y)*y; }
double pow(double x, double y) {
    if (y == 1.0/3.0) {
        double r=x; for(int i=0;i<8;i++) r=(2.0*r+x/(r*r))/3.0; return r;
    }
    return x;
}
double cos(double x)  { return 1.0; }
double acos(double x) { return 0.0; }
double floor(double x){ int i=(int)x; return (double)((x<i)?(i-1):i); }
double ceil (double x){ int i=(int)x; return (double)((x>i)?(i+1):i); }
float  floorf(float x){ int i=(int)x; return (float)((x<i)?(i-1):i); }
float  ceilf (float x){ int i=(int)x; return (float)((x>i)?(i+1):i); }

#include "stb_truetype.h"
#include "font_data_DejaVu.h"

// ── 硬件定义 ────────────────────────────────────────────────────
#define MBOX_BASE   0x3F00B880
#define MBOX_READ   ((volatile uint32_t*)(MBOX_BASE + 0x00))
#define MBOX_STATUS ((volatile uint32_t*)(MBOX_BASE + 0x18))
#define MBOX_WRITE  ((volatile uint32_t*)(MBOX_BASE + 0x20))
#define MBOX_EMPTY  0x40000000
#define MBOX_FULL   0x80000000

// mbox 必须 16 字节对齐，且物理地址可被 GPU 访问
uint32_t __attribute__((aligned(16))) mbox[64];

// 动态屏幕尺寸（由 query_screen_size 填充）
static uint32_t SCREEN_W = 0;
static uint32_t SCREEN_H = 0;

// ── Mailbox 调用 ─────────────────────────────────────────────────
static int mbox_call(void) {
    // Channel 8 = Property channel (ARM→VC)
    uint32_t r = (uint32_t)((size_t)mbox & ~0xFu) | 8u;
    while (*MBOX_STATUS & MBOX_FULL);
    *MBOX_WRITE = r;
    while (1) {
        while (*MBOX_STATUS & MBOX_EMPTY);
        if (*MBOX_READ == r)
            return mbox[1] == 0x80000000;
    }
}

// ── 第一步：查询当前物理屏幕分辨率 ──────────────────────────────
// Tag 0x40003 = Get Physical (Display) Size，返回 width/height
// 如果固件没有设置显示器，返回 0；此时回退到安全默认值。
static void query_screen_size(void) {
    mbox[0] = 8 * 4;       // 总字节数
    mbox[1] = 0;           // 请求
    mbox[2] = 0x40003;     // Get Physical Size
    mbox[3] = 8;           // value buffer 大小（字节）
    mbox[4] = 0;           // request/response 标志
    mbox[5] = 0;           // out: width
    mbox[6] = 0;           // out: height
    mbox[7] = 0;           // End tag

    if (mbox_call() && mbox[5] != 0 && mbox[6] != 0) {
        SCREEN_W = mbox[5];
        SCREEN_H = mbox[6];
    } else {
        // 回退默认值：HDMI 常见安全分辨率
        SCREEN_W = 1024;
        SCREEN_H = 768;
    }
}

// ── 第二步：用查询到的尺寸初始化 Framebuffer ─────────────────────
// 返回 framebuffer 指针，失败返回 NULL
static uint32_t *init_framebuffer(void) {
    int idx = 0;
    mbox[idx++] = 0;         // [0] 总大小（稍后填）
    mbox[idx++] = 0;         // [1] 请求码

    // Tag: Set Physical Size
    mbox[idx++] = 0x48003;
    mbox[idx++] = 8;
    mbox[idx++] = 0;
    mbox[idx++] = SCREEN_W;
    mbox[idx++] = SCREEN_H;

    // Tag: Set Virtual Size（与物理相同，单缓冲）
    mbox[idx++] = 0x48004;
    mbox[idx++] = 8;
    mbox[idx++] = 0;
    mbox[idx++] = SCREEN_W;
    mbox[idx++] = SCREEN_H;

    // Tag: Set Virtual Offset（偏移 0,0）
    mbox[idx++] = 0x48009;
    mbox[idx++] = 8;
    mbox[idx++] = 0;
    mbox[idx++] = 0;         // x offset
    mbox[idx++] = 0;         // y offset

    // Tag: Set Depth（32 bpp）
    mbox[idx++] = 0x48005;
    mbox[idx++] = 4;
    mbox[idx++] = 0;
    mbox[idx++] = 32;

    // Tag: Set Pixel Order（RGB，非 BGR）
    mbox[idx++] = 0x48006;
    mbox[idx++] = 4;
    mbox[idx++] = 0;
    mbox[idx++] = 1;         // 1 = RGB

    // Tag: Allocate Buffer
    // 输入：alignment；输出：[base_addr, size]
    // idx 在 mbox_call 后：
    //   mbox[idx+3] = 分配到的物理地址
    //   mbox[idx+4] = 缓冲区大小
    int alloc_idx = idx;
    mbox[idx++] = 0x40001;
    mbox[idx++] = 8;
    mbox[idx++] = 0;
    mbox[idx++] = 16;        // alignment（输入），响应后被地址覆盖
    mbox[idx++] = 0;         // out: size

    // Tag: Get Pitch（每行字节数，可选，方便后续使用）
    mbox[idx++] = 0x40008;
    mbox[idx++] = 4;
    mbox[idx++] = 0;
    mbox[idx++] = 0;         // out: bytes per line

    mbox[idx++] = 0;         // End tag
    mbox[0]     = idx * 4;   // 填总大小

    if (!mbox_call()) return (void*)0;

    // 检查地址是否有效（非零）
    uint32_t fb_addr = mbox[alloc_idx + 3];
    if (fb_addr == 0) return (void*)0;

    // GPU 返回的是总线地址（0xCxxxxxxx），屏蔽高位得 ARM 物理地址
    return (uint32_t *)((size_t)(fb_addr) & 0x3FFFFFFF);
}

// ── 工具 ────────────────────────────────────────────────────────
static inline int iabs(int v) { return v < 0 ? -v : v; }

static inline void set_pixel(uint32_t *fb, int x, int y, uint32_t color) {
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H)
        fb[y * SCREEN_W + x] = color;
}

// ── Bresenham 画线 ───────────────────────────────────────────────
void draw_line(uint32_t *fb, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx =  iabs(x1-x0), sx = x0<x1 ? 1 : -1;
    int dy = -iabs(y1-y0), sy = y0<y1 ? 1 : -1;
    int err = dx+dy, e2;
    while (1) {
        set_pixel(fb, x0, y0, color);
        if (x0==x1 && y0==y1) break;
        e2 = 2*err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// ── 矢量字符渲染 ─────────────────────────────────────────────────
void draw_ttf_char(uint32_t *fb, stbtt_fontinfo *font,
                   int codepoint, int x0, int y0,
                   float scale, uint32_t color) {

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(font, &ascent, &descent, &line_gap);
    int baseline = y0 + (int)(ascent * scale);

    // 回退堆到字体水印位置，回收上一个字符的顶点数据
    heap_reset_to_font_watermark();

    stbtt_vertex *v;
    int n = stbtt_GetCodepointShape(font, codepoint, &v);
    if (n == 0 || !v) return;

    int cur_x = 0, cur_y = 0;
    for (int i = 0; i < n; i++) {
        int vx = (int)(v[i].x  * scale) + x0;
        int vy = (int)(-v[i].y * scale) + baseline;
        int cx = (int)(v[i].cx * scale) + x0;
        int cy = (int)(-v[i].cy * scale) + baseline;

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
                float t = j / 8.0f, it = 1.0f - t;
                int px = (int)(it*it*cur_x + 2*it*t*cx + t*t*vx);
                int py = (int)(it*it*cur_y + 2*it*t*cy + t*t*vy);
                draw_line(fb, cur_x, cur_y, px, py, color);
                cur_x = px; cur_y = py;
            }
            break;
        default: break;
        }
    }
    stbtt_FreeShape(font, v);  // 空操作，无害
}

// ── 渲染字符串 ───────────────────────────────────────────────────
void draw_string(uint32_t *fb, stbtt_fontinfo *font,
                 const char *str, int x, int y,
                 float scale, uint32_t color) {
    for (int i = 0; str[i]; i++) {
        draw_ttf_char(fb, font, (unsigned char)str[i], x, y, scale, color);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(font, (unsigned char)str[i], &adv, &lsb);
        x += (int)(adv * scale);
        // 字距微调（可选，性能开销略高）
        if (str[i+1]) {
            int kern = stbtt_GetCodepointKernAdvance(font,
                           (unsigned char)str[i],
                           (unsigned char)str[i+1]);
            x += (int)(kern * scale);
        }
    }
}

// ── 填充矩形 ─────────────────────────────────────────────────────
void fill_rect(uint32_t *fb, int rx, int ry, int rw, int rh, uint32_t color) {
    for (int j = 0; j < rh; j++)
        for (int i = 0; i < rw; i++)
            set_pixel(fb, rx+i, ry+j, color);
}

// ── 窗口 & 按钮 ──────────────────────────────────────────────────
typedef struct {
    int x, y, w, h;
    const char *title;
    uint32_t bg_color;
    uint32_t border_color;
} Window;

void draw_window(uint32_t *fb, Window *win,
                 stbtt_fontinfo *font, float scale) {
    const int title_h = 30;

    // 背景
    fill_rect(fb, win->x, win->y, win->w, win->h, win->bg_color);
    // 标题栏
    fill_rect(fb, win->x, win->y, win->w, title_h, 0x333355);
    // 边框（逐边画线，比逐像素判断快一点）
    draw_line(fb, win->x,           win->y,            win->x+win->w-1, win->y,            win->border_color);
    draw_line(fb, win->x,           win->y+win->h-1,   win->x+win->w-1, win->y+win->h-1,   win->border_color);
    draw_line(fb, win->x,           win->y,             win->x,          win->y+win->h-1,   win->border_color);
    draw_line(fb, win->x+win->w-1,  win->y,             win->x+win->w-1, win->y+win->h-1,   win->border_color);

    // 标题文字
    draw_string(fb, font, win->title,
                win->x + 10, win->y + 4,
                scale * 0.4f, 0xFFFFFF);
}

typedef struct {
    int x, y, w, h;
    const char *text;
    int is_pressed;
} Button;

void draw_button(uint32_t *fb, Button *btn,
                 stbtt_fontinfo *font, float scale) {
    uint32_t bg = btn->is_pressed ? 0x888888 : 0x4466AA;
    fill_rect(fb, btn->x, btn->y, btn->w, btn->h, bg);
    // 简单高光（顶/左亮，底/右暗，模拟立体感）
    draw_line(fb, btn->x,          btn->y,          btn->x+btn->w-1, btn->y,          0xAABBDD);
    draw_line(fb, btn->x,          btn->y,          btn->x,          btn->y+btn->h-1, 0xAABBDD);
    draw_line(fb, btn->x,          btn->y+btn->h-1, btn->x+btn->w-1, btn->y+btn->h-1, 0x223355);
    draw_line(fb, btn->x+btn->w-1, btn->y,          btn->x+btn->w-1, btn->y+btn->h-1, 0x223355);

    draw_string(fb, font, btn->text,
                btn->x + 10, btn->y + 6,
                scale * 0.28f, 0xFFFFFF);
}

// 辅助函数：计算边缘函数（本质是二维向量叉乘）
// 返回值 > 0 表示点在向量右侧，< 0 在左侧，0 在线上
inline int edge_func(int x0, int y0, int x1, int y1, int px, int py) {
    return (px - x0) * (y1 - y0) - (py - y0) * (x1 - x0);
}

inline int min3(int a, int b, int c) {
    int m = a;
    if (b < m) m = b;
    if (c < m) m = c;
    return m;
}

inline int max3(int a, int b, int c) {
    int m = a;
    if (b > m) m = b;
    if (c > m) m = c;
    return m;
}

void draw_triangle(uint32_t *fb, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    // 1. 计算三角形的包围盒 (Bounding Box)
    int min_x = min3(x0, x1, x2);
    int max_x = max3(x0, x1, x2);
    int min_y = min3(y0, y1, y2);
    int max_y = max3(y0, y1, y2);

    // 2. 这里的坐标判定要遵循 "Center Sampling" (像素中心在三角形内)
    // 遍历包围盒内的每一个像素
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            
            // 取像素中心点坐标 (x+0.5, y+0.5)
            // 为了避免浮点运算，我们将坐标放大 2 倍处理，或者直接用整数逻辑
            // 这里的 px, py 对应像素中心
            float px = x + 0.5f;
            float py = y + 0.5f;

            // 3. 检查中心点是否在三条边的同一侧
            // 注意：顶点顺序（顺时针或逆时针）会影响正负号
            // 下面的逻辑假设是统一的环绕方向
            int w0 = edge_func(x0, y0, x1, y1, x, y); // 这里简化用整数，实际作业建议用 float 或处理 0.5
            int w1 = edge_func(x1, y1, x2, y2, x, y);
            int w2 = edge_func(x2, y2, x0, y0, x, y);

            // 如果都在内侧（对于特定的顶点顺序，w 必须全部 >= 0 或全部 <= 0）
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                set_pixel(fb, x, y, color);
            }
        }
    }
}

// ── 主程序 ───────────────────────────────────────────────────────
void kernel_main(void) {

    // 1. 查询物理屏幕尺寸
    query_screen_size();

    // 2. 初始化字体（在 fb 初始化之前，让字体数据占用堆的头部）
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, assets_DejaVuSans_ttf, 0))
        while (1);          // 字体加载失败，挂起

    // 记录字体初始化后的堆水印：后续每次渲染字符后回到这里
    heap_font_watermark = heap_ptr;

    // 3. 初始化 Framebuffer
    uint32_t *fb = init_framebuffer();
    if (!fb) while (1);     // 分配失败，挂起

    // 4. 清屏（深灰背景）
    uint32_t total_px = SCREEN_W * SCREEN_H;
    for (uint32_t i = 0; i < total_px; i++) fb[i] = 0x1A1A2E;

    // 5. 计算字体缩放（目标 48px 行高）
    float scale = stbtt_ScaleForPixelHeight(&font, 48.0f);

    // 6. 绘制 UI

    // 窗口
    Window info_win = {
        .x = (int)SCREEN_W - 320,
        .y = 60,
        .w = 300,
        .h = 220,
        .title       = "OS MONITOR",
        .bg_color    = 0x16213E,
        .border_color = 0x00FF88,
    };
    draw_window(fb, &info_win, &font, scale);

    // 窗口内容文字
    draw_string(fb, &font, "CPU:  12%",
                info_win.x + 12, info_win.y + 40, scale * 0.32f, 0x00FF88);
    draw_string(fb, &font, "MEM:  64 MB",
                info_win.x + 12, info_win.y + 90, scale * 0.32f, 0x00CCFF);
    draw_string(fb, &font, "TEMP: 42 C",
                info_win.x + 12, info_win.y + 140, scale * 0.32f, 0xFFAA00);

    // 按钮
    Button ok_btn = {
        .x = info_win.x + 20, .y = info_win.y + 170,
        .w = 100, .h = 34,
        .text = "OK",
        .is_pressed = 0,
    };
    draw_button(fb, &ok_btn, &font, scale);

    // 大字欢迎语
    draw_string(fb, &font, "Hello, OS!", 60, 80, scale, 0xFFCC00);

    // 单字符示例
    draw_ttf_char(fb, &font, 'A', 60,  200, scale * 0.8f, 0xFF4444);
    draw_ttf_char(fb, &font, 'B', 130, 200, scale * 0.8f, 0x44FF44);
    draw_ttf_char(fb, &font, 'C', 200, 200, scale * 0.8f, 0x4444FF);

    // 屏幕分辨率提示（用小字显示在左下角，方便调试）
    draw_string(fb, &font, "RES:", 10, (int)SCREEN_H - 50,
                scale * 0.25f, 0x888888);
    draw_triangle(fb, 100, 100, 400, 100, 400, 400, 0x0000ff);
    while (1);  // 挂起，持续显示
}