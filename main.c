#include "uart.h" // 记得创建对应的 .h 声明上述函数

void kernel_main() {
    char input_buffer[128];
    int index = 0;

    uart_init();
    uart_puts("\nGeminiOS Shell v0.1\n# ");

    while (1) {
        char c = uart_getc();
        
        // 回显字符
        uart_send(c); 

        if (c == '\r' || c == '\n') {
            input_buffer[index] = '\0';
            uart_puts("\nReceived command: ");
            uart_puts(input_buffer);
            uart_puts("\n# ");
            index = 0;
        } else {
            if (index < 127) {
                input_buffer[index++] = c;
            }
        }
    }
}