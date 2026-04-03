#include "uart.h" // 记得创建对应的 .h 声明上述函数

void kernel_main() {
    // 1. 初始化串口
    uart_init();

    // 2. 打印欢迎信息
    uart_puts("\n\n------------------------------\n");
    uart_puts("  GeminiOS Kernel v0.1 Initializing...\n");
    uart_puts("  Architecture: AArch64 (ARMv8-A)\n");
    uart_puts("  Hardware: Raspberry Pi 3B (BCM2837)\n");
    uart_puts("------------------------------\n\n");

    uart_puts("Boot process complete. Welcome to the mini shell!\n");
    uart_puts("System > ");

    // 3. 简单的回显循环 (Echo)
    while (1) {
        // 这里可以扩展读取 uart_getc() 
        // 暂时只是维持内核运行
    }
}