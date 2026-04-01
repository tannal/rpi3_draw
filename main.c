#include <stdint.h>
#include <stddef.h>
// Mailbox 寄存器定义
#define MBOX_BASE    0x3F00B880
#define MBOX_READ    ((volatile uint32_t*)(MBOX_BASE + 0x00))
#define MBOX_STATUS  ((volatile uint32_t*)(MBOX_BASE + 0x18))
#define MBOX_WRITE   ((volatile uint32_t*)(MBOX_BASE + 0x20))
#define MBOX_EMPTY   0x40000000
#define MBOX_FULL    0x80000000

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

void draw_mandelbrot(uint32_t *fb) {
    int width = 1024, height = 768;
    for (int py = 0; py < height; py++) {
        for (int px = 0; px < width; px++) {
            float cx = (px - 700.0) / 300.0; // 缩放偏移
            float cy = (py - 384.0) / 300.0;
            float zx = 0, zy = 0;
            int iter = 0;
            while (zx*zx + zy*zy <= 4.0 && iter < 100) {
                float tmp = zx*zx - zy*zy + cx;
                zy = 2*zx*zy + cy;
                zx = tmp;
                iter++;
            }
            fb[py * width + px] = (iter == 100) ? 0 : (iter * 2 << 16 | iter * 5 << 8 | 255);
        }
    }
}

void kernel_main() {
    mbox[0] = 35 * 4; mbox[1] = 0;
    mbox[2] = 0x48003; mbox[3] = 8; mbox[4] = 8; mbox[5] = 1024; mbox[6] = 768;
    mbox[7] = 0x48004; mbox[8] = 8; mbox[9] = 8; mbox[10] = 1024; mbox[11] = 768;
    mbox[12] = 0x48005; mbox[13] = 4; mbox[14] = 4; mbox[15] = 32;
    mbox[16] = 0x40001; mbox[17] = 8; mbox[18] = 8; mbox[19] = 4096; mbox[20] = 0;
    mbox[21] = 0;

    if (mbox_call() && mbox[20] != 0) {
        uint32_t *fb = (uint32_t *)((size_t)mbox[19] & 0x3FFFFFFF);
        draw_mandelbrot(fb);
    }
    while(1);
}