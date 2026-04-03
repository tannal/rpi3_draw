#include "uart.h"
#include "io.h"

/**
 * 初始化 Mini UART (AUX)
 * 波特率设为 115200，8位数据位，无校验
 */
void uart_init() {
    unsigned int selector;

    // 1. 启用 Mini UART 模块，以便访问其寄存器
    // AUX_ENABLES: bit 0 为 Mini UART, bit 1 为 SPI1, bit 2 为 SPI2
    mmio_write(AUX_ENABLES, 1);

    // 2. 暂时关闭收发功能，以便安全配置
    mmio_write(AUX_MU_CNTL_REG, 0);

    // 3. 配置波特率
    // 树莓派 3B 的系统时钟频率默认为 250MHz
    // 波特率计算公式: baudrate = system_clock / (8 * (baudrate_reg + 1))
    // 250,000,000 / (8 * (270 + 1)) ≈ 115276 (误差在 115200 允许范围内)
    mmio_write(AUX_MU_BAUD_REG, 270);

    // 4. 设置数据格式
    mmio_write(AUX_MU_LCR_REG, 3);     // 8位模式 (bit 0-1 设为 11)
    mmio_write(AUX_MU_MCR_REG, 0);     // 设置调制控制为 0
    mmio_write(AUX_MU_IER_REG, 0);     // 禁用中断（初期使用轮询方式）
    mmio_write(AUX_MU_IIR_REG, 0xC6);  // 禁用并清除 FIFO

    // 5. 配置 GPIO 引脚功能 (GPIO 14 = TX, GPIO 15 = RX)
    // GPFSEL1 控制 GPIO 10-19。每个引脚占 3 位。
    selector = mmio_read(GPFSEL1);
    
    // 清除 GPIO 14 (bits 12-14) 和 15 (bits 15-17)
    selector &= ~((7 << 12) | (7 << 15));
    
    // 设置为 Alt5 模式 (二进制 010，即十进制 2)
    selector |= (2 << 12) | (2 << 15);
    mmio_write(GPFSEL1, selector);

    // 6. 禁用引脚的上拉/下拉电阻 (防止信号干扰)
    mmio_write(GPPUD, 0);
    for (int i = 0; i < 150; i++) asm volatile("nop"); // 等待 150 个周期
    mmio_write(GPPUDCLK0, (1 << 14) | (1 << 15));
    for (int i = 0; i < 150; i++) asm volatile("nop");
    mmio_write(GPPUDCLK0, 0); // 刷新配置

    // 7. 重新开启发送和接收器
    mmio_write(AUX_MU_CNTL_REG, 3);
}

/**
 * 发送一个字节
 */
void uart_send(unsigned int c) {
    // 等待直到发送 FIFO 至少有一个字节的空位 (LSR bit 5)
    while (!(mmio_read(AUX_MU_LSR_REG) & 0x20));
    mmio_write(AUX_MU_IO_REG, c);
}

/**
 * 接收一个字节 (阻塞式)
 */
char uart_getc() {
    // 等待直到接收 FIFO 有数据读取 (LSR bit 0)
    while (!(mmio_read(AUX_MU_LSR_REG) & 0x01));
    return (char)(mmio_read(AUX_MU_IO_REG) & 0xFF);
}

/**
 * 发送字符串
 * 自动将 \n 转换为 \r\n 以适配大多数串口终端
 */
void uart_puts(char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_send('\r');
        }
        uart_send(*s++);
    }
}