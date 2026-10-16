#include "D2DECODE.H"
#include <string.h>
#include <stdio.h>

typedef struct {const unsigned char *p,*valid;
    ScU32 count,at;
    int bad,width;
    char segment[5];
} D2Cursor;
static const char *d2_r32[8]={"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
static const char *d2_r16[8]={"ax","cx","dx","bx","sp","bp","si","di"};
static const char *d2_r8[8]={"al","cl","dl","bl","ah","ch","dh","bh"};
static const char *d2_cc[16]={"jo","jno","jb","jae","je","jne","jbe","ja","js","jns","jp","jnp","jl","jge","jle","jg"};
static const char *d2_alu[8]={"add","or","adc","sbb","and","sub","xor","cmp"};
static const char *d2_shift[8]={"rol","ror","rcl","rcr","shl","shr",0,"sar"};
static ScU32 d2_take(D2Cursor *c,ScU32 bytes)
{
    if(bytes>c->count-c->at || c->at+bytes>15u){c->bad=1;
        return 0;
    }
    ScU32 value=0;
    for(ScU32 i=0;i<bytes;i++){if(!c->valid[c->at])c->bad=1;
        value|=(ScU32)c->p[c->at++]<<(i*8);
    }
    return value;
}
static const char *d2_reg(ScU32 reg,int width)
{return width==8?d2_r8[reg&7]:width==16?d2_r16[reg&7]:d2_r32[reg&7];
}
static void d2_append(char *out,ScU32 cap,const char *text)
{ScU32 n=(ScU32)strlen(out);
    if(n<cap)snprintf(out+n,cap-n,"%s",text);
}
static void d2_disp(char *out,ScU32 cap,ScU32 value,int signed_value,int have_base)
{
    char text[32];
    if(signed_value && (value&0x80000000u))snprintf(text,sizeof(text),"-0x%X",0u-value);
    else snprintf(text,sizeof(text),have_base?"+0x%X":"0x%X",value);
    d2_append(out,cap,text);
}
static ScU32 d2_operand(D2Cursor *c,int width,char out[96],ScU32 *reg)
{
    ScU32 m=d2_take(c,1),mode=m>>6,rm=m&7;
     *reg=(m>>3)&7;
    if(mode==3){snprintf(out,96,"%s",d2_reg(rm,width));
        return mode;
    }
    snprintf(out,96,"%s%s[",width==8?"byte ":width==16?"word ":"dword ",c->segment);
    int have=0;
    ScU32 sib=0;
    if(rm==4){sib=d2_take(c,1);
        ScU32 base=sib&7,index=(sib>>3)&7,scale=1u<<(sib>>6);
        if(mode || base!=5){d2_append(out,96,d2_r32[base]);
            have=1;
        }
        if(index!=4){char text[32];
            snprintf(text,sizeof(text),have?"+%s*%u":"%s*%u",d2_r32[index],scale);
            d2_append(out,96,text);
            have=1;
        }
        if(!mode && base==5){ScU32 displacement=d2_take(c,4);
            d2_disp(out,96,displacement,0,have);
            have=1;
    }}
    else if(!mode && rm==5){d2_disp(out,96,d2_take(c,4),0,0);
        have=1;
    }
    else{d2_append(out,96,d2_r32[rm]);
        have=1;
    }
    if(mode==1){ScU32 displacement=(ScU32)(ScI32)(signed char)d2_take(c,1);
        if(displacement)d2_disp(out,96,displacement,1,have);
    }
    if(mode==2){ScU32 displacement=d2_take(c,4);
        if(displacement)d2_disp(out,96,displacement,1,have);
    }
    d2_append(out,96,"]");
    return mode;
}
void D2Decode(const unsigned char *bytes,const unsigned char *readable,ScU32 count,ScU32 address,D2Instruction *out)
{
    memset(out,0,sizeof(*out));
    out->address=address;
    out->length=1;
    if(!bytes || !readable || !count || !readable[0]){snprintf(out->bytes,sizeof(out->bytes),"??");
        snprintf(out->text,sizeof(out->text),"<unreadable>");
        return;
    }
    out->readable=1;
    D2Cursor c={bytes,readable,count,0,0,32,{0}};
    const char *name=0;
    char a[96]={0},b[96]={0};
    ScU32 reg=0,op=0,rep=0;
    int lock=0,known=1,modrm=0;
    for(;;){op=d2_take(&c,1);
        if(c.bad)break;
        if(op==0x66){c.width=16;
            continue;
        }
        if(op==0xf2 || op==0xf3){rep=op;
            continue;
        }
        if(op==0xf0){lock=1;
            continue;
        }
        if(op==0x26 || op==0x2e || op==0x36 || op==0x3e || op==0x64 || op==0x65){
            snprintf(c.segment,sizeof(c.segment),"%s:",op==0x26?"es":op==0x2e?"cs":op==0x36?"ss":op==0x3e?"ds":op==0x64?"fs":"gs");
            continue;
        }
        break;
    }
    if(c.bad)known=0;
    else if(op<=0x3d && (op&7)<=5){ScU32 group=op>>3,form=op&7;
        name=d2_alu[group];
        int width=(form&1)?c.width:8;
        if(form<4){modrm=1;
            d2_operand(&c,width,a,&reg);
            snprintf(b,sizeof(b),"%s",d2_reg(reg,width));
            if(form&2){char temp[96];
                memcpy(temp,a,sizeof(temp));
                memcpy(a,b,sizeof(a));
                memcpy(b,temp,sizeof(b));
        }}
        else{snprintf(a,sizeof(a),"%s",d2_reg(0,width));
            snprintf(b,sizeof(b),"0x%X",d2_take(&c,width==8?1u:(ScU32)c.width/8));
    }}
    else if(op>=0x40 && op<=0x5f){name=op<0x48?"inc":op<0x50?"dec":op<0x58?"push":"pop";
        snprintf(a,sizeof(a),"%s",d2_reg(op&7,c.width));
    }
    else if(op==0x60)name=c.width==16?"pusha":"pushad";
    else if(op==0x61)name=c.width==16?"popa":"popad";
    else if(op==0x68 || op==0x6a){name="push";
        ScU32 value=d2_take(&c,op==0x6a?1u:(ScU32)c.width/8);
        if(op==0x6a)value=(ScU32)(ScI32)(signed char)value;
        snprintf(a,sizeof(a),"0x%X",value);
    }
    else if(op==0x69 || op==0x6b){name="imul";
        modrm=1;
        d2_operand(&c,c.width,b,&reg);
        snprintf(a,sizeof(a),"%s",d2_reg(reg,c.width));
        ScU32 value=d2_take(&c,op==0x6b?1u:(ScU32)c.width/8);
        if(op==0x6b)value=(ScU32)(ScI32)(signed char)value;
        char text[32];
        snprintf(text,sizeof(text),", 0x%X",value);
        d2_append(b,sizeof(b),text);
    }
    else if(op>=0x70 && op<=0x7f){name=d2_cc[op&15];
        ScU32 displacement=(ScU32)(ScI32)(signed char)d2_take(&c,1);
        out->target=address+c.at+displacement;
        out->branch=1;
        snprintf(a,sizeof(a),"%08X",out->target);
    }
    else if(op==0x80 || op==0x81 || op==0x83){modrm=1;
        int width=op==0x80?8:c.width;
        d2_operand(&c,width,a,&reg);
        name=d2_alu[reg];
        ScU32 value=d2_take(&c,op==0x81?(ScU32)c.width/8:1u);
        if(op==0x83)value=(ScU32)(ScI32)(signed char)value;
        snprintf(b,sizeof(b),"0x%X",value);
    }
    else if(op>=0x84 && op<=0x8f && op!=0x8c && op!=0x8e){int width=(op&1)?c.width:8;
        modrm=1;
        ScU32 mode=d2_operand(&c,op==0x8d?c.width:width,a,&reg);
        if(op==0x8f){if(reg)known=0;
            else name="pop";
        }
        else{name=op<0x86?"test":op<0x88?"xchg":op==0x8d?"lea":"mov";
            if(op==0x8d && mode==3)known=0;
            snprintf(b,sizeof(b),"%s",d2_reg(reg,op==0x8d?c.width:width));
            if(op==0x8a || op==0x8b || op==0x8d){char temp[96];
                memcpy(temp,a,sizeof(temp));
                memcpy(a,b,sizeof(a));
                memcpy(b,temp,sizeof(b));
    }}}
    else if(op==0x90)name=rep==0xf3?"pause":"nop";
    else if(op>=0x91 && op<=0x97){name="xchg";
        snprintf(a,sizeof(a),"%s",d2_reg(0,c.width));
        snprintf(b,sizeof(b),"%s",d2_reg(op&7,c.width));
    }
    else if(op==0x98)name=c.width==16?"cbw":"cwde";
    else if(op==0x99)name=c.width==16?"cwd":"cdq";
    else if(op==0x9c)name=c.width==16?"pushf":"pushfd";
    else if(op==0x9d)name=c.width==16?"popf":"popfd";
    else if(op>=0xa0 && op<=0xa3){name="mov";
        int width=(op&1)?c.width:8;
        ScU32 absolute=d2_take(&c,4);
        snprintf(a,sizeof(a),"%s",d2_reg(0,width));
        snprintf(b,sizeof(b),"%s%s[0x%08X]",width==8?"byte ":width==16?"word ":"dword ",c.segment,absolute);
        if(op&2){char temp[96];
            memcpy(temp,a,sizeof(temp));
            memcpy(a,b,sizeof(a));
            memcpy(b,temp,sizeof(b));
    }}
    else if(op==0xa4 || op==0xa5)name=(op&1)?c.width==16?"movsw":"movsd":"movsb";
    else if(op==0xa6 || op==0xa7)name=(op&1)?c.width==16?"cmpsw":"cmpsd":"cmpsb";
    else if(op==0xaa || op==0xab)name=(op&1)?c.width==16?"stosw":"stosd":"stosb";
    else if(op==0xac || op==0xad)name=(op&1)?c.width==16?"lodsw":"lodsd":"lodsb";
    else if(op==0xae || op==0xaf)name=(op&1)?c.width==16?"scasw":"scasd":"scasb";
    else if(op==0xa8 || op==0xa9){name="test";
        int width=op==0xa8?8:c.width;
        snprintf(a,sizeof(a),"%s",d2_reg(0,width));
        snprintf(b,sizeof(b),"0x%X",d2_take(&c,(ScU32)width/8));
    }
    else if(op>=0xb0 && op<=0xbf){name="mov";
        int width=op<0xb8?8:c.width;
        snprintf(a,sizeof(a),"%s",d2_reg(op&7,width));
        snprintf(b,sizeof(b),"0x%X",d2_take(&c,(ScU32)width/8));
    }
    else if(op==0xc2 || op==0xc3){name="ret";
        if(op==0xc2)snprintf(a,sizeof(a),"0x%X",d2_take(&c,2));
    }
    else if(op==0xc6 || op==0xc7){name="mov";
        modrm=1;
        int width=op==0xc6?8:c.width;
        d2_operand(&c,width,a,&reg);
        if(reg)known=0;
        snprintf(b,sizeof(b),"0x%X",d2_take(&c,(ScU32)width/8));
    }
    else if(op==0xc9)name="leave";
    else if(op==0xcc)name="int3";
    else if(op==0xcd){name="int";
        snprintf(a,sizeof(a),"0x%X",d2_take(&c,1));
    }
    else if(op==0xc0 || op==0xc1 || (op>=0xd0 && op<=0xd3)){modrm=1;
        int width=(op&1)?c.width:8;
        d2_operand(&c,width,a,&reg);
        name=d2_shift[reg];
        if(op==0xc0 || op==0xc1)snprintf(b,sizeof(b),"%u",d2_take(&c,1));
        else snprintf(b,sizeof(b),op<0xd2?"1":"cl");
    }
    else if(op>=0xe0 && op<=0xe3){static const char *names[4]={"loopne","loope","loop","jecxz"};
        name=names[op-0xe0];
        ScU32 displacement=(ScU32)(ScI32)(signed char)d2_take(&c,1);
        out->target=address+c.at+displacement;
        out->branch=1;
        snprintf(a,sizeof(a),"%08X",out->target);
    }
    else if(op==0xe8 || op==0xe9 || op==0xeb){name=op==0xe8?"call":"jmp";
        ScU32 displacement=d2_take(&c,op==0xeb?1u:(ScU32)c.width/8);
        if(op==0xeb)displacement=(ScU32)(ScI32)(signed char)displacement;
        else if(c.width==16)displacement=(ScU32)(ScI32)(short)displacement;
        out->target=address+c.at+displacement;
        if(c.width==16 && op!=0xeb)out->target&=65535u;
        out->branch=1;
        snprintf(a,sizeof(a),"%08X",out->target);
    }
    else if(op==0xf6 || op==0xf7){static const char *names[8]={"test",0,"not","neg","mul","imul","div","idiv"};
        modrm=1;
        int width=op==0xf6?8:c.width;
        d2_operand(&c,width,a,&reg);
        name=names[reg];
        if(reg==0)snprintf(b,sizeof(b),"0x%X",d2_take(&c,(ScU32)width/8));
    }
    else if(op==0xfe || op==0xff){static const char *names[8]={"inc","dec","call",0,"jmp",0,"push",0};
        modrm=1;
        d2_operand(&c,op==0xfe?8:c.width,a,&reg);
        name=names[reg];
        if(op==0xfe && reg>1)known=0;
    }
    else if(op==0xfc)name="cld";
    else if(op==0xfd)name="std";
    else if(op==0xf8)name="clc";
    else if(op==0xf9)name="stc";
    else if(op==0x0f){ScU32 second=d2_take(&c,1);
        if(second>=0x80 && second<=0x8f){name=d2_cc[second&15];
            ScU32 displacement=d2_take(&c,(ScU32)c.width/8);
            if(c.width==16)displacement=(ScU32)(ScI32)(short)displacement;
            out->target=address+c.at+displacement;
            if(c.width==16)out->target&=65535u;
            out->branch=1;
            snprintf(a,sizeof(a),"%08X",out->target);
        }
        else if(second>=0x90 && second<=0x9f){modrm=1;
            static const char *names[16]={"seto","setno","setb","setae","sete","setne","setbe","seta","sets","setns","setp","setnp","setl","setge","setle","setg"};
            name=names[second&15];
            d2_operand(&c,8,a,&reg);
        }
        else if(second==0xaf || second==0xb6 || second==0xb7 || second==0xbe || second==0xbf){modrm=1;
            name=second==0xaf?"imul":second>=0xbe?"movsx":"movzx";
            d2_operand(&c,second==0xaf?c.width:(second&1)?16:8,b,&reg);
            snprintf(a,sizeof(a),"%s",d2_reg(reg,c.width));
        }
        else if(second>=0xc8 && second<=0xcf && c.width==32){name="bswap";
            snprintf(a,sizeof(a),"%s",d2_r32[second&7]);
        }
        else if(second==0x0b)name="ud2";
        else if(second==0x31)name="rdtsc";
        else if(second==0xa2)name="cpuid";
        else if(second==0x1e && rep==0xf3 && d2_take(&c,1)==0xfb)name="endbr32";
        else known=0;
    }else known=0;
    /* 地址宽度67、SSE/FPU等未实现编码不猜长度。前缀或截断同样只
     * 回退首字节，避免一条误译吞掉后面的真实指令；TF完全独立。 */
    if(rep && op!=0x90 && !(op>=0xa4 && op<=0xaf) && strcmp(name?name:"","endbr32"))known=0;
    if(lock && !modrm)known=0;
    if(!known || !name || c.bad || !c.at){out->branch=0;
        out->known=0;
        out->length=1;
        snprintf(out->text,sizeof(out->text),"db 0x%02X",bytes[0]);
    }
    else{out->length=c.at;
        out->known=1;
        const char *prefix=lock?"lock ":rep && op>=0xa4 && op<=0xaf?(rep==0xf2?"repne ":"rep "):"";
        snprintf(out->text,sizeof(out->text),"%s%s%s%s%s%s",prefix,name,a[0]?" ":"",a,b[0]?", ":"",b);
    }
    ScU32 at=0;
    for(ScU32 i=0;i<out->length && at+3<sizeof(out->bytes);i++)at+=(ScU32)snprintf(out->bytes+at,sizeof(out->bytes)-at,i?" %02X":"%02X",bytes[i]);
}
