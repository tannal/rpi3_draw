#ifndef UART_H
#define UART_H

// 树莓派 3B 外设基地址
#define PBASE 0x3F000000

// Mini UART 相关寄存器地址
#define AUX_ENABLES     (PBASE + 0x00215004)
#define AUX_MU_IO_REG   (PBASE + 0x00215040)
#define AUX_MU_IER_REG  (PBASE + 0x00215044)
#define AUX_MU_IIR_REG  (PBASE + 0x00215048)
#define AUX_MU_LCR_REG  (PBASE + 0x0021504C)
#define AUX_MU_MCR_REG  (PBASE + 0x00215050)
#define AUX_MU_LSR_REG  (PBASE + 0x00215054)
#define AUX_MU_MSR_REG  (PBASE + 0x00215058)
#define AUX_MU_SCRATCH  (PBASE + 0x0021505C)
#define AUX_MU_CNTL_REG (PBASE + 0x00215060)
#define AUX_MU_STAT_REG (PBASE + 0x00215064)
#define AUX_MU_BAUD_REG (PBASE + 0x00215068)

// GPIO 引脚控制寄存器
#define GPFSEL1         (PBASE + 0x00200004)
#define GPPUD           (PBASE + 0x00200094)
#define GPPUDCLK0       (PBASE + 0x00200098)

// 函数原型声明
void uart_init();
void uart_send(unsigned int c);
char uart_getc();
void uart_puts(char *s);

#endif