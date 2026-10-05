/* =====================================================================
 * mio：SCCC 的原生 x86 后端。这里产生真正的 32 位指令，不解释语法树。
 * 表达式结果在 EAX，暂存使用机器栈；ECX/EDX 是临时寄存器。每个函数
 * 保持cdecl的EBX/ESI/EDI/EBP：有内联汇编时保守保存全部三个，
 * 普通生成代码只用EAX/ECX/EDX，原值自然保留而省掉无效压栈。
 * 绝对地址等代码/数据/BSS 布局全部确定后回填；磁盘保存的只有
 * 代码与初始化数据，BSS 交给 SCX 加载器清零。
 *
 * 分支采用 rel32，故前向/后向跳转不受 8 位位移限制。break/continue/
 * return 链表临时借用跳转位移字段，补齐时立即变成真实位移，不另设
 * 固定数量很小的“每循环 break 数”数组。
 * ===================================================================== */
#define F_REL 1
#define F_ABS 2
#define F_DATA 3
static int return_jumps,control_depth;
static int break_jumps[64],continue_jumps[64],control_loop[64];
static int label_names[128],label_targets[128],label_jumps[128],label_count;
static int label_find(int name)
{
    for(int i=0;i<label_count;i++) if(label_names[i]==name) return i;
    if(label_count>=128) fatal("function label capacity exceeded");
    int i=label_count++;
    label_names[i]=name;
    label_targets[i]=-1;
    label_jumps[i]=-1;
    return i;
}
static void byte(int value)
{
    if(code_used>=CODE_MAX) fatal("machine code capacity exceeded");
    code[36+code_used++]=(u8)value;
}
static void dword(u32 value) { for(int i=0;i<4;i++) { byte((int)(value&255));
        value>>=8; } }
static void put32(u8 *out,u32 value)
{ for(int i=0;i<4;i++) { out[i]=(u8)value;
        value>>=8; } }
static u32 get32(u8 *in) { return (u32)in[0]|((u32)in[1]<<8)|((u32)in[2]<<16)|((u32)in[3]<<24); }
static void fixed(int kind,int symbol,int addend,int offset)
{
    if(fix_count>=FIX_MAX) fatal("relocation capacity exceeded");
    Fix *f=fixes+fix_count++;
    f->kind=kind;
    f->symbol=symbol;
    f->addend=addend;
    f->offset=offset;
}
static void immediate(int reg,u32 value) { byte(0xB8+reg);
    dword(value); }
static void move_reg(int dst,int src) { byte(0x89);
    byte(0xC0+src*8+dst); }
static void stack_add(int n) { if(n) { byte(0x81);
        byte(0xC4);
        dword((u32)n); } }
static void memory(int reg,int base,int displacement)
{
    /* 总用 disp32，避免正负边界的多套编码；ESP 的 SIB 必须显式写。
     * base=-1 为绝对地址形式，调用者可以把位移四字节留给重定位。 */
    if(base<0) { byte(reg*8+5);
        dword((u32)displacement); }
    else { byte(0x80+reg*8+base);
        if(base==4) byte(0x24);
        dword((u32)displacement); }
}
static int jump(int condition)
{
    if(condition<0) byte(0xE9);
    else { byte(0x0F);
        byte(0x80+condition); }
    int at=code_used;
    dword(0);
    return at;
}
static void patch(int at,int destination) { put32(code+36+at,(u32)(destination-at-4)); }
static void jump_link(int *head)
{ int at=jump(-1);
    put32(code+36+at,(u32)*head);
    *head=at; }
static void patch_chain(int head,int destination)
{
    while(head>=0) { int next=(int)get32(code+36+head);
        patch(head,destination);
        head=next; }
}
static void test_eax(void) { byte(0x85);
    byte(0xC0); }
static void bool_result(int condition) { byte(0x0F);
    byte(0x90+condition);
    byte(0xC0);
    byte(0x0F);
    byte(0xB6);
    byte(0xC0); }
static void cast_value(int type)
{
    Type *t=types+type;
    if(t->kind==TY_CHAR || t->kind==TY_SHORT) {
        byte(0x0F);
        byte((t->flags&Q_UNSIGNED)?(t->size==1?0xB6:0xB7):(t->size==1?0xBE:0xBF));
        byte(0xC0);
    }
}
static void load_value(int type)
{
    Type *t=types+type;
    if(t->kind==TY_ARRAY || t->kind==TY_FUNC || t->kind==TY_STRUCT || t->kind==TY_UNION) return;
    if(t->size==1 || t->size==2) { byte(0x0F);
        byte((t->flags&Q_UNSIGNED)?(t->size==1?0xB6:0xB7):(t->size==1?0xBE:0xBF));
        byte(0x00); }
    else if(t->size==4) { byte(0x8B);
        byte(0x00); }
    else fatal("cannot load incomplete object");
}
static void store_value(int type)
{
    Type *t=types+type;
    /* EDX 为目的地址，EAX 为值。聚合值本身是来源地址；按实际大小
     * 复制，包括尾部 1/2 字节，不能用 size/4 丢掉末尾成员。 */
    if(t->kind==TY_STRUCT || t->kind==TY_UNION) {
        for(int i=0;i<t->size;) {
            if(i+4<=t->size) { byte(0x8B);
                memory(1,0,i);
                byte(0x89);
                memory(1,2,i);
                i+=4; }
            else { byte(0x8A);
                memory(1,0,i);
                byte(0x88);
                memory(1,2,i);
                i++; }
        }
        move_reg(0,2);
        return;
    }
    cast_value(type);
    if(t->size==2) byte(0x66);
    byte(t->size==1?0x88:0x89);
    byte(0x02);
}
static int frame_scalar(int node)
{
    /* mio：标量局部/参数的位置已经由解析器确定为EBP加常量，
     * 不需要每次先LEA地址，再通过EAX二次解引用。这里仅识别
     * 没有求值副作用的名字节点；*p、a[i]、结构体字段和全局
     * 仍走原地址生成，不把可能变化的指针猜成固定栈槽。
     * 数组/聚合表达式携带地址而不是标量，绝不能直接读首四字节。
     * volatile只禁止合并/删读写，不禁止相同次数的直接寻址；
     * 新路径每次仍实际访问同一栈槽，不跨表达式缓存它的值。 */
    if(!node||nodes[node].kind!=N_SYMBOL)return 0;
    Symbol *s=symbols+nodes[node].value;Type *t=types+nodes[node].type;
    if(s->kind!=S_LOCAL&&s->kind!=S_PARAM)return 0;
    return (t->kind==TY_CHAR||t->kind==TY_SHORT||t->kind==TY_INT||t->kind==TY_PTR)
        &&(t->size==1||t->size==2||t->size==4);
}
static void frame_load_register(int node,int reg)
{
    Type *t=types+nodes[node].type;
    if(t->size==1||t->size==2){
        byte(0x0F);
        byte((t->flags&Q_UNSIGNED)?(t->size==1?0xB6:0xB7):(t->size==1?0xBE:0xBF));
    }else byte(0x8B);
    memory(reg,5,symbols[nodes[node].value].value);
}
static void frame_load(int node){frame_load_register(node,0);}
static int global_scalar(int node)
{
    if(!node||nodes[node].kind!=N_SYMBOL)return 0;
    Symbol *s=symbols+nodes[node].value;Type *t=types+nodes[node].type;
    return s->kind==S_GLOBAL&&(t->kind==TY_CHAR||t->kind==TY_SHORT
        ||t->kind==TY_INT||t->kind==TY_PTR)&&(t->size==1||t->size==2||t->size==4);
}
static void global_load_register(int node,int reg)
{
    /* 全局绝对地址仍由原F_ABS链接规则在所有代码/BSS已确定后
     * 回填，不能把此时仅为数据偏移的symbol.value写成最终地址。
     * 这与原LEA地址后间接读取访问同一对象，只缩短机器指令。 */
    Type *t=types+nodes[node].type;
    if(t->size==1||t->size==2){byte(0x0F);
        byte((t->flags&Q_UNSIGNED)?(t->size==1?0xB6:0xB7):(t->size==1?0xBE:0xBF));}
    else byte(0x8B);
    byte(reg*8+5);fixed(F_ABS,nodes[node].value,0,code_used);dword(0);
}
static void frame_store(int node)
{
    /* 赋值表达式的结果仍须经过目标char/short的截断与符号扩展，
     * 不能只写低字节却把未经转换的EAX交给外层表达式。用与原
     * store_value完全相同的cast_value，唯一改变是目的地址直接
     * 编进ModRM。EBP由cdecl栈帧保持，右侧嵌套调用返回后不变。 */
    int type=nodes[node].type;cast_value(type);
    if(types[type].size==2)byte(0x66);
    byte(types[type].size==1?0x88:0x89);
    memory(0,5,symbols[nodes[node].value].value);
}
static void symbol_address(int symbol,int extra)
{
    Symbol *s=symbols+symbol;
    if(s->kind==S_LOCAL || s->kind==S_PARAM) { byte(0x8D);
        memory(0,5,s->value+extra); }
    else {
        byte(0xB8);
        fixed(F_ABS,symbol,extra,code_used);
        dword(0);
    }
}
static void emit_expression(int node);
static void address(int node)
{
    Node *n=nodes+node;
    if(n->kind==N_SYMBOL) symbol_address(n->value,0);
    else if(n->kind==N_DEREF) emit_expression(n->a);
    else if(n->kind==N_STRING) { byte(0xB8);
        fixed(F_DATA,0,n->value,code_used);
        dword(0); }
    else fatal("code generation needs an addressable object");
}
static int compound_operator(int op)
{
    if(op==T_ADDSET) return '+';
    if(op==T_SUBSET) return '-';
    if(op==T_MULSET) return '*';
    if(op==T_DIVSET) return '/';
    if(op==T_MODSET) return '%';
    if(op==T_ANDSET) return '&';
    if(op==T_ORSET) return '|';
    if(op==T_XORSET) return '^';
    if(op==T_SHLSET) return T_SHL;
    if(op==T_SHRSET) return T_SHR;
    return op;
}
static void scale(int reg,int n)
{ if(n!=1) { if(n<=0) fatal("arithmetic needs complete pointer element");
        byte(0x69);
        byte(0xC0+reg*9);
        dword((u32)n); } }
static void operation(int op,int left_type,int right_type)
{
    int a=promoted(left_type),b=promoted(right_type),pa=types[a].kind==TY_PTR,pb=types[b].kind==TY_PTR;
    int unsigned_math=((types[a].flags|types[b].flags)&Q_UNSIGNED) || pa || pb;
    if((op=='+' || op=='-') && pa && !pb) scale(1,types[types[a].base].size);
    if(op=='+' && !pa && pb) scale(0,types[types[b].base].size);
    if(op=='+') { byte(0x01);
        byte(0xC8); }
    else if(op=='-') {
        byte(0x29);
        byte(0xC8);
        if(pa && pb) { int size=types[types[a].base].size;
            if(size!=1) { immediate(1,(u32)size);
                byte(0x99);
                byte(0xF7);
                byte(0xF9); }
        }
    } else if(op=='*') { byte(0x0F);
    byte(0xAF);
    byte(0xC1); }
    else if(op=='/' || op=='%') {
        if(unsigned_math) { byte(0x31);
            byte(0xD2); } else byte(0x99);
        byte(0xF7);
        byte(unsigned_math?0xF1:0xF9);
        if(op=='%') move_reg(0,2);
    } else if(op=='&') { byte(0x21);
    byte(0xC8); }
    else if(op=='|') { byte(0x09);
        byte(0xC8); }
    else if(op=='^') { byte(0x31);
        byte(0xC8); }
    else if(op==T_SHL || op==T_SHR) { byte(0xD3);
        byte(op==T_SHL?0xE0:unsigned_math?0xE8:0xF8); }
    else {
        byte(0x39);
        byte(0xC8);
        int cc=op==T_EQ?4:op==T_NE?5:op=='<'?(unsigned_math?2:12):op=='>'?(unsigned_math?7:15):op==T_LE?(unsigned_math?6:14):op==T_GE?(unsigned_math?3:13):-1;
        if(cc<0) fatal("unsupported binary operator");
        bool_result(cc);
    }
}
static int constant_operation(int op,int left_type,int right_type,u32 value)
{
    /* mio：仅右操作数是无副作用的整数字面量时，直接使用x86立即数。
     * 左操作数已且只已计算一次，所以f()/256、volatile读取、后自增
     * 的副作用顺序不变。指针的步长和完整类型仍交给原operation；
     * 不把“常数优化”变成另一套有遗漏的指针算术实现。
     *
     * 软件光追大量使用Q8/Q16，原后端每次/256、%256都压栈、装ECX
     * 并执行IDIV。二次幂除法可以精确用移位实现，但有符号负数须
     * 先加偏置：C向零截断，而SAR向负无穷取整。余数从原数减去
     * 商乘除数取得，保持负余数、INT_MIN和每个边界的原语义。
     * 不是近似倒数，不会改变定点交点、阴影或负世界坐标。
     *
     * 只处理正的二次幂；零、负除数和其他常数继续原DIV/IDIV。
     * 因此除零/INT_MIN除-1仍是原运行时异常，而非编译时偷吞异常。
     * ECX/EDX是原后端已约定的临时寄存器，EBX/ESI/EDI/EBP不改。
     * unsigned_math沿用原operation的类型选择，优化不顺便改语言合同。 */
    int a=promoted(left_type),b=promoted(right_type);
    if(types[a].kind==TY_PTR||types[b].kind==TY_PTR)return 0;
    int unsigned_math=(types[a].flags|types[b].flags)&Q_UNSIGNED;
    if(op=='+'||op=='-'||op=='&'||op=='|'||op=='^'){
        byte(op=='+'?0x05:op=='-'?0x2D:op=='&'?0x25:op=='|'?0x0D:0x35);
        dword(value);return 1;
    }
    if(op=='*'){
        byte(0x69);byte(0xC0);dword(value);return 1;
    }
    if(op==T_SHL||op==T_SHR){
        byte(0xC1);byte(op==T_SHL?0xE0:unsigned_math?0xE8:0xF8);
        byte((int)(value&31));return 1;
    }
    if(op=='/'||op=='%'){
        if(!value||(value&(value-1))||(!unsigned_math&&value>0x7FFFFFFFu))return 0;
        int shift=0;u32 divisor=value;while(divisor>1){divisor>>=1;shift++;}
        if(!shift){if(op=='%'){byte(0x31);byte(0xC0);}return 1;}
        if(unsigned_math){
            if(op=='%'){byte(0x25);dword(value-1);}
            else{byte(0xC1);byte(0xE8);byte(shift);}
            return 1;
        }
        if(op=='%')move_reg(2,0);
        move_reg(1,0);byte(0xC1);byte(0xF9);byte(31);
        byte(0x81);byte(0xE1);dword(value-1);
        byte(0x01);byte(0xC8);byte(0xC1);byte(0xF8);byte(shift);
        if(op=='%'){
            byte(0xC1);byte(0xE0);byte(shift);
            byte(0x29);byte(0xC2);move_reg(0,2);
        }
        return 1;
    }
    return 0;
}
static void emit_expression(int node)
{
    if(!node) { immediate(0,0);
        return; }
    Node *n=nodes+node;
    error_position=n->where;
    int kind=n->kind;
    if(kind==N_CONST) immediate(0,(u32)n->value);
    else if(frame_scalar(node))frame_load(node);
    else if(global_scalar(node))global_load_register(node,0);
    else if(kind==N_SYMBOL || kind==N_DEREF || kind==N_STRING) { address(node);
        load_value(n->type); }
    else if(kind==N_ADDR) address(n->a);
    else if(kind==N_CAST) { emit_expression(n->a);
        cast_value(n->type); }
    else if(kind==N_COMMA) { emit_expression(n->a);
        emit_expression(n->b); }
    else if(kind==N_UNARY) {
        emit_expression(n->a);
        if(n->value=='-') { byte(0xF7);
            byte(0xD8); }
        else if(n->value=='~') { byte(0xF7);
            byte(0xD0); }
        else if(n->value=='!') { test_eax();
            bool_result(4); }
    } else if(kind==N_BINARY) {
        emit_expression(n->a);
        if(n->value==T_AND || n->value==T_OR) {
            test_eax();
            int end=jump(n->value==T_AND?4:5);
            emit_expression(n->b);
            test_eax();
            bool_result(5);
            int done=jump(-1);
            patch(end,code_used);
            immediate(0,n->value==T_OR);
            patch(done,code_used);
        } else if(nodes[n->b].kind==N_CONST&&constant_operation(n->value,
                    nodes[n->a].type,nodes[n->b].type,(u32)nodes[n->b].value)){
            // 立即数已完整发射；不再生成压栈、右侧装载和弹栈。
        } else if(frame_scalar(n->b)||global_scalar(n->b)||nodes[n->b].kind==N_CONST){
            /* mio：简单右操作数的装载不调用函数、不修改对象、
             * 不占用EAX，直接读入ECX即可保住已经求出的左值。
             * 原代码每个a+b/a*b/a<b等都PUSH左值、用EAX取b、
             * MOV到ECX再POP左值；整数光追会重复数百万次。
             * 只省这三条机器指令，实际右值仍在左值求完之后读，
             * 所以f()+volatile_global和f(&local)+local沿用原顺序。
             * 窄右值先按自身类型符号/零扩展；指针缩放、除法、
             * 比较和返回转换仍用原operation。复杂右侧/数组/
             * 解引用/副作用保留栈保护，不猜测哪些调用可内联。 */
            if(frame_scalar(n->b))frame_load_register(n->b,1);
            else if(global_scalar(n->b))global_load_register(n->b,1);
            else immediate(1,(u32)nodes[n->b].value);
            operation(n->value,nodes[n->a].type,nodes[n->b].type);
        } else {
            byte(0x50);
            emit_expression(n->b);
            move_reg(1,0);
            byte(0x58);
            operation(n->value,nodes[n->a].type,nodes[n->b].type);
        }
    } else if(kind==N_COND) {
        emit_expression(n->a);
        test_eax();
        int no=jump(4);
        emit_expression(n->b);
        int done=jump(-1);
        patch(no,code_used);
        emit_expression(n->c);
        patch(done,code_used);
    } else if(kind==N_ASSIGN&&frame_scalar(n->a)) {
        /* 固定栈槽的左值求址没有副作用，也不会被右侧求值移动。
         * 普通赋值省掉LEA/PUSH/POP；复合赋值仍先读旧值再执行
         * 右侧，保护a+=(a=...)、&a经函数修改等原求值顺序。
         * 指针步长/有符号选择仍调用原operation，不复制第二套语义。 */
        if(n->value=='=')emit_expression(n->b);
        else{
            frame_load(n->a);byte(0x50);
            emit_expression(n->b);move_reg(1,0);byte(0x58);
            operation(compound_operator(n->value),n->type,nodes[n->b].type);
        }
        frame_store(n->a);
    } else if(kind==N_ASSIGN) {
        address(n->a);
        byte(0x50);
        if(n->value=='=') emit_expression(n->b);
        else {
            load_value(n->type);
            byte(0x50);
            emit_expression(n->b);
            move_reg(1,0);
            byte(0x58);
            operation(compound_operator(n->value),n->type,nodes[n->b].type);
        }
        byte(0x5A);
        store_value(n->type);
    } else if(kind==N_INC&&frame_scalar(n->a)) {
        /* 后缀保存转换后的旧值；前缀返回转换后的新值。两条路径
         * 均只有一次真实读和一次真实写，窄整数溢出截断、指针
         * 增量和volatile行为与原实现相同，不使用会改变返回值的
         * INC [mem]替代。这里只省没有用途的目的地址栈往返。 */
        frame_load(n->a);if(n->c)byte(0x50);
        int type=promoted(n->type),delta=n->value;
        if(types[type].kind==TY_PTR)delta*=types[types[type].base].size;
        byte(0x05);dword((u32)delta);frame_store(n->a);
        if(n->c)byte(0x58);
    } else if(kind==N_INC) {
        address(n->a);
        byte(0x50);
        load_value(n->type);
        if(n->c) byte(0x50);
        int type=promoted(n->type),delta=n->value;
        if(types[type].kind==TY_PTR) delta*=types[types[type].base].size;
        byte(0x05);
        dword((u32)delta);
        if(n->c) { byte(0x8B);
            memory(2,4,4);
            store_value(n->type);
            byte(0x58);
            stack_add(4); }
        else { byte(0x5A);
            store_value(n->type); }
    } else if(kind==N_CALL) {
        /* 参数树按源码顺序倒链，恰好对应 cdecl 从右往左压栈。
         * 间接目标先入栈，求参数不会破坏函数指针；CALL 后一并平衡。 */
        int direct=nodes[n->a].kind==N_SYMBOL && symbols[nodes[n->a].value].kind==S_FUNCTION;
        if(!direct) { emit_expression(n->a);
            byte(0x50); }
        for(int arg=n->b;arg;arg=nodes[arg].b) { emit_expression(nodes[arg].a);
            byte(0x50); }
        if(direct) { byte(0xE8);
            fixed(F_REL,nodes[n->a].value,0,code_used);
            dword(0); }
        else { byte(0x8B);
            memory(0,4,n->value*4);
            byte(0xFF);
            byte(0xD0); }
        stack_add(n->value*4+(direct?0:4));
        cast_value(n->type);
    } else fatal("unsupported value node");
}
/* 编译期计算与运行时生成分别实现并互相测试，不能用宿主 signed
 * 溢出行为定义目标整数语义。加减乘在 u32 环上运算，比较/除法再依据
 * 类型选择有符号；条件与逻辑短路不访问未选中的除零表达式。 */
static u32 constant_value(int node)
{
    Node *n=nodes+node;
    error_position=n->where;
    if(n->kind==N_CONST) return (u32)n->value;
    if(n->kind==N_CAST) {
        u32 v=constant_value(n->a);
        Type *t=types+n->type;
        if(t->size==1) return (t->flags&Q_UNSIGNED)?v&255:(u32)(int)(char)v;
        if(t->size==2) return (t->flags&Q_UNSIGNED)?v&65535:(u32)(int)(short)v;
        return v;
    }
    if(n->kind==N_UNARY) { u32 v=constant_value(n->a);
        return n->value=='-'?0u-v:n->value=='~'?~v:n->value=='!'?!v:v; }
    if(n->kind==N_COND) return constant_value(constant_value(n->a)?n->b:n->c);
    if(n->kind==N_COMMA) { constant_value(n->a);
        return constant_value(n->b); }
    if(n->kind!=N_BINARY) fatal("initializer requires a constant expression");
    u32 a=constant_value(n->a),b;
    int op=n->value;
    if(op==T_AND && !a) return 0;
    if(op==T_OR && a) return 1;
    b=constant_value(n->b);
    int uns=(types[promoted(nodes[n->a].type)].flags|types[promoted(nodes[n->b].type)].flags)&Q_UNSIGNED;
    if(op=='+') return a+b;
    if(op=='-') return a-b;
    if(op=='*') return a*b;
    if(op=='/' || op=='%') {
        if(!b) fatal("division by zero in constant expression");
        if(uns) return op=='/'?a/b:a%b;
        if(a==0x80000000u && b==0xFFFFFFFFu) fatal("signed constant division overflow");
        return op=='/'?(u32)((int)a/(int)b):(u32)((int)a%(int)b);
    }
    if(op=='&') return a&b;
    if(op=='|') return a|b;
    if(op=='^') return a^b;
    if(op==T_SHL) return a<<(b&31);
    if(op==T_SHR) return uns?a>>(b&31):(u32)((int)a>>(b&31));
    if(op==T_EQ) return a==b;
    if(op==T_NE) return a!=b;
    if(op=='<') return uns?a<b:(int)a<(int)b;
    if(op=='>') return uns?a>b:(int)a>(int)b;
    if(op==T_LE) return uns?a<=b:(int)a<=(int)b;
    if(op==T_GE) return uns?a>=b:(int)a>=(int)b;
    if(op==T_AND) return a && b;
    if(op==T_OR) return a || b;
    fatal("unsupported constant operator");
    return 0;
}
static int constant_address_mode(int node,int *symbol,int *kind,int *addend,int lvalue_mode)
{
    Node *n=nodes+node;
    if(n->kind==N_CAST) return constant_address_mode(n->a,symbol,kind,addend,lvalue_mode);
    if(n->kind==N_ADDR) return constant_address_mode(n->a,symbol,kind,addend,1);
    if(n->kind==N_DEREF && lvalue_mode) return constant_address_mode(n->a,symbol,kind,addend,0);
    if(n->kind==N_STRING) { *kind=F_DATA;
        *symbol=0;
        *addend+=n->value;
        return 1; }
    if(n->kind==N_SYMBOL && (symbols[n->value].kind==S_FUNCTION || (symbols[n->value].kind==S_GLOBAL && (lvalue_mode || types[n->type].kind==TY_ARRAY)))) {
        *kind=F_ABS;
        *symbol=n->value;
        return 1;
    }
    if(n->kind==N_BINARY && (n->value=='+' || n->value=='-')) {
        if(constant_address_mode(n->a,symbol,kind,addend,0)) {
            int type=promoted(nodes[n->a].type),stride=types[type].kind==TY_PTR?types[types[type].base].size:1;
            *addend+=(int)constant_value(n->b)*stride*(n->value=='-'?-1:1);
            return 1;
        }
        if(n->value=='+' && constant_address_mode(n->b,symbol,kind,addend,0)) {
            int type=promoted(nodes[n->b].type),stride=types[type].kind==TY_PTR?types[types[type].base].size:1;
            *addend+=(int)constant_value(n->a)*stride;
            return 1;
        }
    }
    return 0;
}
static void global_init(int type,int offset,int expression)
{
    int symbol=0,kind=0,addend=0;
    if(types[type].kind==TY_STRUCT || types[type].kind==TY_UNION) fatal("global aggregate copy is not a constant initializer");
    if(types[type].size==4 && constant_address_mode(expression,&symbol,&kind,&addend,0)) {
        fixed(kind,symbol,addend,-offset-1);
        return;
    }
    u32 value=constant_value(expression);
    for(int i=0;i<types[type].size;i++) { data[offset+i]=(u8)value;
        value>>=8; }
}
static void define_global(int symbol,int initializer)
{
    Symbol *s=symbols+symbol;
    int size=types[s->type].size;
    if(!size || types[s->type].kind==TY_FUNC) fatal("global object needs a complete type");
    if(s->defined) { if(initializer) fatal("global is already defined");
        return; }
    if(initializer) {
        data_used=(data_used+3)&~3;
        if(size>DATA_MAX-data_used) fatal("initialized global data capacity exceeded");
        s->aux=1;
        s->value=data_used;
        data_used+=size;
        for(int i=0;i<size;i++) data[s->value+i]=0;
        for(int n=initializer;n;n=nodes[n].next) if(!nodes[n].value) global_init(nodes[n].type,s->value+nodes[n].b,nodes[n].c);
    } else {
        bss_used=(bss_used+3)&~3;
        if(size>0x3D0000-bss_used) fatal("BSS capacity exceeded");
        s->aux=0;
        s->value=bss_used;
        bss_used+=size;
    }
    s->defined=1;
}
/* 内联汇编只接受明确的固定寄存器约束；未知约束必须诊断，不偷偷
 * 当成普通函数。模板采用常用 AT&T 写法，%% 转义寄存器，也支持 %0
 * 引用约束编号。内存形式是 disp(base)，足够直接表达 syscall 桥。
 */
typedef struct { int kind,reg,base,value; } AsmOperand;
static int asm_register(const char *name)
{
    const char *names[]={"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
    for(int i=0;i<8;i++) if(equal(name,names[i])) return i;
    fatal("asm needs a 32-bit register");
    return 0;
}
static int constraint_register(int text)
{
    char *p=spelling(text);
    if(*p=='=' || *p=='+') p++;
    int r=*p=='a'?0:*p=='c'?1:*p=='d'?2:*p=='b'?3:*p=='S'?6:*p=='D'?7:-1;
    if(r<0 || p[1]) fatal("asm requires fixed a/b/c/d/S/D constraints");
    return r;
}
static void asm_space(char **p) { while(**p==' ' || **p=='\t' || **p=='\r') (*p)++; }
static int asm_integer(char **p)
{
    int sign=1;
    if(**p=='-') { sign=-1;
        (*p)++; } else if(**p=='+') (*p)++;
    int base=10;
    if((*p)[0]=='0' && ((*p)[1]=='x' || (*p)[1]=='X')) { base=16;
        *p+=2; }
    u32 value=0;
    int n=0;
    for(;;) {
        int c=**p,d=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:99;
        if(d>=base) break;
        value=value*(u32)base+(u32)d;
        (*p)++;
        n++;
    }
    if(!n) fatal("asm immediate needs an integer");
    return sign<0?(int)(0u-value):(int)value;
}
static int asm_reg_token(char **p,int *registers,int count)
{
    if(**p!='%') fatal("asm register needs percent prefix");
    (*p)++;
    if(**p=='%') (*p)++;
    if(**p>='0' && **p<='9') {
        int i=*(*p)++-'0';
        if(i>=count) fatal("asm operand index exceeds constraints");
        return registers[i];
    }
    char name[8];
    int n=0;
    while(**p>='a' && **p<='z') { if(n>=7) fatal("asm register too long");
        name[n++]=*(*p)++; }
    name[n]=0;
    return asm_register(name);
}
static void asm_operand(char **p,AsmOperand *o,int *registers,int count)
{
    asm_space(p);
    o->kind=0;
    o->reg=0;
    o->base=-1;
    o->value=0;
    if(**p=='$') { (*p)++;
        o->kind=1;
        o->value=asm_integer(p);
        return; }
    if(**p=='%') { o->kind=2;
        o->reg=asm_reg_token(p,registers,count);
        return; }
    o->kind=3;
    if(**p!='(') o->value=asm_integer(p);
    if(**p=='(') {
        (*p)++;
        o->base=asm_reg_token(p,registers,count);
        asm_space(p);
        if(**p!=')') fatal("asm supports disp(base) memory operands");
        (*p)++;
    }
}
static void asm_rm(int reg,AsmOperand *operand)
{
    if(operand->kind==2) byte(0xC0+reg*8+operand->reg);
    else if(operand->kind==3) memory(reg,operand->base,operand->value);
    else fatal("asm instruction needs register or memory");
}
static void asm_template(int string,int *registers,int count)
{
    char *p=(char *)(data+nodes[string].value);
    while(*p) {
        asm_space(&p);
        if(*p==';' || *p=='\n') { p++;
            continue; }
        char name[12];
        int n=0;
        /* UD2 的数字属于指令名，不能只读字母后误诊成未知的 ud。
         * 指令名仍在本子集白名单内，允许数字并不等于接受任意汇编。 */
        while((*p>='a' && *p<='z') || (*p>='0' && *p<='9')) { if(n>=11) fatal("asm mnemonic too long");
            name[n++]=*p++; } name[n]=0;
        if(!n) fatal("asm needs an instruction");
        if(equal(name,"movl")) copy(name,"mov",12);
        if(equal(name,"pushl")) copy(name,"push",12);
        if(equal(name,"popl")) copy(name,"pop",12);
        if(equal(name,"xorl")) copy(name,"xor",12);
        if(equal(name,"divl")) copy(name,"div",12);
        if(equal(name,"idivl")) copy(name,"idiv",12);
        if(equal(name,"ud2")) { byte(0x0F);
            byte(0x0B); }
        else if(equal(name,"nop")) byte(0x90);
        else if(equal(name,"int3")) byte(0xCC);
        else if(equal(name,"cld")) byte(0xFC);
        else if(equal(name,"int")) { AsmOperand a;
            asm_operand(&p,&a,registers,count);
            if(a.kind!=1 || a.value<0 || a.value>255) fatal("int needs an immediate byte");
            byte(0xCD);
            byte(a.value); }
        else if(equal(name,"push") || equal(name,"pop") || equal(name,"div") || equal(name,"idiv")) {
            AsmOperand a;
            asm_operand(&p,&a,registers,count);
            if(equal(name,"push") && a.kind==1) { byte(0x68);
                dword((u32)a.value); }
            else if(a.kind==2 && (equal(name,"push") || equal(name,"pop"))) byte((equal(name,"push")?0x50:0x58)+a.reg);
            else { byte(0xF7);
                asm_rm(equal(name,"div")?6:7,&a); }
        } else if(equal(name,"mov") || equal(name,"lea") || equal(name,"xor") || equal(name,"add") || equal(name,"sub") || equal(name,"and") || equal(name,"or") || equal(name,"cmp") || equal(name,"test")) {
            AsmOperand a,b;
            asm_operand(&p,&a,registers,count);
            asm_space(&p);
            if(*p++!=',') fatal("asm binary instruction needs comma");
            asm_operand(&p,&b,registers,count);
            if(equal(name,"mov")) {
                if(a.kind==1 && b.kind==2) immediate(b.reg,(u32)a.value);
                else if(a.kind==1) { byte(0xC7);
                    asm_rm(0,&b);
                    dword((u32)a.value); }
                else if(a.kind==2) { byte(0x89);
                    asm_rm(a.reg,&b); }
                else if(b.kind==2) { byte(0x8B);
                    asm_rm(b.reg,&a); }
                else fatal("mov cannot use two memory operands");
            } else if(equal(name,"lea")) {
                if(a.kind!=3 || b.kind!=2) fatal("lea needs memory then register");
                byte(0x8D);
                asm_rm(b.reg,&a);
            } else {
                int group=equal(name,"add")?0:equal(name,"or")?1:equal(name,"and")?4:equal(name,"sub")?5:equal(name,"xor")?6:equal(name,"cmp")?7:-1;
                if(a.kind==1 && group>=0) { byte(0x81);
                    asm_rm(group,&b);
                    dword((u32)a.value); }
                else if(a.kind==2) { byte(equal(name,"test")?0x85:group*8+1);
                    asm_rm(a.reg,&b); }
                else fatal("asm arithmetic source needs register or immediate");
            }
        } else fatal("unsupported inline asm instruction");
        asm_space(&p);
        if(*p && *p!=';' && *p!='\n') fatal("extra text after asm instruction");
    }
}
static void emit_asm(int node)
{
    Node *n=nodes+node;
    int regs[16],values[16],out_nodes[8],out_regs[8],inputs=0,outputs=0,count=0;
    for(int a=n->b;a;a=nodes[a].next) {
        if(outputs>=8) fatal("too many asm outputs");
        int r=constraint_register(nodes[a].value);
        regs[count++]=r;
        out_nodes[outputs]=nodes[a].a;
        out_regs[outputs++]=r;
        address(nodes[a].a);
        byte(0x50);
        if(spelling(nodes[a].value)[0]=='+') { values[inputs]=nodes[a].a;
            regs[8+inputs++]=r; }
    }
    for(int a=n->c;a;a=nodes[a].next) {
        if(inputs>=8 || count>=8) fatal("too many asm inputs");
        int r=constraint_register(nodes[a].value);
        regs[count++]=r;
        values[inputs]=nodes[a].a;
        regs[8+inputs++]=r;
    }
    for(int i=0;i<inputs;i++) { emit_expression(values[i]);
        byte(0x50); }
    for(int i=inputs-1;i>=0;i--) byte(0x58+regs[8+i]);
    asm_template(n->a,regs,count);
    for(int i=0;i<outputs;i++) byte(0x50+out_regs[i]);
    for(int i=0;i<outputs;i++) {
        byte(0x8B);
        memory(0,4,4*(outputs-1-i));
        byte(0x8B);
        memory(2,4,4*(outputs*2-1-i));
        store_value(nodes[out_nodes[i]].type);
    }
    stack_add(outputs*8);
}
static void control_begin(int loop)
{
    if(control_depth>=64) fatal("control nesting exceeds 64");
    break_jumps[control_depth]=continue_jumps[control_depth]=-1;
    control_loop[control_depth++]=loop;
}
static void control_end(int end,int continuation)
{
    control_depth--;
    patch_chain(break_jumps[control_depth],end);
    patch_chain(continue_jumps[control_depth],continuation);
}
static void emit_statement(int node)
{
    if(!node) return;
    Node *n=nodes+node;
    error_position=n->where;
    int kind=n->kind;
    if(kind==N_BLOCK) for(int a=n->a;a;a=nodes[a].next) emit_statement(a);
    else if(kind==N_EXPR) emit_expression(n->a);
    else if(kind==N_INIT) {
        symbol_address(n->a,n->b);
        byte(0x50);
        if(n->value) {
            /* 花括号对象清零用一次短循环，不为几十 KB 数组生成几万条
             * MOV。初始化过程不调用其他函数，ECX/EDX 可安全作为计数。 */
            byte(0x5A);
            immediate(1,(u32)types[n->type].size);
            immediate(0,0);
            int start=code_used;
            byte(0x88);
            byte(0x02);
            byte(0x42);
            byte(0x49);
            int again=jump(5);
            patch(again,start);
        } else { emit_expression(n->c);
        byte(0x5A);
        store_value(n->type); }
    } else if(kind==N_IF) {
        emit_expression(n->a);
        test_eax();
        int otherwise=jump(4);
        emit_statement(n->b);
        if(n->c) { int end=jump(-1);
            patch(otherwise,code_used);
            emit_statement(n->c);
            patch(end,code_used); }
        else patch(otherwise,code_used);
    } else if(kind==N_WHILE || kind==N_DO || kind==N_FOR) {
        control_begin(1);
        int start=code_used,exit=-1;
        if(kind!=N_DO && n->a) { emit_expression(n->a);
            test_eax();
            exit=jump(4); }
        emit_statement(n->b);
        int continuation=code_used;
        if(kind==N_FOR && n->c) emit_expression(n->c);
        if(kind==N_DO) { emit_expression(n->a);
            test_eax();
            int again=jump(5);
            patch(again,start); }
        else { int again=jump(-1);
            patch(again,start); }
        if(exit>=0) patch(exit,code_used);
        control_end(code_used,kind==N_WHILE?start:continuation);
    } else if(kind==N_SWITCH) {
        emit_expression(n->a);
        int dispatch=jump(-1);
        control_begin(0);
        emit_statement(n->b);
        int skip=jump(-1);
        patch(dispatch,code_used);
        int fallback=-1;
        for(int c=n->c;c;c=nodes[c].b) {
            if(nodes[c].type) fallback=nodes[c].c;
            else { byte(0x3D);
                dword((u32)nodes[c].value);
                int match=jump(4);
                patch(match,nodes[c].c); }
        }
        int tail=jump(-1);
        patch(tail,fallback<0?code_used:fallback);
        patch(skip,code_used);
        control_end(code_used,code_used);
    } else if(kind==N_CASE) { n->c=code_used;
    emit_statement(n->a); }
    else if(kind==N_BREAK) {
        if(!control_depth) fatal("break outside loop/switch");
        jump_link(break_jumps+control_depth-1);
    } else if(kind==N_CONTINUE) {
        int depth=control_depth-1;
        while(depth>=0 && !control_loop[depth]) depth--;
        if(depth<0) fatal("continue outside loop");
        jump_link(continue_jumps+depth);
    } else if(kind==N_RETURN) {
        emit_expression(n->a);
        cast_value(n->type);
        jump_link(&return_jumps);
    } else if(kind==N_LABEL) {
        int label=label_find(n->value);
        if(label_targets[label]>=0) fatal("duplicate function label");
        label_targets[label]=code_used;
        emit_statement(n->a);
    } else if(kind==N_GOTO) jump_link(label_jumps+label_find(n->value));
    else if(kind==N_ASM) emit_asm(node);
    else fatal("unsupported statement node");
}
static void hex_text(char *out,u32 value)
{
    const char *digits="0123456789ABCDEF";
    for(int i=7;i>=0;i--) { out[i]=digits[value&15];
        value>>=4; } out[8]=0;
}
static void map_function(int symbol,int address_value,int where)
{
    char row[160],hex[9],line[12];
    hex_text(hex,(u32)address_value);
    decimal(line,where&65535);
    copy(row,hex,sizeof(row));
    append(row," ",sizeof(row));
    append(row,spelling(symbols[symbol].name),sizeof(row));
    append(row," ",sizeof(row));
    int file=((u32)where>>16)&32767;
    if(file<source_count) append(row,sources[file].path,sizeof(row));
    append(row,":",sizeof(row));
    append(row,line,sizeof(row));
    append(row,"\n",sizeof(row));
    int size=length(row);
    if(map_used+size>=(int)sizeof(map_text)) fatal("symbol map capacity exceeded");
    for(int i=0;i<size;i++) map_text[map_used++]=row[i];
    map_text[map_used]=0;
}
static void emit_function(int symbol,int body)
{
    /* mio：普通后端只以EAX/ECX/EDX作为临时寄存器，EBP/ESP
     * 按栈帧约定保存。过去每个小整数函数都额外PUSH/POP三对
     * EBX/ESI/EDI，软件光追的点积/比值/夹取又嵌套多层，实际
     * 没有修改这些寄存器仍重复执行六条指令和六次栈访存。
     * 没有asm时原值本来就未改变；有asm时先扫描本函数完整
     * 语法树，只要含N_ASM就保守保存全部三个。不能仅看约束，
     * 模板允许显式写寄存器，clobber也可能指名修改它们。
     * 调用其它函数不会破坏保留值：对方同样遵守此cdecl规则；
     * 普通间接函数、递归、异常恢复和系统调用继续原调用边界。
     * 后续若引入寄存器分配须同步扩使用掩码，此证明只适用于
     * 当前仅EAX/ECX/EDX的生成器，不把“没asm”永久等同“不使用”。 */
    int preserve=0;
    for(int i=1;i<node_count;i++)if(nodes[i].kind==N_ASM)preserve=1;
    symbols[symbol].value=code_used;
    map_function(symbol,BASE+code_used,nodes[body].where);
    return_jumps=-1;
    control_depth=0;
    label_count=0;
    byte(0x55);
    move_reg(5,4);
    byte(0x81);
    byte(0xEC);
    dword((u32)local_bytes);
    if(preserve){byte(0x53);byte(0x56);byte(0x57);}
    emit_statement(body);
    immediate(0,0);
    patch_chain(return_jumps,code_used);
    for(int i=0;i<label_count;i++) {
        if(label_targets[i]<0) fatal("goto target label is undefined");
        patch_chain(label_jumps[i],label_targets[i]);
    }
    if(preserve){byte(0x5F);byte(0x5E);byte(0x5B);}
    byte(0xC9);
    byte(0xC3);
}
static void finish_image(const char *output)
{
    int main_symbol=find_symbol(intern("main",4),0);
    if(!main_symbol || symbols[main_symbol].kind!=S_FUNCTION || !symbols[main_symbol].defined) fatal("main function is missing");
    if(types[symbols[main_symbol].type].count!=0) fatal("SandCore main must have no parameters; use sc_args");
    int text_end=(code_used+3)&~3;
    if(data_used>CODE_MAX-text_end) fatal("SCX initialized image capacity exceeded");
    int load=text_end+data_used;
    if(load+bss_used>0x3D0000) fatal("SCX code/data/BSS exceed private address region");
    for(int i=code_used;i<text_end;i++) code[36+i]=0;
    for(int i=0;i<data_used;i++) code[36+text_end+i]=data[i];
    for(int i=0;i<fix_count;i++) {
        Fix *f=fixes+i;
        u32 value=0;
        if(f->kind==F_DATA) value=BASE+text_end+f->addend;
        else {
            Symbol *s=symbols+f->symbol;
            if(!s->defined) { error_position=0;
                fatal("referenced external symbol has no definition"); }
            if(s->kind==S_FUNCTION) value=BASE+s->value;
            else value=BASE+(s->aux?text_end:load)+s->value;
            value+=(u32)f->addend;
            if(f->kind==F_REL) value-=BASE+f->offset+4;
        }
        int offset=f->offset<0?text_end-f->offset-1:f->offset;
        put32(code+36+offset,value);
    }
    /* 对象地址也入符号表，图形调试器和验收可以读取实际目标数据，
     * 不把宿主 ELF 地址套到 SCCC 生成的另一个布局上。OBJECT 明示
     * 没有保存对象声明行号，不伪造源码位置。 */
    for(int i=1;i<symbol_count;i++) if(symbols[i].kind==S_GLOBAL && symbols[i].defined) {
        char row[160],hex[9];
        u32 address=BASE+(symbols[i].aux?text_end:load)+symbols[i].value;
        hex_text(hex,address);
        copy(row,hex,sizeof(row));
        append(row," ",sizeof(row));
        append(row,spelling(symbols[i].name),sizeof(row));
        append(row," OBJECT\n",sizeof(row));
        int n=length(row);
        if(map_used+n>=(int)sizeof(map_text)) fatal("symbol map capacity exceeded");
        for(int j=0;j<n;j++) map_text[map_used++]=row[j];
        map_text[map_used]=0;
    }
    /* 固定入口直接 CALL main，再把返回值交给 EXIT。它与宿主 start.asm
     * 的约定一致，不需要附带宿主对象文件或库才能运行生成的程序。 */
    for(int i=0;i<24;i++) code[36+i]=0x90;
    code[36]=0xE8;
    put32(code+37,(u32)(symbols[main_symbol].value-5));
    code[41]=0x89;
    code[42]=0xC3;
    code[43]=0xB8;
    put32(code+44,0);
    code[48]=0xCD;
    code[49]=0x7C;
    code[50]=0xEB;
    code[51]=0xFE;
    const char *magic="SCX1MIO";
    for(int i=0;i<8;i++) code[i]=(u8)magic[i];
    put32(code+8,0);
    put32(code+12,(u32)load);
    put32(code+16,(u32)bss_used);
    put32(code+20,131072);
    put32(code+24,0);
    put32(code+28,BASE);
    put32(code+32,0x004F494D);
    /* 内置图标只参与容器封装，不参与代码重定位/BSS布局；从三环文件
     * 读取的资源也必须验证精确长度。初始化code数组仍保持旧容量，
     * 可选较大容器放高端堆，避免自举编译器BSS逼近4MB私有映像上限。 */
    int image_bytes=36+load;
    u8 *container=code;
    if(icon_path[0]){
        u32 stat[2];u8 header[32];
        if(sc_stat(icon_path,stat) || stat[0]!=1 || sc_read_at(icon_path,header,32,0)!=32)
            fatal("cannot read embedded icon");
        const char *imagic="SCB2MIO";
        for(int i=0;i<8;i++)if(header[i]!=(u8)imagic[i])fatal("embedded icon must be SCB2");
        u32 w=*(u32 *)(header+8),h=*(u32 *)(header+12);
        if(!w || !h || w>128 || h>128 || *(u32 *)(header+16)!=2 || *(u32 *)(header+20)
           || *(u32 *)(header+24)!=w*h*4 || *(u32 *)(header+28)!=0x004F494Du || stat[1]!=32+w*h*4)
            fatal("invalid embedded icon header or length");
        container=(u8 *)sc_alloc((u32)image_bytes+stat[1]);
        if(!container)fatal("embedded icon container allocation failed");
        put32(code+24,2);
        for(int i=0;i<image_bytes;i++)container[i]=code[i];
        if(sc_read_at(icon_path,container+image_bytes,stat[1],0)!=(int)stat[1])fatal("embedded icon read failed");
        image_bytes+=(int)stat[1];
    }
    if(sc_write(output,container,image_bytes)!=image_bytes) fatal("output write failed (possibly protected path)");
    if(container!=code)sc_free(container);
    char report[128],value[12];
    decimal(value,load);
    copy(report,"SCCC OK load=",sizeof(report));
    append(report,value,sizeof(report));
    append(report," bss=",sizeof(report));
    decimal(value,bss_used);
    append(report,value,sizeof(report));
    append(report," / mio\n",sizeof(report));
    sc_write(log_path,report,length(report));
    sc_puts(report);
    /* 文本符号表有 MIO 魔数；调试器可按函数名/来源展示，但 CPU 断点
     * 本身始终使用真实 EIP，不依赖符号表才能单步或读内存。 */
    sc_write(map_path,map_text,map_used);
}
