; =====================================================================
; start.asm —— ext 安装包用户程序入口（约定与主项目 user/start.asm 一致）
; SCX entry 进入时没有 C 运行库：加载器负责清 BSS，入口只调用
; main 并以 SYS_EXIT 结束任务；main 返回码经 EBX 交给父进程，
; 这样安装器/Shell 能等待子程序并读取退出码。
; 系统调用桥不在汇编里：SCAPI.H 的 sc_call 内联桥带完整 cdecl
; 保存/恢复，宿主 GCC 直接内联展开效率更好。
; =====================================================================
[bits 32]
section .text
global _start
extern main
_start:
    call main
%ifdef WIN_COFF
; MinGW/COFF 下 GCC 会在 main 序言发出 call __main（CRT 构造桩）；
; freestanding 没有构造列表，这里提供空桩即可链接。
global __main
__main:
    ret
%endif
    mov ebx, eax                  ; main 返回码 -> 父进程可见的退出码
    xor eax, eax                  ; EAX=0x00 SYS_EXIT
    int 0x7C
.end:
    jmp .end                      ; EXIT 不返回；此处兜底防恶意内核变化
