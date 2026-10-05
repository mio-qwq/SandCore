/* SandAsm的Intel顺序整数SSE2编码。寄存器和内存类型分开解析，
 * 禁止把xmm3误当ebx；两遍一律使用disp32，前向符号不改变长度。
 * aligned指令的运行地址由调用者保证，工具不悄悄换成其它指令。 */
typedef struct {int kind,reg,base;u32 displacement;} vector_operand;
static vector_operand vector_argument(void)
{
    vector_operand a={0,0,-1,0};space();
    if(*cursor=='['){
        cursor++;space();a.kind=3;char *saved=cursor,name[32];identifier(name);
        const char *general[]={"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
        for(int i=0;i<8;i++)if(equal(name,general[i]))a.base=i;
        if(a.base<0){cursor=saved;a.displacement=value();}
        else {space();if(*cursor=='+' || *cursor=='-'){int minus=*cursor++=='-';u32 d=value();a.displacement=minus?0u-d:d;}}
        space();if(*cursor!=']')fail("SSE2 memory needs closing bracket");else cursor++;return a;
    }
    char name[32];identifier(name);
    if(name[0]=='x' && name[1]=='m' && name[2]=='m' && name[3]>='0' && name[3]<='7' && !name[4]){
        a.kind=2;a.reg=name[3]-'0';return a;}
    const char *general[]={"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
    for(int i=0;i<8;i++)if(equal(name,general[i])){a.kind=1;a.reg=i;return a;}
    fail("expected xmm/register/memory operand");return a;
}
static void vector_modrm(int regfield,vector_operand a)
{
    if(a.kind!=3){emit(0xC0|(regfield<<3)|a.reg);return;}
    if(a.base<0){emit((regfield<<3)|5);emit32(a.displacement);return;}
    emit(0x80|(regfield<<3)|a.base);if(a.base==4)emit(0x24);emit32(a.displacement);
}
static int vector_instruction(const char *op)
{
    static const struct {const char *name;u8 opcode;} binary[]={
        {"paddb",0xFC},{"paddw",0xFD},{"paddd",0xFE},{"paddq",0xD4},
        {"psubb",0xF8},{"psubw",0xF9},{"psubd",0xFA},{"psubq",0xFB},
        {"paddsb",0xEC},{"paddsw",0xED},{"paddusb",0xDC},{"paddusw",0xDD},
        {"psubsb",0xE8},{"psubsw",0xE9},{"psubusb",0xD8},{"psubusw",0xD9},
        {"pand",0xDB},{"pandn",0xDF},{"por",0xEB},{"pxor",0xEF},
        {"pcmpeqb",0x74},{"pcmpeqw",0x75},{"pcmpeqd",0x76},
        {"pcmpgtb",0x64},{"pcmpgtw",0x65},{"pcmpgtd",0x66},
        {"pmullw",0xD5},{"pmulhw",0xE5},{"pmulhuw",0xE4},{"pmuludq",0xF4},{"pmaddwd",0xF5},
        {"pminsw",0xEA},{"pmaxsw",0xEE},{"pminub",0xDA},{"pmaxub",0xDE},
        {"pavgb",0xE0},{"pavgw",0xE3},{"psadbw",0xF6},
        {"packsswb",0x63},{"packssdw",0x6B},{"packuswb",0x67},
        {"punpcklbw",0x60},{"punpcklwd",0x61},{"punpckldq",0x62},{"punpcklqdq",0x6C},
        {"punpckhbw",0x68},{"punpckhwd",0x69},{"punpckhdq",0x6A},{"punpckhqdq",0x6D}
    };
    for(u32 i=0;i<sizeof(binary)/sizeof(binary[0]);i++)if(equal(op,binary[i].name)){
        vector_operand d=vector_argument();comma();vector_operand s=vector_argument();
        if(d.kind!=2 || (s.kind!=2 && s.kind!=3))fail("SSE2 needs xmm, xmm/memory");
        emit(0x66);emit(0x0F);emit(binary[i].opcode);vector_modrm(d.reg,s);return 1;
    }
    if(equal(op,"movdqa") || equal(op,"movdqu")){
        vector_operand d=vector_argument();comma();vector_operand s=vector_argument();
        int store=d.kind==3;if((store && s.kind!=2) || (!store && (d.kind!=2 || (s.kind!=2 && s.kind!=3))))fail("invalid vector move operands");
        emit(equal(op,"movdqa")?0x66:0xF3);emit(0x0F);emit(store?0x7F:0x6F);vector_modrm(store?s.reg:d.reg,store?d:s);return 1;
    }
    if(equal(op,"movd")){
        vector_operand d=vector_argument();comma();vector_operand s=vector_argument();int store=d.kind!=2;
        if((store && (s.kind!=2 || (d.kind!=1 && d.kind!=3))) || (!store && (s.kind!=1 && s.kind!=3)))fail("movd needs one xmm and one integer operand");
        emit(0x66);emit(0x0F);emit(store?0x7E:0x6E);vector_modrm(store?s.reg:d.reg,store?d:s);return 1;
    }
    if(equal(op,"pmovmskb")){
        vector_operand d=vector_argument();comma();vector_operand s=vector_argument();
        if(d.kind!=1 || s.kind!=2)fail("pmovmskb needs general register, xmm");
        emit(0x66);emit(0x0F);emit(0xD7);vector_modrm(d.reg,s);return 1;
    }
    if(equal(op,"pshufd")){
        vector_operand d=vector_argument();comma();vector_operand s=vector_argument();comma();u32 n=value();
        if(d.kind!=2 || (s.kind!=2 && s.kind!=3) || n>255)fail("invalid pshufd operands");
        emit(0x66);emit(0x0F);emit(0x70);vector_modrm(d.reg,s);emit(n);return 1;
    }
    static const struct {const char *name;u8 opcode,extension,variable;} shifts[]={
        {"psllw",0x71,6,0xF1},{"pslld",0x72,6,0xF2},{"psllq",0x73,6,0xF3},
        {"psrlw",0x71,2,0xD1},{"psrld",0x72,2,0xD2},{"psrlq",0x73,2,0xD3},
        {"psraw",0x71,4,0xE1},{"psrad",0x72,4,0xE2},{"pslldq",0x73,7,0},{"psrldq",0x73,3,0}
    };
    for(u32 i=0;i<sizeof(shifts)/sizeof(shifts[0]);i++)if(equal(op,shifts[i].name)){
        vector_operand d=vector_argument();comma();space();if(d.kind!=2)fail("shift target must be xmm");
        emit(0x66);emit(0x0F);
        if(*cursor=='[' || (*cursor=='x' && cursor[1]=='m')){
            vector_operand s=vector_argument();if(!shifts[i].variable || (s.kind!=2 && s.kind!=3))fail("invalid variable shift");
            emit(shifts[i].variable);vector_modrm(d.reg,s);
        }else {u32 n=value();if(n>255)fail("shift immediate exceeds byte");emit(shifts[i].opcode);vector_modrm(shifts[i].extension,d);emit(n);}
        return 1;
    }return 0;
}
