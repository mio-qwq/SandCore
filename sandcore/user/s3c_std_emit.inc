/* ABI4变参和ROOT私有双栈adapter的机器正文。只访问本进程地址，
 * 不增加syscall，不把公开stdio或正常程序变成内联系统调用。 */
static void std_read(int reg,int base,int offset)
{byte(0x8B);memory(reg,base,offset);}
static void std_write(int reg,int base,int offset)
{byte(0x89);memory(reg,base,offset);}
static void std_word(int base,int offset,u32 value)
{byte(0xC7);memory(0,base,offset);dword(value);}
static void emit_standard_value(Node *node)
{
    address(node->a);
    if(node->kind==N_VA_START){
        move_reg(2,0);byte(0x8D);memory(0,5,node->value);std_write(0,2,0);
    }else if(node->kind==N_VA_ARG){
        /* ap地址、原cursor和新cursor各保一次；ap即使有副作用也只求一次。 */
        move_reg(2,0);std_read(0,0,0);byte(0x8D);memory(1,0,4);
        std_write(1,2,0);std_read(0,0,0);
    }else if(node->kind==N_VA_COPY){
        byte(0x50);emit_expression(node->b);byte(0x5A);std_write(0,2,0);
    }else std_word(0,0,0);
}
static void emit_root_run(void)
{
    byte(0x55);byte(0x53);byte(0x56);byte(0x57);byte(0xFC);
    std_read(2,4,20);move_reg(0,4);std_write(0,2,0);
    for(int i=0;i<4;i++){std_read(0,4,12-i*4);std_write(0,2,4+i*4);}
    std_read(0,4,28);std_write(0,2,28);std_read(0,4,32);std_write(0,2,32);
    /* call/pop得到当前实际PC，加相对标签差；SCL换基址无需绝对补丁。 */
    byte(0xE8);dword(0);int pc=code_used;byte(0x58);byte(0x05);int delta=code_used;dword(0);
    std_write(0,2,20);move_reg(6,2);std_word(6,44,1);std_word(6,40,0);
    std_read(1,4,24);byte(0x83);byte(0xE4);byte(0xF0);byte(0xFF);byte(0xD1);
    std_write(0,6,36);std_word(6,40,0);std_word(6,44,0);
    int landing=code_used;put32(code+36+delta,(u32)(landing-pc));byte(0xFC);
    std_read(0,6,24);test_eax();int supplied=jump(5);std_read(0,6,0);patch(supplied,code_used);
    move_reg(4,0);byte(0x83);byte(0xE4);byte(0xF0);byte(0x83);byte(0xEC);byte(12);
    byte(0xFF);memory(6,6,32);byte(0xFF);memory(2,6,28);
    /* 直到所有C finisher结束才触原ROOT栈，应用setvbuf自动缓冲不被覆盖。 */
    std_read(4,6,0);byte(0x5F);byte(0x5E);byte(0x5B);byte(0x5D);byte(0xFC);byte(0xC3);
}
static void emit_root_unwind(void)
{
    std_read(2,4,4);byte(0x85);byte(0xD2);int null=jump(4);
    byte(0x83);memory(7,2,44);byte(1);int unarmed=jump(5);
    byte(0x83);memory(7,2,24);byte(0);int no_stack=jump(4);
    move_reg(6,2);std_word(6,44,0);std_word(6,40,1);
    std_read(5,6,4);std_read(3,6,8);std_read(7,6,16);byte(0xFC);
    byte(0xFF);memory(4,6,20);
    int invalid=code_used;patch(null,invalid);patch(unarmed,invalid);patch(no_stack,invalid);
    byte(0x0F);byte(0x0B);
    int retry=jump(-1);patch(retry,invalid);
}
static void emit_standard_adapters(void)
{
    for(int i=0;i<2;i++)if(root_adapter_used[i]){
        int symbol=root_adapter_symbols[i];
        if(!symbol || symbols[symbol].defined)fatal("reserved root adapter declaration is missing or has a body");
        symbols[symbol].defined=1;symbols[symbol].value=code_used;
        map_function(symbol,BASE+code_used,0);
        if(!i)emit_root_run();else emit_root_unwind();
        if(shared_output || dynamic_output)scl_symbol_sizes[symbol]=code_used-symbols[symbol].value;
    }
}
