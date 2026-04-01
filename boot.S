.section ".text.boot"
.global _start

_start:
    // 1. 只有 Core 0 执行
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, master
hang: 
    wfe
    b       hang

master:
    // 2. 开启 FPU (必须在任何浮点运算前)
    mov     x0, #(3 << 20)          // CPACR_EL1.FPEN = 0b11
    msr     cpacr_el1, x0
    isb

    // 3. 设置栈指针
    ldr     x0, =_start
    mov     sp, x0

    // 4. 清除 BSS 段 (修正逻辑)
    ldr     x0, =__bss_start
    ldr     w1, =__bss_size
    cbz     w1, run_main            // 如果 BSS 大小为 0，跳过
cbss:
    str     xzr, [x0], #8
    subs    w1, w1, #8
    b.gt    cbss

run_main:
    bl      kernel_main
    b       hang