; =====================================================================
; mio：用户态 C 程序的最小入口与 cdecl 系统调用桥。
; CPU 从 SCX entry 进入时没有 C 运行库：不能指望 CRT 清 BSS 或调用 main。
; 加载器负责零初始化，入口只调用应用 main，再以 SYS_EXIT 结束任务。
; sc_call 保留 cdecl 规定的 EBX/ESI/EDI/EBP，把七个栈参数装入 ABI 寄存器。
; 这样 EBP 可以携带 FILL 的颜色，C 编译器无需依赖特殊寄存器分配技巧。
; =====================================================================
[bits 32]
section .text
global _start
global sc_call
extern main
_start:
    call main
    mov ebx, eax                  ; M7：main 返回码传给父进程，IDE 可等待编译结果
    xor eax, eax
    int 0x7C
.end:
    jmp .end
sc_call:
    push ebx
    push esi
    push edi
    push ebp
    ; 返回地址原位于 ESP+0，四次 push 后成为 +16，首参从 +20 开始。
    mov eax, [esp+20]
    mov ebx, [esp+24]
    mov ecx, [esp+28]
    mov edx, [esp+32]
    mov esi, [esp+36]
    mov edi, [esp+40]
    mov ebp, [esp+44]
    int 0x7C
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
