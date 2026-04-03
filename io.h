#ifndef IO_H
#define IO_H

// 向地址写入 32 位数据
static inline void mmio_write(unsigned long reg, unsigned int val) {
    *(volatile unsigned int *)reg = val;
}

// 从地址读取 32 位数据
static inline unsigned int mmio_read(unsigned long reg) {
    return *(volatile unsigned int *)reg;
}

#endif