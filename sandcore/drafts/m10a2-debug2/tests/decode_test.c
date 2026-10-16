#include "D2DECODE.H"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {unsigned char bytes[16];unsigned int count,length;const char *text;ScU32 target,branch;} Case;
static unsigned int assertions;
static void check(int condition,const char *message)
{assertions++;if(!condition){fprintf(stderr,"decode: %s\n",message);exit(1);}}
int main(void)
{
    Case cases[]={
        {{0x55},1,1,"push ebp",0,0},
        {{0x8b,0xec},2,2,"mov ebp, esp",0,0},
        {{0x83,0xec,0x10},3,3,"sub esp, 0x10",0,0},
        {{0x8b,0x44,0x8d,0xfc},4,4,"mov eax, dword [ebp+ecx*4-0x4]",0,0},
        {{0xc7,0x45,0xfc,0x78,0x56,0x34,0x12},7,7,"mov dword [ebp-0x4], 0x12345678",0,0},
        {{0x0f,0x85,0xf6,0xff,0xff,0xff},6,6,"jne 003FFFFC",0x003ffffc,1},
        {{0xf3,0xa5},2,2,"rep movsd",0,0},
        {{0x66,0x01,0xd8},3,3,"add ax, bx",0,0},
        {{0xf3,0x0f,0x1e,0xfb},4,4,"endbr32",0,0},
        {{0xe8,0xfb,0xff,0xff,0xff},5,5,"call 00400000",0x00400000,1},
        {{0x0f,0xbe,0x45,0x10},4,4,"movsx eax, byte [ebp+0x10]",0,0},
        {{0xff,0x55,0x08},3,3,"call dword [ebp+0x8]",0,0},
        {{0x64,0xa1,0,0,0,0},6,6,"mov eax, dword fs:[0x00000000]",0,0},
        {{0x67,0x8b,0},3,1,"db 0x67",0,0},
        {{0x66,0x0f,0xef,0xc0},4,1,"db 0x66",0,0},
        {{0xc7,0xc8,0,0,0,0},6,1,"db 0xC7",0,0},
        {{0x0f,0x0b},2,2,"ud2",0,0},
        {{0xeb,0xfe},2,2,"jmp 00400000",0x00400000,1}
    };
    unsigned char valid[16];memset(valid,1,sizeof(valid));
    for(unsigned int i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
        D2Instruction decoded;D2Decode(cases[i].bytes,valid,cases[i].count,0x400000,&decoded);
        check(decoded.length==cases[i].length,"instruction length");
        if(strcmp(decoded.text,cases[i].text)){fprintf(stderr,"case %u expected [%s], got [%s]\n",i,cases[i].text,decoded.text);return 1;}
        check(decoded.target==cases[i].target,"relative target");check(decoded.branch==cases[i].branch,"branch marker");
        for(unsigned int cut=1;cut<cases[i].length;cut++){
            D2Decode(cases[i].bytes,valid,cut,0x400000,&decoded);check(decoded.length==1 && !decoded.known,"truncated instruction must use one-byte db");
        }
        for(unsigned int bad=0;bad<cases[i].length;bad++){
            valid[bad]=0;D2Decode(cases[i].bytes,valid,cases[i].count,0x400000,&decoded);
            check(decoded.length==1 && !decoded.known,"unreadable instruction byte cannot decode as real zero");valid[bad]=1;
        }
    }
    unsigned char prefix[16];memset(prefix,0x66,sizeof(prefix));D2Instruction decoded;D2Decode(prefix,valid,16,0,&decoded);
    check(decoded.length==1 && !decoded.known,"15-byte maximum");D2Decode(0,0,0,0,&decoded);
    check(!decoded.readable && !strcmp(decoded.text,"<unreadable>"),"unmapped first byte");
    printf("D2_DECODE_PASS cases=%u assertions=%u\n",(unsigned)(sizeof(cases)/sizeof(cases[0])),assertions);return 0;
}
