.section ".text.boot"

.global _start

_start:
    // 1. 只有主核心 (Core 0) 执行，其他核心挂起
    mrs     x0, mpidr_el1
    and     x0, x0, #3
    cbz     x0, master
hang: 
    wfe
    b       hang

master:
    // 2. 开启浮点运算 (CPACR_EL1)
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    // 3. 设置栈指针 (放在 0x80000 之下)
    ldr     x0, =_start
    mov     sp, x0

    // 4. 清除 BSS 段
    ldr     x0, =__bss_start
    ldr     w1, =__bss_size
cbss:
    cbz     w1, run_main
    str     xzr, [x0], #8
    sub     w1, w1, #1
    cbnz    w1, cbss

run_main:
    bl      kernel_main
    b       hang