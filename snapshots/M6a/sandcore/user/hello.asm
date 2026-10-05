; ============================================================
;  hello —— SandCore 用户程序 (ring3)
;  开自己的窗口, 画两行字, 等一次按键, 退出。
;  全部经 int 0x7C 系统调用, 零依赖。
; ============================================================
[bits 32]
[org 0x400000]

%define SYS_EXIT    0x00
%define SYS_WINOPEN 0x10
%define SYS_TXT     0x12
%define SYS_FILL    0x13
%define SYS_GETKEY  0x14

%define C_BG  37
%define C_FG  35
%define C_TINT 38

_start:
    mov eax, SYS_WINOPEN
    mov ebx, wtitle
    mov ecx, 220
    mov edx, 110
    int 0x7C
    mov [win], eax

    mov eax, SYS_FILL
    mov ebx, [win]
    xor ecx, ecx
    xor edx, edx
    mov esi, 218
    mov edi, 96
    mov ebp, C_BG
    int 0x7C

    mov eax, SYS_TXT
    mov ebx, [win]
    mov ecx, 8
    mov edx, 8
    mov esi, m1
    mov edi, C_FG
    int 0x7C
    mov eax, SYS_TXT
    mov ebx, [win]
    mov ecx, 8
    mov edx, 24
    mov esi, m2
    mov edi, C_TINT
    int 0x7C
    mov eax, SYS_TXT
    mov ebx, [win]
    mov ecx, 8
    mov edx, 40
    mov esi, m3
    mov edi, C_FG
    int 0x7C

    mov eax, SYS_GETKEY         ; 等一次按键 (不聚焦就收不到)
    int 0x7C

    mov eax, SYS_EXIT
    int 0x7C

wtitle: db "hello", 0
m1: db "hello from ring3!", 0
m2: db "sandcore says hi to mio", 0
m3: db "press any key to close", 0
win: dd 0
