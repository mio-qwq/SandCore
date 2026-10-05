; =====================================================================
;  SandCore 中断/异常入口桩 (kernel/idt.asm)
;  ---------------------------------------------------------------------
;  IDT 里挂的不是 C 函数而是这里生成的小跳板, 原因:
;    1. CPU 进中断时只有部分寄存器被保存, C 函数会乱动寄存器
;    2. CPU 缺页等异常会压一个"错误码", 其他中断不压 —— 桩负责把
;       队伍站齐(没压的补个 0), 这样栈布局永远一致
;    3. 每个桩把自己的向量号压栈, C 侧一个公共函数按号分发
;
;  【栈布局】(进入公共桩后, 从 ESP 向上):
;    +0  GS   +4 FS   +8 ES   +12 DS     <- 公共桩压的段寄存器
;    +16..47  EDI ESI EBP ESPo EBX EDX ECX EAX   <- pusha
;    +48 中断向量号   +52 错误码(或补的0)
;    +56 EIP  +60 CS  +64 EFLAGS               <- CPU 压的
;    (若从 ring3 进入, CPU 还会先压 SS/ESP —— M1 没有 ring3, 先不管)
;
;  【符号名】C 编译统一 -fno-leading-underscore (见 Makefile),
;  两平台符号同名, 这里直接写原名。表项用 %+ 把 "isr" 和循环变量
;  粘成标号 —— 注意 %+ 只粘紧邻的两块, dd 前面留了空格所以没事。
; =====================================================================
[bits 32]
section .text

global isr_table
extern isr_c_common                ; C: 异常类分发 (interrupts.c)
extern irq_c_common                ; C: 硬件 IRQ 分发 (interrupts.c)
extern sched_pick                  ; C: 抢占调度 (task.c, IRQ 尾部调用)

; ---------------------------------------------------------------------
; 异常桩宏: %1 = 向量号, %2 = 该号是否带 CPU 错误码
; ---------------------------------------------------------------------
%macro ISR_STUB 2
    align 16
global isr%1
isr%1:
%if %2
    ; CPU 已压错误码, 不用补
%else
    push dword 0                     ; 补 0 占位, 对齐栈布局
%endif
    push dword %1                    ; 压向量号
    jmp  isr_common
%endmacro

; 0-7, 9, 15-31: 无错误码;  8, 10-14, 17: 带错误码 (Intel 手卷 3.12)
ISR_STUB  0, 0
ISR_STUB  1, 0
ISR_STUB  2, 0
ISR_STUB  3, 0
ISR_STUB  4, 0
ISR_STUB  5, 0
ISR_STUB  6, 0
ISR_STUB  7, 0
ISR_STUB  8, 1                    ; Double Fault
ISR_STUB  9, 0
ISR_STUB 10, 1                    ; Invalid TSS
ISR_STUB 11, 1                    ; Segment Not Present
ISR_STUB 12, 1                    ; Stack Fault
ISR_STUB 13, 1                    ; General Protection Fault
ISR_STUB 14, 1                    ; Page Fault
ISR_STUB 15, 0
ISR_STUB 16, 0
ISR_STUB 17, 1                    ; Alignment Check
%assign v 18
%rep 14                           ; 18..31 (SSE/虚拟化等, 内核暂时用不到)
ISR_STUB v, 0
%assign v v+1
%endrep

; ---------------------------------------------------------------------
; IRQ 桩: 外设中断经 PIC 重映射到向量 32..47
; ---------------------------------------------------------------------
%macro IRQ_STUB 1
    align 16
global irq%1
irq%1:
    push dword 0
    push dword 32 + %1               ; 重映射后的向量号
    jmp  irq_common
%endmacro

IRQ_STUB  0                        ; 32: PIT 时钟
IRQ_STUB  1                        ; 33: PS/2 键盘
IRQ_STUB  2                        ; 34: 级联
IRQ_STUB  3
IRQ_STUB  4
IRQ_STUB  5
IRQ_STUB  6
IRQ_STUB  7
IRQ_STUB  8
IRQ_STUB  9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12                        ; 44: PS/2 鼠标
IRQ_STUB 13
IRQ_STUB 14                        ; 46: 主 ATA
IRQ_STUB 15                        ; 47: 从 ATA

; ---------------------------------------------------------------------
; 系统调用/保留区桩: 向量 48..124
;   0x7C(124) = SandCall 系统调用门 (attr=0xEE, 用户态可触发)
; ---------------------------------------------------------------------
%assign v 48
%rep 77
ISR_STUB v, 0
%assign v v+1
%endrep

; ---------------------------------------------------------------------
; 公共出口: 存现场 -> 段寄存器归位 -> 调 C 分发 -> 调度器 -> 还现场
; ---------------------------------------------------------------------
isr_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10                    ; 数据段统一归位 (未来 ring3 后这里
    mov ds, ax                      ; 要按帧里存的 CS 判断, M1 固定内核段)
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld                             ; C 代码假定 DF=0 (rep 方向)
    mov eax, [esp + 48]             ; 取向量号 (布局见文件头)
    push esp                        ; 第 2 参: 现场指针 (系统调用要用)
    push eax                        ; 第 1 参: 向量号
    call isr_c_common
    add esp, 8
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                      ; 丢掉 向量号+错误码
    iretd

irq_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
    mov eax, [esp + 48]
    push eax
    call irq_c_common
    add esp, 4
    ; ---- 抢占调度 (M5): 换 esp 到下一个任务的现场栈 ----
    push esp                        ; 当前恢复点
    call sched_pick                 ; 返回下一个任务的恢复点
    add esp, 4
    mov esp, eax
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iretd

; ---------------------------------------------------------------------
; 桩地址表: interrupts.c 建 IDT 时按 0..124 顺序取用
; ---------------------------------------------------------------------
align 4
isr_table:
%assign v 0
%rep 32
    dd isr %+ v
%assign v v+1
%endrep
%assign v 0
%rep 16
    dd irq %+ v
%assign v v+1
%endrep
%assign v 48
%rep 77
    dd isr %+ v
%assign v v+1
%endrep

; ---------------------------------------------------------------------
; GDT/TSS 重载 (task.c 重建 GDT 后用)
; ---------------------------------------------------------------------
global gdt_flush
gdt_flush:                         ; 入参: [esp+4] = 6 字节 gdtr 镜像地址
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10                   ; 重载数据段
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush                ; 远跳刷新 CS
.flush:
    ret

global tss_flush
tss_flush:                         ; TR 装载 TSS 选择子 0x28
    mov ax, 0x28
    ltr ax
    ret
