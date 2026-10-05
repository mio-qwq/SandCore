; mio：在沙核里用 asm 汇编，再用 run 执行生成的 SCX。
[bits 32]
[org 0x400000]
_start:
mov eax, 0x10
mov ebx, title
mov ecx, 250
mov edx, 120
int 0x7c
mov eax, 1
mov ebx, message
int 0x7c
wait:
mov eax, 0x14
int 0x7c
jmp wait
title: db "Built in SandCore", 0
message: db "hello from SandAsm", 10, "SCX1MIO / mio", 10, 0
