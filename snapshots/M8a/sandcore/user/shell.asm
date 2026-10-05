; mio：SandShell 是普通 ring3 程序，文字游标由本人窗口维护。
[bits 32]
[org 0x400000]
_start:
    mov eax, 0x10
    mov ebx, title
    mov ecx, 306
    mov edx, 164
    int 0x7C
    test eax, eax
    js exit
    mov [win], eax
    mov ebx, banner
    call puts
prompt_loop:
    mov ebx, prompt
    call puts
    mov dword [length], 0
readline:
    mov eax, 0x14
    int 0x7C
    cmp eax, -1
    je readline
    cmp eax, 10
    je execute
    cmp eax, 13
    je execute
    cmp eax, 8
    je backspace
    cmp eax, 32
    jb readline
    cmp eax, 126
    ja readline
    mov edx, [length]
    ; 内核文字游标支持折行退格；行缓冲仍留一字节给 NUL。
    ; 是否能退格由 length 守卫，不能用屏幕列位置猜测提示符边界。
    ; M8长路径+127B参数可能超过原63B整行。行缓存扩至256B；内核
    ; 仍分别验证路径/参数上限，超限返回错误，不在Shell静默截断。
    cmp edx, 255
    jae readline
    mov [line+edx], al
    inc dword [length]
    mov [one], al
    mov ebx, one
    call puts
    jmp readline
backspace:
    cmp dword [length], 0
    je readline
    dec dword [length]
    mov ebx, erase
    call puts
    jmp readline
execute:
    mov edx, [length]
    mov byte [line+edx], 0
    mov ebx, newline
    call puts
    cmp dword [length], 0
    je prompt_loop
    mov edi, cmd_help
    call match
    test eax, eax
    jnz help
    mov edi, cmd_clear
    call match
    test eax, eax
    jnz clear
    mov edi, cmd_echo
    call match
    test eax, eax
    jnz echo
    mov edi, cmd_ls
    call match
    test eax, eax
    jnz list
    mov edi, cmd_run
    call match
    test eax, eax
    jnz run
    mov edi, cmd_exit
    call match
    test eax, eax
    jnz exit
    ; 未命中内建命令时走 bin/ 命令查找；命令名与参数分开拼接，
    ; 例如 asm home/demo.asm apps/demo.scx -> bin/asm.scx 后跟原参数。
    ; Shell 无需枚举新的用户态命令，asm 与 s3c 都按 bin/ 独立安装。
    mov esi, line
    mov edi, command
    mov dword [edi], 'bin/'
    add edi, 4
.command_name:
    lodsb
    test al, al
    jz .suffix
    cmp al, ' '
    je .suffix
    stosb
    jmp .command_name
.suffix:
    mov dword [edi], '.scx'
    add edi, 4
    mov byte [edi], ' '
    inc edi
    test al, al
    jz .command_end
.arguments:
    lodsb
    stosb
    test al, al
    jnz .arguments
    jmp .launch
.command_end:
    mov byte [edi], 0
.launch:
    mov ebx, command
    mov eax, 0x40
    int 0x7C
    test eax, eax
    js .unknown
    jmp prompt_loop
.unknown:
    mov ebx, unknown
    call puts
    jmp prompt_loop
help:
    mov ebx, help_text
    call puts
    jmp prompt_loop
clear:
    mov eax, 0x13
    mov ebx, [win]
    xor ecx, ecx
    xor edx, edx
    mov esi, 304
    mov edi, 150
    mov ebp, 37                 ; PAL_CON_BG
    int 0x7C
    jmp prompt_loop
echo:
    mov ebx, esi
    call puts
    mov ebx, newline
    call puts
    jmp prompt_loop
list:
    mov eax, 0x32
    mov ebx, files
    mov edx, 4096
    int 0x7C
    mov ebx, files
    call puts
    jmp prompt_loop
run:
    mov ebx, esi
    mov eax, 0x40
    int 0x7C
    test eax, eax
    js .failed
    mov ebx, started
    call puts
    jmp prompt_loop
.failed:
    mov ebx, failed
    call puts
    jmp prompt_loop
exit:
    xor eax, eax
    int 0x7C
    jmp exit
puts:
    mov eax, 1
    int 0x7C
    ret
; 命令必须完整匹配；ESI 返回跳过空格后的参数起点。
match:
    mov esi, line
.loop:
    mov al, [edi]
    test al, al
    jz .boundary
    cmp al, [esi]
    jne .no
    inc edi
    inc esi
    jmp .loop
.boundary:
    cmp byte [esi], 0
    je .yes
    cmp byte [esi], ' '
    jne .no
.spaces:
    cmp byte [esi], ' '
    jne .yes
    inc esi
    jmp .spaces
.yes:
    mov eax, 1
    ret
.no:
    xor eax, eax
    ret
title: db 'SandShell',0
banner: db 'SandShell / mio',10,'help to begin',10,10,0
prompt: db 'sandcore> ',0
newline: db 10,0
erase: db 8,0
one: dw 0
help_text: db 'help clear echo ls run exit',10,'asm <source> <output>',10,'s3c <source> <output>',10,'run apps/hello.scx',10,0
unknown: db 'unknown command (try help)',10,0
started: db 'started',10,0
failed: db 'exec failed',10,0
cmd_help: db 'help',0
cmd_clear: db 'clear',0
cmd_echo: db 'echo',0
cmd_ls: db 'ls',0
cmd_run: db 'run',0
cmd_exit: db 'exit',0
win: dd 0
length: dd 0
line: times 256 db 0
files: times 4096 db 0
; 隐式bin命令在原行之外增加bin/、.scx、空格和NUL，至少需265B。
; 预留272B，不能只扩line而让命令拼接覆盖随后的映像数据。
command: times 272 db 0
