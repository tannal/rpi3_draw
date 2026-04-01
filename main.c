#include <stdint.h>
#include <stddef.h>

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

static inline int iabs(int v) { return v < 0 ? -v : v; }

void draw_line(uint32_t *fb, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = iabs(x1 - x0), sx = x0 < x1 ? 1 : -1;
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

// 二次贝塞尔曲线
// P0(x0,y0) 起点，P1(cx,cy) 控制点，P2(x1,y1) 终点
// steps 越大曲线越平滑，一般 64~256 足够
void draw_bezier(uint32_t *fb,
                 int x0, int y0,   // 起点
                 int cx, int cy,   // 控制点
                 int x1, int y1,   // 终点
                 int steps,
                 uint32_t color) {
    int prev_x = x0, prev_y = y0;
    for (int i = 1; i <= steps; i++) {
        float t  = (float)i / steps;
        float it = 1.0f - t;
        // B(t) = (1-t)^2 * P0 + 2(1-t)t * P1 + t^2 * P2
        int nx = (int)(it * it * x0 + 2.0f * it * t * cx + t * t * x1);
        int ny = (int)(it * it * y0 + 2.0f * it * t * cy + t * t * y1);
        draw_line(fb, prev_x, prev_y, nx, ny, color);
        prev_x = nx;
        prev_y = ny;
    }
}

void kernel_main() {
    mbox[0] = 35 * 4; mbox[1] = 0;
    mbox[2] = 0x48003; mbox[3] = 8; mbox[4] = 8; mbox[5] = WIDTH;  mbox[6] = HEIGHT;
    mbox[7] = 0x48004; mbox[8] = 8; mbox[9] = 8; mbox[10] = WIDTH; mbox[11] = HEIGHT;
    mbox[12] = 0x48005; mbox[13] = 4; mbox[14] = 4; mbox[15] = 32;
    mbox[16] = 0x40001; mbox[17] = 8; mbox[18] = 8; mbox[19] = 4096; mbox[20] = 0;
    mbox[21] = 0;

    if (mbox_call() && mbox[20] != 0) {
        uint32_t *fb = (uint32_t *)((size_t)mbox[19] & 0x3FFFFFFF);

        // 起点(100,600)，控制点(512,50)，终点(900,600)，黄色
        draw_bezier(fb, 100, 600, 512, 50, 900, 600, 128, 0xFFFF00);
    }
    while (1);
}