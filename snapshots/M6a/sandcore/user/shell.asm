; ============================================================
;  SandShell —— SandCore 的 Shell (ring3 用户程序)
;  ------------------------------------------------------------
;  M6 起.shell 不再是内核的一部分: 它就是一个普通的三环程序,
;  经系统调用开窗口 / 读键盘 / 画文字 / 列文件 / 启动别的程序。
;  内核从此不知道 "shell" 的存在 —— 这就是可替换的桌面体验。
; ============================================================
[bits 32]
[org 0x400000]

%define SYS_EXIT    0x00
%define SYS_PUTS    0x01
%define SYS_WINOPEN 0x10
%define SYS_TXT     0x12
%define SYS_FILL    0x13
%define SYS_GETKEY  0x14
%define SYS_FSLIST  0x32

%define C_BG    37           ; PAL_CON_BG
%define C_FG    35           ; PAL_TITLE
%define C_TINT  38           ; PAL_CON_TINT
%define C_WARN  39           ; PAL_CON_WARN

%define W_W 306
%define W_H 166
%define COLS 38

_start:
    mov eax, SYS_WINOPEN
    mov ebx, wtitle
    mov ecx, W_W
    mov edx, W_H
    int 0x7C
    mov [win], eax
    test eax, eax
    js .die

    ; 画布铺底色
    mov eax, SYS_FILL
    mov ebx, [win]
    xor ecx, ecx
    xor edx, edx
    mov esi, W_W - 2
    mov edi, W_H - 14
    mov ebp, C_BG
    int 0x7C

    mov eax, SYS_TXT
    mov ebx, [win]
    mov ecx, 4
    mov edx, 4
    mov esi, b1
    mov edi, C_TINT
    int 0x7C
    mov eax, SYS_TXT
    mov ebx, [win]
    mov ecx, 4
    mov edx, 14
    mov esi, b2
    mov edi, C_TINT
    int 0x7C

    mov dword [row], 4          ; 文字行游标 (像素 y)

.main:
    mov eax, SYS_TXT            ; 提示符
    mov ebx, [win]
    mov ecx, 4
    mov edx, [row]
    mov esi, prompt
    mov edi, C_TINT
    int 0x7C

    mov dword [col], 4 + 10 * 8 ; 提示符后的光标列
    mov dword [llen], 0

.readline:
    mov eax, SYS_GETKEY
    int 0x7C
    cmp eax, -1
    je .readline
    cmp eax, 13                 ; 回车
    je .execute
    cmp eax, 8                  ; 退格
    je .backsp
    cmp eax, 32
    jb .readline
    cmp eax, 126
    ja .readline
    ; 可打印: 存进行缓冲 + 回显 (2 字节临时串)
    mov edx, [llen]
    cmp edx, 62
    jae .readline
    mov [line + edx], al
    mov edx, [llen]
    inc dword [llen]
    mov bl, al
    mov [tmpc], bl
    mov dword [tmpc + 1], 0
    mov eax, SYS_TXT
    mov ebx, [win]
    mov ecx, [col]
    mov edx, [row]
    mov esi, tmpc
    mov edi, C_FG
    int 0x7C
    add dword [col], 8
    jmp .readline

.backsp:
    cmp dword [llen], 0
    je .readline
    dec dword [llen]
    sub dword [col], 8
    mov dword [tmpc], ' '
    mov dword [tmpc + 1], 0
    mov eax, SYS_TXT
    mov ebx, [win]
    mov ecx, [col]
    mov edx, [row]
    mov esi, tmpc
    mov edi, C_BG
    int 0x7C
    jmp .readline

.execute:
    mov eax, [llen]
    mov [line + eax], byte 0
    call newline
    call dispatch
    jmp .main

.die:
    mov eax, SYS_EXIT
    int 0x7C

; ---------------- 换行 (行尾滚动: 越界则整屏重铺) ----------------
newline:
    add dword [row], 10
    mov eax, W_H - 14 - 10
    cmp [row], eax
    jb .nl_ok
    mov eax, SYS_FILL
    mov ebx, [win]
    xor ecx, ecx
    xor edx, edx
    mov esi, W_W - 2
    mov edi, W_H - 14
    mov ebp, C_BG
    int 0x7C
    mov dword [row], 4
.nl_ok:
    ret

; ---------------- 命令分发 ----------------
dispatch:
    mov esi, line
    mov edi, c_help
    call match
    test eax, eax
    jnz near do_help
    mov edi, c_clear
    call match
    test eax, eax
    jnz near do_clear
    mov edi, c_echo
    call match
    test eax, eax
    jnz near do_echo
    mov edi, c_ls
    call match
    test eax, eax
    jnz near do_ls
    mov edi, c_run
    call match
    test eax, eax
    jnz near do_run
    mov esi, c_unknown
    call out_str
    call newline
    ret

; line 前缀与 edi 比较 (到 edi 的 0 为止), 相等 eax=1
match:
    xor eax, eax
    mov esi, line
.m:
    mov bl, [edi]
    test bl, bl
    jz .done
    mov al, [esi]
    inc esi
    inc edi
    cmp al, bl
    je .m
    xor eax, eax
.done:
    ret

; ---------------- 各命令 ----------------
do_help:
    mov esi, h1
    call out_str
    call newline
    mov esi, h2
    call out_str
    call newline
    ret

do_clear:
    mov eax, SYS_FILL
    mov ebx, [win]
    xor ecx, ecx
    xor edx, edx
    mov esi, W_W - 2
    mov edi, W_H - 14
    mov ebp, C_BG
    int 0x7C
    mov dword [row], 4
    ret

do_echo:
    mov esi, line + 4           ; 跳过 "echo"
    call out_str
    call newline
    ret

do_ls:
    mov eax, SYS_FSLIST
    mov ebx, fsbuf
    mov edx, 1024
    mov ecx, edx
    int 0x7C
    mov esi, fsbuf
    call out_str
    call newline
    ret

do_run:
    ; line+4 起 = 程序路径, 经 SYS_EXEC 启动新任务
    mov eax, 0x40               ; SYS_EXEC
    mov ebx, line + 4
    int 0x7C
    cmp eax, 0
    jge .ok
    mov esi, r_fail
    call out_str
    call newline
    ret
.ok:
    mov esi, r_ok
    call out_str
    call newline
    ret

; ---------------- 输出辅助 ----------------
out_str:
    mov eax, SYS_TXT
    mov ebx, [win]
    mov ecx, 4
    mov edx, [row]
    mov edi, C_FG
    int 0x7C
    call newline
    ret

; ---------------- 数据 ----------------
wtitle: db "sh3ll", 0
b1:     db "SandShell v0.1 (ring3 app)", 0
b2:     db "commands: help clear echo ls run", 0
prompt: db "sandcore> ", 0
c_help: db "help", 0
c_clear: db "clear", 0
c_echo: db "echo ", 0
c_ls:   db "ls", 0
c_run:  db "run ", 0
c_unknown: db "unknown (try help)", 0
h1:     db "built-in: help clear echo ls run", 0
h2:     db "run <scx> starts a program", 0
r_ok:   db "started", 0
r_fail: db "exec failed", 0
tmpc:   dd 0
win:    dd 0
row:    dd 4
col:    dd 4
llen:   dd 0
fsbuf:  times 512 db 0
line:   times 64 db 0
