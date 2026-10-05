.intel_syntax noprefix
.text
.global _start
_start:
{disp32} pcmpeqd xmm6,[ebp-128]
pcmpeqd xmm6,[0x1941b0]
pcmpgtb xmm7,xmm1
{disp32} pcmpgtb xmm7,[esp+128]
{disp32} pcmpgtb xmm7,[ebp-128]
pcmpgtb xmm7,[0x1941b0]
pcmpgtw xmm0,xmm4
{disp32} pcmpgtw xmm0,[esp+128]
{disp32} pcmpgtw xmm0,[ebp-128]
pcmpgtw xmm0,[0x1941b0]
pcmpgtd xmm1,xmm7
{disp32} pcmpgtd xmm1,[esp+128]
{disp32} pcmpgtd xmm1,[ebp-128]
pcmpgtd xmm1,[0x1941b0]
pmullw xmm2,xmm2
{disp32} pmullw xmm2,[esp+128]
{disp32} pmullw xmm2,[ebp-128]
pmullw xmm2,[0x1941b0]
pmulhw xmm3,xmm5
{disp32} pmulhw xmm3,[esp+128]
{disp32} pmulhw xmm3,[ebp-128]
pmulhw xmm3,[0x1941b0]
pmulhuw xmm4,xmm0
{disp32} pmulhuw xmm4,[esp+128]
{disp32} pmulhuw xmm4,[ebp-128]
pmulhuw xmm4,[0x1941b0]
pmuludq xmm5,xmm3
{disp32} pmuludq xmm5,[esp+128]
{disp32} pmuludq xmm5,[ebp-128]
pmuludq xmm5,[0x1941b0]
pmaddwd xmm6,xmm6
{disp32} pmaddwd xmm6,[esp+128]
{disp32} pmaddwd xmm6,[ebp-128]
pmaddwd xmm6,[0x1941b0]
pminsw xmm7,xmm1
{disp32} pminsw xmm7,[esp+128]
{disp32} pminsw xmm7,[ebp-128]
pminsw xmm7,[0x1941b0]
pmaxsw xmm0,xmm4
{disp32} pmaxsw xmm0,[esp+128]
{disp32} pmaxsw xmm0,[ebp-128]
pmaxsw xmm0,[0x1941b0]
pminub xmm1,xmm7
{disp32} pminub xmm1,[esp+128]
{disp32} pminub xmm1,[ebp-128]
pminub xmm1,[0x1941b0]
pmaxub xmm2,xmm2
{disp32} pmaxub xmm2,[esp+128]
{disp32} pmaxub xmm2,[ebp-128]
pmaxub xmm2,[0x1941b0]
pavgb xmm3,xmm5
{disp32} pavgb xmm3,[esp+128]
{disp32} pavgb xmm3,[ebp-128]
pavgb xmm3,[0x1941b0]
pavgw xmm4,xmm0
{disp32} pavgw xmm4,[esp+128]
{disp32} pavgw xmm4,[ebp-128]
pavgw xmm4,[0x1941b0]
psadbw xmm5,xmm3
{disp32} psadbw xmm5,[esp+128]
{disp32} psadbw xmm5,[ebp-128]
psadbw xmm5,[0x1941b0]
packsswb xmm6,xmm6
{disp32} packsswb xmm6,[esp+128]
{disp32} packsswb xmm6,[ebp-128]
packsswb xmm6,[0x1941b0]
packssdw xmm7,xmm1
{disp32} packssdw xmm7,[esp+128]
{disp32} packssdw xmm7,[ebp-128]
packssdw xmm7,[0x1941b0]
packuswb xmm0,xmm4
{disp32} packuswb xmm0,[esp+128]
{disp32} packuswb xmm0,[ebp-128]
packuswb xmm0,[0x1941b0]
punpcklbw xmm1,xmm7
{disp32} punpcklbw xmm1,[esp+128]
{disp32} punpcklbw xmm1,[ebp-128]
punpcklbw xmm1,[0x1941b0]
punpcklwd xmm2,xmm2
{disp32} punpcklwd xmm2,[esp+128]
{disp32} punpcklwd xmm2,[ebp-128]
punpcklwd xmm2,[0x1941b0]
punpckldq xmm3,xmm5
{disp32} punpckldq xmm3,[esp+128]
{disp32} punpckldq xmm3,[ebp-128]
punpckldq xmm3,[0x1941b0]
punpcklqdq xmm4,xmm0
{disp32} punpcklqdq xmm4,[esp+128]
{disp32} punpcklqdq xmm4,[ebp-128]
punpcklqdq xmm4,[0x1941b0]
ret
