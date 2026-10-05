.intel_syntax noprefix
.text
.global _start
_start:
paddb xmm0,xmm4
{disp32} paddb xmm0,[esp+128]
{disp32} paddb xmm0,[ebp-128]
paddb xmm0,[0x1941b0]
paddw xmm1,xmm7
{disp32} paddw xmm1,[esp+128]
{disp32} paddw xmm1,[ebp-128]
paddw xmm1,[0x1941b0]
paddd xmm2,xmm2
{disp32} paddd xmm2,[esp+128]
{disp32} paddd xmm2,[ebp-128]
paddd xmm2,[0x1941b0]
paddq xmm3,xmm5
{disp32} paddq xmm3,[esp+128]
{disp32} paddq xmm3,[ebp-128]
paddq xmm3,[0x1941b0]
psubb xmm4,xmm0
{disp32} psubb xmm4,[esp+128]
{disp32} psubb xmm4,[ebp-128]
psubb xmm4,[0x1941b0]
psubw xmm5,xmm3
{disp32} psubw xmm5,[esp+128]
{disp32} psubw xmm5,[ebp-128]
psubw xmm5,[0x1941b0]
psubd xmm6,xmm6
{disp32} psubd xmm6,[esp+128]
{disp32} psubd xmm6,[ebp-128]
psubd xmm6,[0x1941b0]
psubq xmm7,xmm1
{disp32} psubq xmm7,[esp+128]
{disp32} psubq xmm7,[ebp-128]
psubq xmm7,[0x1941b0]
paddsb xmm0,xmm4
{disp32} paddsb xmm0,[esp+128]
{disp32} paddsb xmm0,[ebp-128]
paddsb xmm0,[0x1941b0]
paddsw xmm1,xmm7
{disp32} paddsw xmm1,[esp+128]
{disp32} paddsw xmm1,[ebp-128]
paddsw xmm1,[0x1941b0]
paddusb xmm2,xmm2
{disp32} paddusb xmm2,[esp+128]
{disp32} paddusb xmm2,[ebp-128]
paddusb xmm2,[0x1941b0]
paddusw xmm3,xmm5
{disp32} paddusw xmm3,[esp+128]
{disp32} paddusw xmm3,[ebp-128]
paddusw xmm3,[0x1941b0]
psubsb xmm4,xmm0
{disp32} psubsb xmm4,[esp+128]
{disp32} psubsb xmm4,[ebp-128]
psubsb xmm4,[0x1941b0]
psubsw xmm5,xmm3
{disp32} psubsw xmm5,[esp+128]
{disp32} psubsw xmm5,[ebp-128]
psubsw xmm5,[0x1941b0]
psubusb xmm6,xmm6
{disp32} psubusb xmm6,[esp+128]
{disp32} psubusb xmm6,[ebp-128]
psubusb xmm6,[0x1941b0]
psubusw xmm7,xmm1
{disp32} psubusw xmm7,[esp+128]
{disp32} psubusw xmm7,[ebp-128]
psubusw xmm7,[0x1941b0]
pand xmm0,xmm4
{disp32} pand xmm0,[esp+128]
{disp32} pand xmm0,[ebp-128]
pand xmm0,[0x1941b0]
pandn xmm1,xmm7
{disp32} pandn xmm1,[esp+128]
{disp32} pandn xmm1,[ebp-128]
pandn xmm1,[0x1941b0]
por xmm2,xmm2
{disp32} por xmm2,[esp+128]
{disp32} por xmm2,[ebp-128]
por xmm2,[0x1941b0]
pxor xmm3,xmm5
{disp32} pxor xmm3,[esp+128]
{disp32} pxor xmm3,[ebp-128]
pxor xmm3,[0x1941b0]
pcmpeqb xmm4,xmm0
{disp32} pcmpeqb xmm4,[esp+128]
{disp32} pcmpeqb xmm4,[ebp-128]
pcmpeqb xmm4,[0x1941b0]
pcmpeqw xmm5,xmm3
{disp32} pcmpeqw xmm5,[esp+128]
{disp32} pcmpeqw xmm5,[ebp-128]
pcmpeqw xmm5,[0x1941b0]
pcmpeqd xmm6,xmm6
{disp32} pcmpeqd xmm6,[esp+128]
ret
