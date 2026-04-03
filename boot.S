// boot.S
.section ".text.boot"

.global _start

_start:
    // 1. 读取多核 ID，只允许主核 (ID == 0) 继续
    mrs     x0, mpidr_el1
    and     x0, x0, #3
    cbz     x0, master
    b       halt

halt: 
    wfe
    b       halt

master:
    // 2. 设置栈指针 (Stack Pointer)
    ldr     x0, =_start
    mov     sp, x0

    // 3. 清零 BSS 段 (初始值为 0 的全局变量区)
    ldr     x0, =__bss_start
    ldr     w1, =__bss_size
setup_bss:
    cbz     w1, run_main
    str     xzr, [x0], #8
    sub     w1, w1, #1
    cbnz    w1, setup_bss

run_main:
    // 4. 跳转到 C 语言入口
    bl      kernel_main
    b       halt