.intel_syntax noprefix
.text
.global _start
_start:
punpckhbw xmm5,xmm3
{disp32} punpckhbw xmm5,[esp+128]
{disp32} punpckhbw xmm5,[ebp-128]
punpckhbw xmm5,[0x1941b0]
punpckhwd xmm6,xmm6
{disp32} punpckhwd xmm6,[esp+128]
{disp32} punpckhwd xmm6,[ebp-128]
punpckhwd xmm6,[0x1941b0]
punpckhdq xmm7,xmm1
{disp32} punpckhdq xmm7,[esp+128]
{disp32} punpckhdq xmm7,[ebp-128]
punpckhdq xmm7,[0x1941b0]
punpckhqdq xmm0,xmm4
{disp32} punpckhqdq xmm0,[esp+128]
{disp32} punpckhqdq xmm0,[ebp-128]
punpckhqdq xmm0,[0x1941b0]
movdqa xmm7,xmm0
{disp32} movdqa xmm2,[esi+512]
{disp32} movdqa [esp-512],xmm4
movdqu xmm7,xmm0
{disp32} movdqu xmm2,[esi+512]
{disp32} movdqu [esp-512],xmm4
movd xmm4,eax
movd edx,xmm6
{disp32} movd xmm1,[edi+256]
{disp32} movd [ebp-256],xmm5
pmovmskb ecx,xmm7
pshufd xmm2,xmm7,255
{disp32} pshufd xmm3,[esp+1024],0
psllw xmm5,0
psllw xmm7,255
psllw xmm3,xmm6
{disp32} psllw xmm1,[ebp+256]
pslld xmm5,0
pslld xmm7,255
pslld xmm3,xmm6
{disp32} pslld xmm1,[ebp+256]
psllq xmm5,0
psllq xmm7,255
psllq xmm3,xmm6
{disp32} psllq xmm1,[ebp+256]
psrlw xmm5,0
psrlw xmm7,255
psrlw xmm3,xmm6
{disp32} psrlw xmm1,[ebp+256]
psrld xmm5,0
psrld xmm7,255
psrld xmm3,xmm6
{disp32} psrld xmm1,[ebp+256]
psrlq xmm5,0
psrlq xmm7,255
psrlq xmm3,xmm6
{disp32} psrlq xmm1,[ebp+256]
psraw xmm5,0
psraw xmm7,255
psraw xmm3,xmm6
{disp32} psraw xmm1,[ebp+256]
psrad xmm5,0
psrad xmm7,255
psrad xmm3,xmm6
{disp32} psrad xmm1,[ebp+256]
pslldq xmm5,0
pslldq xmm7,255
psrldq xmm5,0
psrldq xmm7,255
ret
