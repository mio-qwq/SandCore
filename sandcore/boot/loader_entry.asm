; 最小启动器只有自己的BSS和临时IDT，正式中断/任务全部由磁盘主核提供。
[bits 32]
section .text
global _loader_start
global loader_jump
global loader_fault
extern loader_main
extern loader_fail
extern __loader_bss_start
extern __loader_bss_end
_loader_start:
    cli
    cld
    mov esp,0x90000
    mov edi,__loader_bss_start
    mov ecx,__loader_bss_end
    sub ecx,edi
    xor eax,eax
    rep stosb
    call loader_main
.halt:
    cli
    hlt
    jmp .halt
loader_jump:
    mov edx,[esp+4]
    mov eax,0x4B424353
    mov ebx,0x8500
    mov esp,0x90000
    cld
    jmp edx
loader_fault:
    ; 无论异常有没有error code都不返回，统一更换可信栈诊断。
    cli
    cld
    mov ax,0x10
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov esp,0x90000
    push dword 9
    call loader_fail
    jmp _loader_start.halt
