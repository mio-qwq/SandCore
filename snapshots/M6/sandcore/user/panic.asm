; mio：验收专用程序，用真实 ring3 除零验证异常红屏与向量排版。
[bits 32]
[org 0x400000]
    xor edx, edx
    mov eax, 1
    xor ecx, ecx
    div ecx
