#include "SCAWK.H"
#define AWK_NODES 2048
#define AWK_RECORD 16384
#define AWK_FIELDS 256
#define AWK_VARIABLES 96
#define AWK_ARRAYS 1024
#define AWK_HEAP 4194304u
enum {A_NAME=256,A_NUMBER,A_STRING,A_EQ,A_NE,A_LE,A_GE,A_AND,A_OR,A_MATCH_NOT,A_INC,A_DEC,A_ADD_SET,A_SUB_SET,A_MUL_SET,A_DIV_SET,A_MOD_SET,A_POWER,A_IN,A_CAT};
enum {A_LITERAL,A_NUMERIC,A_VARIABLE,A_FIELD,A_ELEMENT,A_REGEX,A_UNARY,A_BINARY,A_CONDITIONAL,A_ASSIGN,A_CALL,A_CHANGE,
      A_BLOCK,A_PRINT,A_PRINTF,A_IF,A_WHILE,A_FOR,A_FOR_IN,A_NEXT,A_BREAK,A_CONTINUE,A_EXIT,A_DELETE,A_EXPRESSION};
typedef struct {int kind,op,a,b,c,next;const char *text;} awk_node;
typedef struct {const char *text;int number,kind;} awk_value;
typedef struct {char name[32];awk_value value;} awk_variable;
typedef struct {u32 hash;int variable,state;char *key;awk_value value;} awk_element;
typedef struct {int when,from,to,action,inside;} awk_rule;
typedef struct {char *pattern;u32 hash;re_program program;} awk_regex;
static awk_node awk_nodes[AWK_NODES];static int awk_node_count;
static awk_variable awk_variables[AWK_VARIABLES];static int awk_variable_count;
static awk_element awk_arrays[AWK_ARRAYS];static int awk_array_count;
static awk_rule awk_rules[128];static int awk_rule_count;
static awk_regex awk_regexes[136];static int awk_regex_cursor,awk_regex_literals_count;
/* 词法字符串不跨调用存活，正文立即进入literal池；放BSS避免深层
 * 表达式中的每次lex都占32KiB栈，挤穿SCX的128KiB栈合同。 */
static char awk_lex_buffer[32768];
static char awk_program[32768],awk_literals[32768],awk_record[AWK_RECORD+1],*awk_scratch;
static u32 awk_scratch_used,awk_field_end,awk_heap_used;static int awk_literal_used;
static const char *awk_cursor,*awk_token_start,*awk_token_text;static int awk_token,awk_error,awk_parse_depth,awk_print_context;
static int awk_number,awk_record_fields,awk_nr,awk_fnr,awk_loop_depth,awk_flow,awk_exit_status,awk_runtime_depth;static u32 awk_steps;
static awk_value awk_fields[AWK_FIELDS];static int awk_fs,awk_ofs,awk_ors,awk_filename;
static int awk_expression(int);
static int awk_statement(void);
static awk_value awk_evaluate(int);
static void awk_execute(int);
static int awk_record_dirty,awk_fields_changed;
static awk_value awk_integer_value(int n){return (awk_value){0,n,0};}
static awk_value awk_string_value(const char *p,int detect)
{int n=0;int kind=detect && cli_integer(p,&n)==0?2:1;return (awk_value){p,n,kind};}
static void awk_fail(const char *why)
{if(!awk_error){cli_text(2,"awk: ");cli_text(2,why);cli_text(2,"\n");}awk_error=1;}
static char *awk_temporary(u32 bytes)
{if(bytes>262144-awk_scratch_used){awk_fail("expression storage limit");return 0;}char *p=awk_scratch+awk_scratch_used;awk_scratch_used+=bytes;return p;}
static char *awk_duplicate(const char *p,u32 n)
{char *out=awk_temporary(n+1);if(!out)return 0;for(u32 i=0;i<n;i++)out[i]=p[i];out[n]=0;return out;}
static const char *awk_text(awk_value value)
{if(value.kind==3)return "";if(value.kind)return value.text;char *p=awk_temporary(12);if(!p)return "";decimal(p,value.number);return p;}
static int awk_numeric(awk_value value)
{if(value.kind==3)return 0;if(!value.kind || value.kind==2)return value.number;int n=0;return cli_integer(value.text,&n)==0?n:0;}
static int awk_true(awk_value value){return value.kind==1?value.text[0]!=0:value.kind==3?0:value.number!=0;}
static void awk_owned_free(awk_value *value)
{if((value->kind==1 || value->kind==2) && value->text){awk_heap_used-=(u32)length(value->text)+1;sc_free((void *)value->text);}*value=awk_integer_value(0);}
static int awk_owned_set(awk_value *target,awk_value value)
{
    if(value.kind && value.kind!=3){u32 n=(u32)length(value.text)+1;if(n>AWK_HEAP-awk_heap_used){awk_fail("persistent storage limit");return -1;}char *p=sc_alloc(n);if(!p){awk_fail("memory");return -1;}
        copy(p,value.text,(int)n);awk_owned_free(target);awk_heap_used+=n;value.text=p;}else awk_owned_free(target);*target=value;return 0;
}
static int awk_variable_find(const char *name,int create)
{for(int i=0;i<awk_variable_count;i++)if(equal(name,awk_variables[i].name))return i;if(!create)return -1;
    if(length(name)>31 || awk_variable_count==AWK_VARIABLES){awk_fail("variable limit");return -1;}int i=awk_variable_count++;copy(awk_variables[i].name,name,32);awk_variables[i].value=(awk_value){0,0,3};
    return i;}
static const char *awk_literal(const char *text,int n)
{if(n<0 || awk_literal_used+n+1>32768){awk_fail("literal limit");return "";}char *p=awk_literals+awk_literal_used;for(int i=0;i<n;i++)p[i]=text[i];p[n]=0;awk_literal_used+=n+1;return p;}
static int awk_hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static void awk_lex(void)
{
    for(;;){while(*awk_cursor==' '||*awk_cursor=='\t'||*awk_cursor=='\r')awk_cursor++;if(*awk_cursor=='#')while(*awk_cursor && *awk_cursor!='\n')awk_cursor++;
        if(awk_cursor[0]=='\\' && awk_cursor[1]=='\n'){awk_cursor+=2;continue;}break;}
    awk_token_start=awk_cursor;char c=*awk_cursor;if(!c){awk_token=0;return;}awk_cursor++;
    if(c=='\n'){awk_token=';';return;}
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'){const char *p=awk_token_start;while((*awk_cursor>='a'&&*awk_cursor<='z')||(*awk_cursor>='A'&&*awk_cursor<='Z')||(*awk_cursor>='0'&&*awk_cursor<='9')||*awk_cursor=='_')awk_cursor++;
        awk_token_text=awk_literal(p,(int)(awk_cursor-p));awk_token=equal(awk_token_text,"in")?A_IN:A_NAME;return;}
    if(c>='0'&&c<='9'){u32 n=(u32)(c-'0');while(*awk_cursor>='0'&&*awk_cursor<='9'){u32 d=(u32)(*awk_cursor++-'0');if(n>(0x7FFFFFFFu-d)/10){awk_fail("integer literal range");break;}n=n*10+d;}awk_number=(int)n;awk_token=A_NUMBER;return;}
    if(c=='"'){char *text=awk_lex_buffer;int used=0;while(*awk_cursor && *awk_cursor!='"'){c=*awk_cursor++;if(c=='\\'){c=*awk_cursor++;if(!c)break;if(c=='\n')continue;
            if(c=='n')c='\n';else if(c=='t')c='\t';else if(c=='r')c='\r';else if(c=='b')c='\b';else if(c=='f')c=12;else if(c=='v')c=11;else if(c=='a')c=7;
            else if(c=='x'){int d=awk_hex(*awk_cursor);if(d<0){awk_fail("hex escape");break;}u32 n=(u32)d;awk_cursor++;d=awk_hex(*awk_cursor);if(d>=0){n=n*16+(u32)d;awk_cursor++;}c=(char)n;}
            else if(c>='0'&&c<='7'){u32 n=(u32)(c-'0');for(int i=1;i<3 && *awk_cursor>='0'&&*awk_cursor<='7';i++)n=n*8+(u32)(*awk_cursor++-'0');c=(char)n;}}
        if(!c || used==32767){awk_fail("NUL/string limit");break;}text[used++]=c;}
        if(*awk_cursor!='"')awk_fail("unfinished string");else awk_cursor++;awk_token_text=awk_literal(text,used);awk_token=A_STRING;return;}
    char next=*awk_cursor;awk_token=(u8)c;
    if(c=='='&&next=='=')awk_token=A_EQ;else if(c=='!'&&next=='=')awk_token=A_NE;else if(c=='<'&&next=='=')awk_token=A_LE;else if(c=='>'&&next=='=')awk_token=A_GE;
    else if(c=='&'&&next=='&')awk_token=A_AND;else if(c=='|'&&next=='|')awk_token=A_OR;else if(c=='!'&&next=='~')awk_token=A_MATCH_NOT;
    else if(c=='+'&&next=='+')awk_token=A_INC;else if(c=='-'&&next=='-')awk_token=A_DEC;else if(c=='+'&&next=='=')awk_token=A_ADD_SET;else if(c=='-'&&next=='=')awk_token=A_SUB_SET;
    else if(c=='*'&&next=='=')awk_token=A_MUL_SET;else if(c=='/'&&next=='=')awk_token=A_DIV_SET;else if(c=='%'&&next=='=')awk_token=A_MOD_SET;else if(c=='*'&&next=='*')awk_token=A_POWER;
    if(awk_token!=(u8)c)awk_cursor++;
}
static int awk_named(const char *p){return awk_token==A_NAME && equal(awk_token_text,p);}
static int awk_new(int kind,int op,int a,int b,int c,const char *text)
{if(awk_node_count==AWK_NODES-1){awk_fail("syntax tree limit");return 0;}int i=++awk_node_count;awk_nodes[i]=(awk_node){kind,op,a,b,c,0,text};return i;}
static void awk_need(int token)
{if(awk_token!=token)awk_fail("syntax");else awk_lex();}
static int awk_lvalue(int n)
{return n && (awk_nodes[n].kind==A_VARIABLE || awk_nodes[n].kind==A_FIELD || awk_nodes[n].kind==A_ELEMENT);}
static int awk_function_name(const char *name)
{static const char *names[12]={"length","int","index","substr","tolower","toupper","match","split","sub","gsub","sprintf","system"};for(int i=0;i<12;i++)if(equal(name,names[i]))return 1;return 0;}
static int awk_primary(void)
{
    if(++awk_parse_depth>32){awk_fail("expression nesting");awk_parse_depth--;return 0;}int n=0;
    if(awk_token==A_NUMBER){n=awk_new(A_NUMERIC,awk_number,0,0,0,0);awk_lex();}
    else if(awk_token==A_STRING){n=awk_new(A_LITERAL,0,0,0,0,awk_token_text);awk_lex();}
    else if(awk_token==A_NAME){const char *name=awk_token_text,*name_end=awk_cursor;int function=awk_function_name(name);awk_lex();
        if(equal(name,"getline") || equal(name,"function") || equal(name,"return") || equal(name,"do") || equal(name,"nextfile")){awk_fail("keyword unsupported");}
        if(awk_token=='(' && (function || awk_token_start==name_end)){if(!function)awk_fail("user function unsupported");awk_lex();int first=0,last=0,count=0;
            while(awk_token!=')' && !awk_error){if(++count>32){awk_fail("call argument limit");break;}int arg=awk_expression(0);if(last)awk_nodes[last].next=arg;else first=arg;last=arg;if(awk_token!=',')break;awk_lex();}
            awk_need(')');n=awk_new(A_CALL,count,first,0,0,name);}
        else if(equal(name,"length"))n=awk_new(A_CALL,0,0,0,0,name);
        else {int v=awk_variable_find(name,1);n=awk_new(A_VARIABLE,v,0,0,0,name);if(awk_token=='['){awk_lex();int key=awk_expression(0);awk_need(']');n=awk_new(A_ELEMENT,v,key,0,0,name);}}}
    else if(awk_token=='$'){awk_lex();n=awk_new(A_FIELD,0,awk_primary(),0,0,0);}
    else if(awk_token=='('){int context=awk_print_context;awk_print_context=0;awk_lex();n=awk_expression(0);awk_need(')');awk_print_context=context;}
    else if(awk_token=='+'||awk_token=='-'||awk_token=='!'||awk_token==A_INC||awk_token==A_DEC){int op=awk_token;awk_lex();int arg=op==A_INC||op==A_DEC?awk_primary():awk_expression(11);if((op==A_INC||op==A_DEC) && !awk_lvalue(arg))awk_fail("increment target");n=awk_new(op==A_INC||op==A_DEC?A_CHANGE:A_UNARY,op,arg,0,0,0);}
    else if(awk_token=='/'){char text[2048];int used=0;while(*awk_cursor && *awk_cursor!='/' && *awk_cursor!='\n'){char c=*awk_cursor++;if(c=='\\' && *awk_cursor){char next=*awk_cursor++;if(next!='/'){if(used==2047)break;text[used++]='\\';}c=next;}if(used==2047)break;text[used++]=c;}
        if(*awk_cursor!='/')awk_fail("unfinished/large regex");else awk_cursor++;const char *pattern=awk_literal(text,used);n=awk_new(A_REGEX,0,0,0,0,pattern);awk_lex();}
    else awk_fail("expression/unsupported token");
    if(awk_token==A_INC||awk_token==A_DEC){int op=awk_token;awk_lex();if(!awk_lvalue(n))awk_fail("increment target");n=awk_new(A_CHANGE,op,n,1,0,0);}awk_parse_depth--;return n;
}
static int awk_operator(int token,int *level)
{
    if(token=='=' || token==A_ADD_SET || token==A_SUB_SET || token==A_MUL_SET || token==A_DIV_SET || token==A_MOD_SET){*level=1;return token;}
    if(token==A_OR){*level=3;return token;}if(token==A_AND){*level=4;return token;}if(token==A_IN){*level=5;return token;}
    if(token==A_EQ || token==A_NE || token==A_LE || token==A_GE || token=='<' || token=='>'){if(awk_print_context && token=='>')return 0;*level=6;return token;}
    if(token=='~' || token==A_MATCH_NOT){*level=7;return token;}if(token=='+' || token=='-'){*level=9;return token;}
    if(token=='*'||token=='/'||token=='%'){*level=10;return token;}if(token=='^'||token==A_POWER){*level=12;return '^';}
    if(token==A_NAME || token==A_NUMBER || token==A_STRING || token=='$' || token=='('){*level=8;return A_CAT;}return 0;
}
static int awk_expression_tail(int left,int minimum)
{
    while(!awk_error){if(awk_token=='?' && minimum<=2){awk_lex();int yes=awk_expression(0);awk_need(':');int no=awk_expression(2);left=awk_new(A_CONDITIONAL,0,left,yes,no,0);continue;}
        int level,op=awk_operator(awk_token,&level);if(!op || level<minimum)break;if(op!=A_CAT)awk_lex();int assignment=level==1;
        int right=awk_expression(assignment || op=='^'?level:level+1);if(assignment && !awk_lvalue(left))awk_fail("assignment target");if(op==A_IN && awk_nodes[right].kind!=A_VARIABLE)awk_fail("array name after in");
        left=awk_new(assignment?A_ASSIGN:A_BINARY,op,left,right,0,0);}
    return left;
}
static int awk_expression(int minimum)
{
    if(++awk_parse_depth>32){awk_fail("expression nesting");awk_parse_depth--;return 0;}
    int left=awk_expression_tail(awk_primary(),minimum);awk_parse_depth--;return left;
}
static void awk_separators(void){while(awk_token==';')awk_lex();}
static int awk_statement(void)
{
    if(++awk_parse_depth>32){awk_fail("statement nesting");awk_parse_depth--;return 0;}int n=0;
    if(awk_token=='{'){awk_lex();awk_separators();int first=0,last=0;while(awk_token && awk_token!='}' && !awk_error){int item=awk_statement();if(last)awk_nodes[last].next=item;else first=item;last=item;
            if(awk_token!=';' && awk_token!='}' && !(item && (awk_nodes[item].kind==A_BLOCK||awk_nodes[item].kind==A_IF||awk_nodes[item].kind==A_WHILE||awk_nodes[item].kind==A_FOR||awk_nodes[item].kind==A_FOR_IN)))awk_fail("statement separator");awk_separators();}
        awk_need('}');n=awk_new(A_BLOCK,0,first,0,0,0);}
    else if(awk_named("if")){awk_lex();awk_need('(');int condition=awk_expression(0);awk_need(')');awk_separators();int yes=awk_statement(),no=0;
        /* else前可有换行；保存词法现场，缺else时不吞掉外层分隔。 */
        const char *cursor=awk_cursor,*start=awk_token_start,*text=awk_token_text;int token=awk_token,number=awk_number;awk_separators();
        if(awk_named("else")){awk_lex();awk_separators();no=awk_statement();}else {awk_cursor=cursor;awk_token_start=start;awk_token_text=text;awk_token=token;awk_number=number;}n=awk_new(A_IF,0,condition,yes,no,0);}
    else if(awk_named("while")){awk_lex();awk_need('(');int condition=awk_expression(0);awk_need(')');awk_separators();int body=awk_statement();n=awk_new(A_WHILE,0,condition,body,0,0);}
    else if(awk_named("for")){awk_lex();awk_need('(');int first=awk_token==';'?0:awk_expression(0);
        if(awk_token==')' && first && awk_nodes[first].kind==A_BINARY && awk_nodes[first].op==A_IN){awk_need(')');awk_separators();n=awk_new(A_FOR_IN,0,awk_nodes[first].a,awk_nodes[first].b,awk_statement(),0);if(awk_nodes[awk_nodes[first].a].kind!=A_VARIABLE)awk_fail("for target");}
        else {awk_need(';');int condition=awk_token==';'?0:awk_expression(0);awk_need(';');int change=awk_token==')'?0:awk_expression(0);awk_need(')');awk_separators();int body=awk_statement();n=awk_new(A_FOR,body,first,condition,change,0);}}
    else if(awk_named("print") || awk_named("printf")){int kind=awk_named("printf")?A_PRINTF:A_PRINT;awk_lex();int first=0,last=0,count=0,wrapped=awk_token=='(';if(wrapped)awk_lex();awk_print_context=!wrapped;
        while(awk_token && awk_token!=';' && awk_token!='}' && (!wrapped || awk_token!=')') && !awk_error){if(++count>32){awk_fail("print argument limit");break;}int item=awk_expression(0);if(last)awk_nodes[last].next=item;else first=item;last=item;
            if(awk_token!=',')break;awk_lex();}
        if(wrapped){awk_need(')');awk_print_context=1;
            /* print (x),y的括号只围住首个表达式；不能把外部逗号
             * 留给语句分隔。单项括号后的算术/拼接仍属于首项。 */
            if(count==1){first=awk_expression_tail(first,0);last=first;}
            while(awk_token==',' && !awk_error){awk_lex();if(++count>32){awk_fail("print argument limit");break;}
                int item=awk_expression(0);awk_nodes[last].next=item;last=item;}}
        awk_print_context=0;if(awk_token=='>' || awk_token=='|')awk_fail("print redirection unsupported");if(kind==A_PRINTF && !count)awk_fail("printf needs format");n=awk_new(kind,count,first,0,0,0);}
    else if(awk_named("next")||awk_named("break")||awk_named("continue")){int kind=awk_named("next")?A_NEXT:awk_named("break")?A_BREAK:A_CONTINUE;awk_lex();n=awk_new(kind,0,0,0,0,0);}
    else if(awk_named("exit")){awk_lex();int value=awk_token && awk_token!=';' && awk_token!='}'?awk_expression(0):0;n=awk_new(A_EXIT,0,value,0,0,0);}
    else if(awk_named("delete")){awk_lex();int array=awk_primary();if(awk_nodes[array].kind!=A_ELEMENT && awk_nodes[array].kind!=A_VARIABLE)awk_fail("delete target");n=awk_new(A_DELETE,0,array,0,0,0);}
    else {int expression=awk_expression(0);n=awk_new(A_EXPRESSION,0,expression,0,0,0);}awk_parse_depth--;return n;
}
static void awk_parse(void)
{
    awk_cursor=awk_program;awk_lex();awk_separators();while(awk_token && !awk_error){if(awk_rule_count==128){awk_fail("rule limit");break;}awk_rule *rule=awk_rules+awk_rule_count++;
        if(awk_named("BEGIN")||awk_named("END")){rule->when=awk_named("BEGIN")?1:2;awk_lex();awk_separators();if(awk_token!='{')awk_fail("BEGIN/END needs block");rule->action=awk_statement();}
        else {if(awk_token!='{'){rule->from=awk_expression(0);if(awk_token==','){awk_lex();rule->to=awk_expression(0);}}
            rule->action=awk_token=='{'?awk_statement():awk_new(A_PRINT,0,0,0,0,0);}
        if(awk_token && awk_token!=';' && awk_nodes[rule->action].kind!=A_BLOCK)awk_fail("rule separator");awk_separators();}
}
static int awk_is_array[AWK_VARIABLES],awk_phase;
static int awk_step(void)
{if(awk_error)return -1;if(++awk_steps>1000000u){awk_fail("execution limit");return -1;}if(!(awk_steps&1023))sc_yield();return 0;}
static u32 awk_key_hash(int variable,const char *key)
{u32 hash=2166136261u^(u32)variable;for(int i=0;key[i];i++)hash=(hash^(u8)key[i])*16777619u;return hash;}
static awk_element *awk_element_find(int variable,const char *key,int create)
{
    if(variable<0 || variable>=AWK_VARIABLES || length(key)>1023){awk_fail("array key limit");return 0;}if(!awk_is_array[variable]){
        if(awk_variables[variable].value.kind!=3){awk_fail("scalar used as array");return 0;}awk_is_array[variable]=1;}
    u32 hash=awk_key_hash(variable,key);int vacant=-1;
    for(int i=0;i<AWK_ARRAYS;i++){int slot=(int)((hash+(u32)i)&(AWK_ARRAYS-1));awk_element *e=awk_arrays+slot;
        if(e->state==1 && e->hash==hash && e->variable==variable && equal(e->key,key))return e;if(e->state!=1 && vacant<0)vacant=slot;if(!e->state)break;}
    if(!create)return 0;if(vacant<0 || awk_array_count==512){awk_fail("array entry limit");return 0;}u32 bytes=(u32)length(key)+1;
    if(bytes>AWK_HEAP-awk_heap_used){awk_fail("persistent storage limit");return 0;}char *copy_key=sc_alloc(bytes);if(!copy_key){awk_fail("memory");return 0;}copy(copy_key,key,(int)bytes);
    awk_heap_used+=bytes;awk_array_count++;awk_element *e=awk_arrays+vacant;*e=(awk_element){hash,variable,1,copy_key,{0,0,3}};return e;
}
static void awk_element_delete(awk_element *e)
{if(!e || e->state!=1)return;awk_heap_used-=(u32)length(e->key)+1;sc_free(e->key);awk_owned_free(&e->value);e->state=2;awk_array_count--;}
static void awk_array_clear(int variable)
{for(int i=0;i<AWK_ARRAYS;i++)if(awk_arrays[i].state==1 && awk_arrays[i].variable==variable)awk_element_delete(awk_arrays+i);}
static re_program *awk_regex_get(const char *pattern)
{
    u32 hash=awk_key_hash(0,pattern);for(int i=0;i<136;i++)if(awk_regexes[i].pattern && awk_regexes[i].hash==hash && equal(awk_regexes[i].pattern,pattern))return &awk_regexes[i].program;
    if(length(pattern)>2047){awk_fail("regex limit");return 0;}awk_regex *slot=awk_regexes+128+awk_regex_cursor;re_program candidate={0};
    if(re_program_compile(&candidate,pattern,1,0)<0){awk_fail("regex unsupported/invalid");return 0;}u32 n=(u32)length(pattern)+1;char *text=sc_alloc(n);
    if(!text){re_program_free(&candidate);awk_fail("memory");return 0;}copy(text,pattern,(int)n);if(slot->pattern){sc_free(slot->pattern);re_program_free(&slot->program);}
    slot->pattern=text;slot->hash=hash;slot->program=candidate;awk_regex_cursor=(awk_regex_cursor+1)&7;return &slot->program;
}
static void awk_regex_pin(const char *pattern)
{
    u32 hash=awk_key_hash(0,pattern);for(int i=0;i<awk_regex_literals_count;i++)if(awk_regexes[i].hash==hash && equal(awk_regexes[i].pattern,pattern))return;
    if(awk_regex_literals_count==128){awk_fail("literal regex count");return;}re_program *program=awk_regex_get(pattern);if(!program)return;
    for(int i=128;i<136;i++)if(&awk_regexes[i].program==program){awk_regexes[awk_regex_literals_count++]=awk_regexes[i];awk_regexes[i]=(awk_regex){0,0,{0}};return;}
}
static void awk_regex_prepare(void)
{
    /* 源码中的固定pattern生命周期就是整个解释器，不能被动态8槽
     * 淘汰。否则包含9条rule的脚本会每条记录重新分配/编译NFA。 */
    for(int i=1;i<=awk_node_count && !awk_error;i++){awk_node *n=awk_nodes+i;int argument=0;
        if(n->kind==A_REGEX)awk_regex_pin(n->text);
        else if(n->kind==A_BINARY && (n->op=='~'||n->op==A_MATCH_NOT))argument=n->b;
        else if(n->kind==A_CALL){if(equal(n->text,"sub")||equal(n->text,"gsub"))argument=n->a;
            else if(equal(n->text,"match") && n->a)argument=awk_nodes[n->a].next;
            else if(equal(n->text,"split") && n->a){argument=awk_nodes[n->a].next;if(argument)argument=awk_nodes[argument].next;
                if(argument && awk_nodes[argument].kind==A_LITERAL && length(awk_nodes[argument].text)<2)argument=0;}}
        if(argument && awk_nodes[argument].kind==A_LITERAL)awk_regex_pin(awk_nodes[argument].text);}
}
static int awk_search(const char *text,const char *pattern,u32 start,u32 *begin,u32 *end)
{re_program *re=awk_regex_get(pattern);if(!re)return 0;return re_program_search(re,text,(u32)length(text),start,begin,end);}
static awk_value awk_snapshot(awk_value value)
{if(value.kind==1 || value.kind==2){const char *p=awk_duplicate(value.text,(u32)length(value.text));if(!p)return awk_integer_value(0);value.text=p;}return value;}
static int awk_part(const char *text,u32 n,int array,int index)
{
    if(index>=AWK_FIELDS){awk_fail("field limit");return -1;}char *p=awk_duplicate(text,n);if(!p)return -1;awk_value value=awk_string_value(p,1);
    if(array<0)awk_fields[index]=value;else {char number[12];decimal(number,index+1);awk_element *e=awk_element_find(array,number,1);if(!e || awk_owned_set(&e->value,value)<0)return -1;}return 0;
}
static int awk_split_string(const char *text,const char *separator,int array,int regex)
{
    u32 bytes=(u32)length(text),begin=0;int count=0;
    if(!regex && equal(separator," ")){for(u32 at=0;at<bytes;){while(at<bytes && (text[at]==' '||text[at]=='\t'||text[at]=='\n'))at++;begin=at;
            while(at<bytes && text[at]!=' ' && text[at]!='\t' && text[at]!='\n')at++;if(at>begin && awk_part(text+begin,at-begin,array,count++)<0)return -1;}return count;}
    if(!separator[0]){for(u32 i=0;i<bytes;i++)if(awk_part(text+i,1,array,count++)<0)return -1;return count;}if(!bytes)return 0;
    if(!regex && !separator[1]){for(u32 i=0;i<=bytes;i++)if(i==bytes || text[i]==separator[0]){if(awk_part(text+begin,i-begin,array,count++)<0)return -1;begin=i+1;}return count;}
    u32 seek=0;while(seek<=bytes){u32 left=0,right=0;if(!awk_search(text,separator,seek,&left,&right))break;if(left==right){seek=right+1;continue;}
        if(awk_part(text+begin,left-begin,array,count++)<0)return -1;begin=seek=right;}
    if(awk_error || awk_part(text+begin,bytes-begin,array,count++)<0)return -1;return count;
}
static int awk_record_split(void)
{int fields=awk_split_string(awk_record,awk_text(awk_variables[awk_fs].value),-1,0);if(fields<0)return -1;awk_record_fields=fields;awk_field_end=awk_scratch_used;awk_record_dirty=awk_fields_changed=0;return 0;}
static int awk_record_set(awk_value value)
{const char *text=awk_text(value);int n=length(text);if(n>AWK_RECORD){awk_fail("record limit");return -1;}copy(awk_record,text,AWK_RECORD+1);int r=awk_record_split();awk_fields_changed=1;return r;}
static int awk_record_rebuild(void)
{
    char out[AWK_RECORD+1];u32 used=0;const char *separator=awk_text(awk_variables[awk_ofs].value);u32 sn=(u32)length(separator);
    for(int i=0;i<awk_record_fields;i++){const char *part=awk_text(awk_fields[i]);u32 n=(u32)length(part);if(n+(i?sn:0)>AWK_RECORD-used){awk_fail("record limit");return -1;}
        if(i)for(u32 j=0;j<sn;j++)out[used++]=separator[j];for(u32 j=0;j<n;j++)out[used++]=part[j];}
    out[used]=0;copy(awk_record,out,AWK_RECORD+1);awk_record_dirty=0;return 0;
}
static const char *awk_record_current(void)
{if(awk_record_dirty)awk_record_rebuild();return awk_record;}
static void awk_fields_compact(void)
{
    if(awk_error)return;if(!awk_fields_changed){awk_scratch_used=awk_field_end;return;}char saved[AWK_RECORD+AWK_FIELDS+1];u32 offsets[AWK_FIELDS],sizes[AWK_FIELDS],used=0;
    /* 语句结束没有活跃表达式，再压紧字段正文；先复制到独立小缓冲，
     * 避免交换字段时覆盖尚未搬走的源。变量/数组另有持久所有权。
     * 修改字段只标脏，实际读取$0才重组，循环不会反复全记录拼接。 */
    for(int i=0;i<awk_record_fields;i++){if(!awk_fields[i].kind)continue;u32 n=(u32)length(awk_fields[i].text)+1;
        if(n>sizeof(saved)-used){awk_fail("field storage limit");return;}offsets[i]=used;sizes[i]=n;for(u32 j=0;j<n;j++)saved[used++]=awk_fields[i].text[j];}
    awk_scratch_used=0;for(int i=0;i<awk_record_fields;i++)if(awk_fields[i].kind){char *p=awk_temporary(sizes[i]);if(!p)return;for(u32 j=0;j<sizes[i];j++)p[j]=saved[offsets[i]+j];awk_fields[i].text=p;}
    awk_field_end=awk_scratch_used;awk_fields_changed=0;
}
static awk_value awk_pattern_value(int node)
{return awk_nodes[node].kind==A_REGEX?awk_string_value(awk_nodes[node].text,0):awk_evaluate(node);}
static int awk_append(char *out,u32 *used,const char *p,u32 n)
{if(n>65536-*used){awk_fail("output/string limit");return -1;}for(u32 i=0;i<n;i++)out[(*used)++]=p[i];out[*used]=0;return 0;}
static awk_value awk_format(awk_value *values,int count)
{
    char *out=awk_temporary(65537);if(!out)return awk_integer_value(0);u32 used=0;const char *format=awk_text(values[0]);int arg=1;
    for(const char *p=format;*p && !awk_error;p++){char c=*p;if(c!='%'){awk_append(out,&used,&c,1);continue;}p++;if(*p=='%'){awk_append(out,&used,"%",1);continue;}
        int left=0,zero=0;if(*p=='-'){left=1;p++;}if(*p=='0'){zero=1;p++;}u32 width=0;while(*p>='0'&&*p<='9'){width=width*10+(u32)(*p++-'0');if(width>4096){awk_fail("format width");break;}}
        int precision=-1;if(*p=='.'){p++;precision=0;while(*p>='0'&&*p<='9'){precision=precision*10+*p++-'0';if(precision>4096){awk_fail("format precision");break;}}}
        if(!*p || arg==count){awk_fail("format/argument count");break;}awk_value value=values[arg++];char number[36];const char *text=0;u32 n=0;
        if(*p=='s'){text=awk_text(value);n=(u32)length(text);if(precision>=0 && n>(u32)precision)n=(u32)precision;}
        else if(*p=='c'){char q=value.kind==1?value.text[0]:(char)awk_numeric(value);if(!q){awk_fail("NUL string unsupported");break;}number[0]=q;text=number;n=1;}
        else if(*p=='d'||*p=='i'||*p=='u'||*p=='x'||*p=='X'||*p=='o'){if(precision>=0){awk_fail("integer precision unsupported");break;}int signed_value=awk_numeric(value),minus=(*p=='d'||*p=='i') && signed_value<0;u32 v=minus?0u-(u32)signed_value:(u32)signed_value,base=*p=='o'?8:*p=='x'||*p=='X'?16:10;
            const char *digits=*p=='X'?"0123456789ABCDEF":"0123456789abcdef";do{number[n++]=digits[v%base];v/=base;}while(v);if(minus)number[n++]='-';for(u32 i=0;i<n/2;i++){char swap=number[i];number[i]=number[n-1-i];number[n-1-i]=swap;}text=number;}
        else {awk_fail("format supports integer/string only");break;}
        if(!left && zero && n && text[0]=='-' && width>n){awk_append(out,&used,"-",1);text++;n--;width--;}
        if(!left)for(u32 i=n;i<width;i++)awk_append(out,&used,zero?"0":" ",1);awk_append(out,&used,text,n);if(left)for(u32 i=n;i<width;i++)awk_append(out,&used," ",1);}
    out[used]=0;return awk_string_value(out,0);
}
typedef struct {int kind,variable,index;const char *key;} awk_reference;
static awk_reference awk_reference_make(int node)
{
    awk_node *n=awk_nodes+node;awk_reference ref={n->kind,n->op,0,0};
    if(n->kind==A_FIELD){ref.index=awk_numeric(awk_evaluate(n->a));if(ref.index<0 || ref.index>AWK_FIELDS)awk_fail("field index");}
    else if(n->kind==A_ELEMENT)ref.key=awk_text(awk_evaluate(n->a));else if(n->kind!=A_VARIABLE)awk_fail("assignment target");return ref;
}
static awk_value awk_variable_value(int index)
{
    if(index<0 || awk_is_array[index]){awk_fail("array used as scalar");return awk_integer_value(0);}const char *name=awk_variables[index].name;
    if(equal(name,"NF"))return awk_integer_value(awk_record_fields);if(equal(name,"NR"))return awk_integer_value(awk_nr);if(equal(name,"FNR"))return awk_integer_value(awk_fnr);
    if(equal(name,"RS"))return awk_string_value("\n",0);if(equal(name,"SUBSEP"))return awk_string_value("\034",0);
    if(equal(name,"OFMT")||equal(name,"CONVFMT"))return awk_string_value("%d",0);if(equal(name,"IGNORECASE"))return awk_integer_value(0);
    awk_value value=awk_variables[index].value;if(value.kind==3)return (awk_value){"",0,2};return awk_snapshot(value);
}
static awk_value awk_reference_get(awk_reference ref)
{
    if(awk_error)return awk_integer_value(0);if(ref.kind==A_VARIABLE)return awk_variable_value(ref.variable);
    if(ref.kind==A_FIELD){if(!ref.index)return awk_snapshot(awk_string_value(awk_record_current(),0));return ref.index<=awk_record_fields?awk_fields[ref.index-1]:(awk_value){"",0,2};}
    awk_element *e=awk_element_find(ref.variable,ref.key,1);if(!e)return awk_integer_value(0);return e->value.kind==3?(awk_value){"",0,2}:awk_snapshot(e->value);
}
static int awk_reference_set(awk_reference ref,awk_value value)
{
    if(awk_error)return -1;if(ref.kind==A_VARIABLE){const char *name=awk_variables[ref.variable].name;int result;
        if(equal(name,"ARGC")||equal(name,"RS")||equal(name,"OFMT")||equal(name,"CONVFMT")||equal(name,"SUBSEP")||equal(name,"IGNORECASE")){awk_fail("variable update unsupported");return -1;}
        if(awk_is_array[ref.variable]){awk_fail("array used as scalar");result=-1;}
        else if(equal(name,"NF")){int count=awk_numeric(value);if(count<0 || count>AWK_FIELDS){awk_fail("NF limit");result=-1;}
            else {while(awk_record_fields<count)awk_fields[awk_record_fields++]=awk_string_value("",0);awk_record_fields=count;awk_record_dirty=awk_fields_changed=1;result=0;}}
        else if(equal(name,"NR")){awk_nr=awk_numeric(value);result=0;}else if(equal(name,"FNR")){awk_fnr=awk_numeric(value);result=0;}
        else result=awk_owned_set(&awk_variables[ref.variable].value,value);return result;}
    if(ref.kind==A_FIELD){if(!ref.index)return awk_record_set(value);while(awk_record_fields<ref.index)awk_fields[awk_record_fields++]=awk_string_value("",0);
        awk_fields[ref.index-1]=awk_snapshot(value);awk_record_dirty=awk_fields_changed=1;awk_field_end=awk_scratch_used;return awk_error?-1:0;}
    if(equal(awk_variables[ref.variable].name,"ARGV")){awk_fail("ARGV update unsupported");return -1;}
    awk_element *e=awk_element_find(ref.variable,ref.key,1);return e?awk_owned_set(&e->value,value):-1;
}
static int awk_calculate(int op,int a,int b)
{
    if(op=='+'||op==A_ADD_SET)return (int)((u32)a+(u32)b);if(op=='-'||op==A_SUB_SET)return (int)((u32)a-(u32)b);if(op=='*'||op==A_MUL_SET)return (int)((u32)a*(u32)b);
    if(op=='/'||op=='%'||op==A_DIV_SET||op==A_MOD_SET){if(!b){awk_fail("division by zero");return 0;}int divide=op=='/'||op==A_DIV_SET;return a==(int)0x80000000u && b==-1?(divide?a:0):divide?a/b:a%b;}
    if(op=='^'){if(b<0 || b>31){awk_fail("integer exponent range");return 0;}u32 result=1,base=(u32)a;while(b){if(b&1)result*=base;base*=base;b>>=1;}return (int)result;}return 0;
}
static int awk_index(const char *text,const char *find)
{
    u32 bytes=(u32)length(text),n=(u32)length(find);if(!n)return 1;if(n>bytes)return 0;int *links=(int *)awk_temporary(n*4);if(!links)return 0;links[0]=0;
    for(u32 i=1,j=0;i<n;i++){while(j && find[i]!=find[j])j=(u32)links[j-1];if(find[i]==find[j])j++;links[i]=(int)j;}
    for(u32 i=0,j=0;i<bytes;i++){while(j && text[i]!=find[j])j=(u32)links[j-1];if(text[i]==find[j])j++;if(j==n)return (int)(i+2-n);}return 0;
}
static awk_value awk_call(awk_node *node)
{
    int arguments[32],count=0;for(int at=node->a;at;at=awk_nodes[at].next)arguments[count++]=at;const char *name=node->text;awk_value values[32];
    if(equal(name,"split")){if(count<2 || count>3 || awk_nodes[arguments[1]].kind!=A_VARIABLE){awk_fail("split arguments");return awk_integer_value(0);}
        awk_value text=awk_evaluate(arguments[0]);int array=awk_nodes[arguments[1]].op;const char *separator=count==3?awk_text(awk_pattern_value(arguments[2])):awk_text(awk_variables[awk_fs].value);
        if(!awk_is_array[array] && awk_variables[array].value.kind!=3){awk_fail("scalar used as array");return awk_integer_value(0);}awk_is_array[array]=1;awk_array_clear(array);
        return awk_integer_value(awk_split_string(awk_text(text),separator,array,count==3 && awk_nodes[arguments[2]].kind==A_REGEX));}
    if(equal(name,"sub") || equal(name,"gsub")){if(count<2 || count>3 || (count==3 && !awk_lvalue(arguments[2]))){awk_fail("sub arguments");return awk_integer_value(0);}
        const char *pattern=awk_text(awk_pattern_value(arguments[0])),*replacement=awk_text(awk_evaluate(arguments[1]));awk_reference ref=count==3?awk_reference_make(arguments[2]):(awk_reference){A_FIELD,0,0,0};
        const char *source=awk_text(awk_reference_get(ref));u32 bytes=(u32)length(source),position=0,search=0,used=0,changes=0;char *out=awk_temporary(65537);if(!out)return awk_integer_value(0);
        while(search<=bytes && !awk_error){u32 begin=0,end=0;if(!awk_search(source,pattern,search,&begin,&end))break;awk_append(out,&used,source+position,begin-position);
            for(int i=0;replacement[i];i++){char c=replacement[i];if(c=='&')awk_append(out,&used,source+begin,end-begin);
                else if(c=='\\' && replacement[i+1]){c=replacement[++i];if(c>='1'&&c<='9'){awk_fail("replacement backreference unsupported");break;}awk_append(out,&used,&c,1);}else awk_append(out,&used,&c,1);}
            changes++;position=search=end;if(begin==end){if(end==bytes)break;awk_append(out,&used,source+end,1);position=search=end+1;}if(equal(name,"sub"))break;if(awk_step()<0)break;}
        awk_append(out,&used,source+position,bytes-position);if(changes && !awk_error)awk_reference_set(ref,awk_string_value(out,0));return awk_integer_value((int)changes);}
    for(int i=0;i<count;i++)values[i]=equal(name,"match") && i==1?awk_pattern_value(arguments[i]):awk_evaluate(arguments[i]);if(awk_error)return awk_integer_value(0);
    if(equal(name,"length") && count<=1)return awk_integer_value(length(count?awk_text(values[0]):awk_record_current()));
    if(equal(name,"int") && count==1)return awk_integer_value(awk_numeric(values[0]));
    if(equal(name,"index") && count==2)return awk_integer_value(awk_index(awk_text(values[0]),awk_text(values[1])));
    if(equal(name,"substr") && count>=2 && count<=3){const char *text=awk_text(values[0]);int start=awk_numeric(values[1]);if(start<1)start=1;int bytes=length(text),n=count==3?awk_numeric(values[2]):bytes;
        if(start>bytes || n<=0)return awk_string_value("",0);if(n>bytes-start+1)n=bytes-start+1;char *p=awk_duplicate(text+start-1,(u32)n);return awk_string_value(p?p:"",0);}
    if((equal(name,"tolower")||equal(name,"toupper")) && count==1){const char *text=awk_text(values[0]);u32 n=(u32)length(text);char *p=awk_duplicate(text,n);if(!p)return awk_integer_value(0);
        for(u32 i=0;i<n;i++)if(equal(name,"tolower")){if(p[i]>='A'&&p[i]<='Z')p[i]+=32;}else if(p[i]>='a'&&p[i]<='z')p[i]-=32;return awk_string_value(p,0);}
    if(equal(name,"match") && count==2){u32 begin=0,end=0;int yes=awk_search(awk_text(values[0]),awk_text(values[1]),0,&begin,&end);
        int start=awk_variable_find("RSTART",1),size=awk_variable_find("RLENGTH",1);if(start>=0)awk_owned_set(&awk_variables[start].value,awk_integer_value(yes?(int)begin+1:0));if(size>=0)awk_owned_set(&awk_variables[size].value,awk_integer_value(yes?(int)(end-begin):-1));return awk_integer_value(yes?(int)begin+1:0);}
    if(equal(name,"sprintf") && count)return awk_format(values,count);
    if(equal(name,"system") && count==1){const char *text=awk_text(values[0]);char command[1024];int used=0;if(launch_argument(command,&used,"sh",1)<0 || launch_argument(command,&used,"-c",0)<0 || launch_argument(command,&used,text,0)<0){awk_fail("system command limit");return awk_integer_value(1);}
        int fds[3]={0,1,2},job=sc_spawn2(command,fds,0);return awk_integer_value(job<0?1:launch_wait((u32)job));}
    awk_fail("function/argument unsupported");return awk_integer_value(0);
}
static awk_value awk_evaluate(int node)
{
    if(!node)return awk_integer_value(1);if(awk_step()<0)return awk_integer_value(0);if(++awk_runtime_depth>64){awk_fail("runtime nesting");awk_runtime_depth--;return awk_integer_value(0);}
    awk_node *n=awk_nodes+node;awk_value result=awk_integer_value(0);
    if(n->kind==A_NUMERIC)result=awk_integer_value(n->op);else if(n->kind==A_LITERAL)result=awk_string_value(n->text,0);
    else if(n->kind==A_VARIABLE)result=awk_variable_value(n->op);else if(n->kind==A_FIELD || n->kind==A_ELEMENT)result=awk_reference_get(awk_reference_make(node));
    else if(n->kind==A_REGEX){u32 begin=0,end=0;result=awk_integer_value(awk_search(awk_record_current(),n->text,0,&begin,&end));}
    else if(n->kind==A_CALL)result=awk_call(n);
    else if(n->kind==A_UNARY){awk_value value=awk_evaluate(n->a);int number=awk_numeric(value);result=awk_integer_value(n->op=='!'?!awk_true(value):n->op=='-'?(int)(0u-(u32)number):number);}
    else if(n->kind==A_CONDITIONAL)result=awk_true(awk_evaluate(n->a))?awk_evaluate(n->b):awk_evaluate(n->c);
    else if(n->kind==A_ASSIGN){awk_reference ref=awk_reference_make(n->a);awk_value old=n->op=='='?awk_integer_value(0):awk_reference_get(ref),value=awk_evaluate(n->b);
        if(n->op!='=')value=awk_integer_value(awk_calculate(n->op,awk_numeric(old),awk_numeric(value)));awk_reference_set(ref,value);result=value;}
    else if(n->kind==A_CHANGE){awk_reference ref=awk_reference_make(n->a);int value=awk_numeric(awk_reference_get(ref));int changed=(int)((u32)value+(n->op==A_INC?1u:0xFFFFFFFFu));awk_reference_set(ref,awk_integer_value(changed));result=awk_integer_value(n->b?value:changed);}
    else if(n->kind==A_BINARY){awk_value a=awk_evaluate(n->a),b=awk_integer_value(0);int op=n->op;
        if(op==A_AND)result=awk_integer_value(awk_true(a) && awk_true(awk_evaluate(n->b)));else if(op==A_OR)result=awk_integer_value(awk_true(a) || awk_true(awk_evaluate(n->b)));
        else if(op==A_IN){const char *key=awk_text(a);result=awk_integer_value(awk_element_find(awk_nodes[n->b].op,key,0)!=0);}
        else {b=op=='~'||op==A_MATCH_NOT?awk_pattern_value(n->b):awk_evaluate(n->b);
            if(op=='~'||op==A_MATCH_NOT){u32 begin=0,end=0;int yes=awk_search(awk_text(a),awk_text(b),0,&begin,&end);result=awk_integer_value(op=='~'?yes:!yes);}
            else if(op==A_CAT){const char *left=awk_text(a),*right=awk_text(b);u32 an=(u32)length(left),bn=(u32)length(right);if(an+bn>65536)awk_fail("string limit");else {char *p=awk_temporary(an+bn+1);if(p){for(u32 i=0;i<an;i++)p[i]=left[i];for(u32 i=0;i<bn;i++)p[an+i]=right[i];p[an+bn]=0;result=awk_string_value(p,0);}}}
            else if(op==A_EQ||op==A_NE||op==A_LE||op==A_GE||op=='<'||op=='>'){int cmp;if(a.kind!=1 && b.kind!=1){int av=awk_numeric(a),bv=awk_numeric(b);cmp=av<bv?-1:av>bv?1:0;}
                else {const char *left=awk_text(a),*right=awk_text(b);cmp=text_compare(left,(u32)length(left),right,(u32)length(right),0);}result=awk_integer_value(op==A_EQ?cmp==0:op==A_NE?cmp!=0:op==A_LE?cmp<=0:op==A_GE?cmp>=0:op=='<'?cmp<0:cmp>0);}
            else result=awk_integer_value(awk_calculate(op,awk_numeric(a),awk_numeric(b)));}}
    else awk_fail("expression node");awk_runtime_depth--;return result;
}
static void awk_execute(int node)
{
    if(!node || awk_error || awk_flow || awk_step()<0)return;if(++awk_runtime_depth>64){awk_fail("runtime nesting");awk_runtime_depth--;return;}awk_node *n=awk_nodes+node;
    if(n->kind==A_BLOCK){for(int item=n->a;item && !awk_flow && !awk_error;item=awk_nodes[item].next)awk_execute(item);}
    else if(n->kind==A_EXPRESSION)awk_evaluate(n->a);
    else if(n->kind==A_PRINT || n->kind==A_PRINTF){awk_value values[32];int count=0;for(int item=n->a;item;item=awk_nodes[item].next)values[count++]=awk_evaluate(item);const char *text=0;
        if(n->kind==A_PRINTF)text=awk_text(awk_format(values,count));else {char *out=awk_temporary(65537);u32 used=0;if(out){if(!count){const char *record=awk_record_current();awk_append(out,&used,record,(u32)length(record));}
                const char *separator=awk_text(awk_variables[awk_ofs].value);for(int i=0;i<count;i++){if(i)awk_append(out,&used,separator,(u32)length(separator));const char *p=awk_text(values[i]);awk_append(out,&used,p,(u32)length(p));}
                const char *end=awk_text(awk_variables[awk_ors].value);awk_append(out,&used,end,(u32)length(end));text=out;}}
        if(!awk_error && text && cli_text(1,text)<0)awk_fail("output");}
    else if(n->kind==A_IF){if(awk_true(awk_evaluate(n->a)))awk_execute(n->b);else awk_execute(n->c);}
    else if(n->kind==A_WHILE || n->kind==A_FOR){if(++awk_loop_depth>32)awk_fail("loop nesting");if(n->kind==A_FOR && n->a)awk_evaluate(n->a);
        while(!awk_error && awk_true(awk_evaluate(n->kind==A_WHILE?n->a:n->b))){awk_execute(n->kind==A_WHILE?n->b:n->op);if(awk_flow==1){awk_flow=0;break;}if(awk_flow==2)awk_flow=0;else if(awk_flow)break;
            if(n->kind==A_FOR && n->c)awk_evaluate(n->c);if(awk_step()<0)break;awk_scratch_used=awk_field_end;}awk_loop_depth--;}
    else if(n->kind==A_FOR_IN){int array=awk_nodes[n->b].op;if(!awk_is_array[array] && awk_variables[array].value.kind!=3)awk_fail("for array");awk_is_array[array]=1;if(++awk_loop_depth>32)awk_fail("loop nesting");
        for(int i=0;i<AWK_ARRAYS && !awk_error;i++){awk_element *e=awk_arrays+i;if(e->state!=1 || e->variable!=array)continue;awk_value key=awk_string_value(e->key,0);awk_reference_set(awk_reference_make(n->a),key);awk_execute(n->c);
            if(awk_flow==1){awk_flow=0;break;}if(awk_flow==2)awk_flow=0;else if(awk_flow)break;awk_scratch_used=awk_field_end;}
        awk_loop_depth--;}
    else if(n->kind==A_NEXT){if(awk_phase!=3)awk_fail("next outside input rule");else awk_flow=3;}
    else if(n->kind==A_BREAK || n->kind==A_CONTINUE){if(!awk_loop_depth)awk_fail("loop control outside loop");else awk_flow=n->kind==A_BREAK?1:2;}
    else if(n->kind==A_EXIT){if(n->a)awk_exit_status=awk_numeric(awk_evaluate(n->a));if(awk_exit_status<0 || awk_exit_status>255)awk_fail("exit range");awk_flow=4;}
    else if(n->kind==A_DELETE){awk_node *array=awk_nodes+n->a;if(equal(awk_variables[array->op].name,"ARGV"))awk_fail("ARGV update unsupported");else if(array->kind==A_VARIABLE){if(!awk_is_array[array->op] && awk_variables[array->op].value.kind!=3)awk_fail("delete scalar");else {awk_is_array[array->op]=1;awk_array_clear(array->op);}}
        else {const char *key=awk_text(awk_evaluate(array->a));awk_element_delete(awk_element_find(array->op,key,0));}}
    else awk_fail("statement node");awk_runtime_depth--;awk_fields_compact();
}
static awk_value awk_cli_string(const char *text)
{
    u32 n=(u32)length(text);char *out=awk_temporary(n+1);if(!out)return awk_integer_value(0);u32 used=0;
    for(u32 i=0;i<n;i++){u8 c=(u8)text[i];if(c=='\\'){if(++i==n){awk_fail("unfinished CLI escape");break;}c=(u8)text[i];
            if(c=='n')c='\n';else if(c=='t')c='\t';else if(c=='r')c='\r';else if(c=='b')c='\b';else if(c=='f')c=12;else if(c=='v')c=11;else if(c=='a')c=7;
            else if(c>='0'&&c<='7'){u32 value=(u32)(c-'0');for(int j=1;j<3 && i+1<n && text[i+1]>='0'&&text[i+1]<='7';j++)value=value*8+(u32)(text[++i]-'0');c=(u8)value;}
            else if(c=='x'){int d=i+1<n?awk_hex(text[i+1]):-1;if(d<0){awk_fail("CLI hex escape");break;}u32 value=(u32)d;i++;d=i+1<n?awk_hex(text[i+1]):-1;if(d>=0){value=value*16+(u32)d;i++;}c=(u8)value;}}
        if(!c){awk_fail("NUL CLI string unsupported");break;}out[used++]=(char)c;}out[used]=0;return awk_string_value(out,1);
}
static int awk_cli_assignment(const char *text)
{
    const char *p=text;char name[32];int n=0;while(*p && *p!='='){char c=*p++;if(n==31 || !((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(n && c>='0'&&c<='9')))return 0;name[n++]=c;}
    if(!n || *p!='=')return 0;name[n]=0;int variable=awk_variable_find(name,1);if(variable<0)return -1;awk_reference ref={A_VARIABLE,variable,0,0};
    return awk_reference_set(ref,awk_cli_string(p+1))<0?-1:1;
}
static int awk_input(int fd,const char *name)
{
    if(awk_owned_set(&awk_variables[awk_filename].value,awk_string_value(name,0))<0)return 1;awk_fnr=0;text_reader reader={fd,{0},0,0};text_line line={0};int n=0;
    while(!awk_error && awk_flow!=4 && (n=text_line_read(&reader,&line))>0){if(line.size>AWK_RECORD){awk_fail("record limit");break;}
        for(u32 i=0;i<line.size;i++){if(!line.bytes[i]){awk_fail("NUL input unsupported");break;}awk_record[i]=line.bytes[i];}awk_record[line.size]=0;if(awk_error)break;
        if(awk_nr==0x7FFFFFFF || awk_fnr==0x7FFFFFFF){awk_fail("record counter range");break;}awk_nr++;awk_fnr++;awk_scratch_used=awk_field_end=awk_steps=0;awk_flow=0;awk_phase=3;
        if(awk_record_split()<0)break;
        for(int i=0;i<awk_rule_count && !awk_error && !awk_flow;i++){awk_rule *rule=awk_rules+i;if(rule->when)continue;int selected=!rule->from;
            if(rule->to){if(!rule->inside && awk_true(awk_evaluate(rule->from)))rule->inside=1;selected=rule->inside;if(selected && awk_true(awk_evaluate(rule->to)))rule->inside=0;}
            else if(rule->from)selected=awk_true(awk_evaluate(rule->from));if(selected)awk_execute(rule->action);awk_fields_compact();}
        if(awk_flow==3)awk_flow=0;sc_yield();}
    if(n<0)awk_fail("input line/read limit");text_line_free(&line);return awk_error;
}
static void awk_finish(void)
{for(int i=0;i<awk_variable_count;i++)awk_owned_free(&awk_variables[i].value);for(int i=0;i<AWK_ARRAYS;i++)if(awk_arrays[i].state==1)awk_element_delete(awk_arrays+i);
    for(int i=0;i<136;i++)if(awk_regexes[i].pattern){sc_free(awk_regexes[i].pattern);re_program_free(&awk_regexes[i].program);}if(awk_scratch)sc_free(awk_scratch);}
static int awk_main(void)
{
    if(cli_parse()<1)return 2;awk_scratch=sc_alloc(262144);if(!awk_scratch)return 1;int result=2,at=0,program_used=0;
    awk_fs=awk_variable_find("FS",1);awk_ofs=awk_variable_find("OFS",1);awk_ors=awk_variable_find("ORS",1);awk_filename=awk_variable_find("FILENAME",1);
    if(awk_owned_set(&awk_variables[awk_fs].value,awk_string_value(" ",0))<0 || awk_owned_set(&awk_variables[awk_ofs].value,awk_string_value(" ",0))<0 || awk_owned_set(&awk_variables[awk_ors].value,awk_string_value("\n",0))<0)goto done;
    for(;at<cli_argc;at++){const char *p=cli_argv[at];if(equal(p,"--")){at++;break;}if(equal(p,"-F") || (p[0]=='-' && p[1]=='F' && p[2])){
            const char *value=p[2]?p+2:++at<cli_argc?cli_argv[at]:0;if(!value || awk_owned_set(&awk_variables[awk_fs].value,awk_cli_string(value))<0 || awk_error)goto done;}
        else if(equal(p,"-v")){if(++at==cli_argc || awk_cli_assignment(cli_argv[at])!=1)goto done;}
        else if(equal(p,"-f")){if(++at==cli_argc)goto done;int fd=cli_input(at);if(fd<0)goto done;u32 bytes;char *text=cli_slurp(fd,32766,&bytes);if(fd)sc_stream_close(fd,0);if(!text)goto done;
            if((u32)program_used+bytes+2>32768){sc_free(text);goto done;}for(u32 i=0;i<bytes;i++){if(!text[i]){sc_free(text);awk_fail("NUL program");goto done;}awk_program[program_used++]=text[i];}
            awk_program[program_used++]='\n';awk_program[program_used]=0;sc_free(text);}
        else if(*p=='-' && p[1])goto done;else break;}
    if(!program_used){if(at==cli_argc)goto done;if(length(cli_argv[at])>32767)goto done;copy(awk_program,cli_argv[at++],32768);}
    int argc_variable=awk_variable_find("ARGC",1),argv_variable=awk_variable_find("ARGV",1);if(argc_variable<0 || argv_variable<0)goto done;
    if(awk_owned_set(&awk_variables[argc_variable].value,awk_integer_value(cli_argc-at+1))<0)goto done;
    awk_element *first=awk_element_find(argv_variable,"0",1);if(!first || awk_owned_set(&first->value,awk_string_value("awk",0))<0)goto done;
    for(int i=at;i<cli_argc;i++){char key[12];decimal(key,i-at+1);awk_element *e=awk_element_find(argv_variable,key,1);if(!e || awk_owned_set(&e->value,awk_string_value(cli_argv[i],0))<0)goto done;}
    awk_parse();awk_regex_prepare();if(awk_error)goto done;awk_phase=1;awk_scratch_used=awk_field_end=awk_steps=0;
    for(int i=0;i<awk_rule_count && !awk_error && !awk_flow;i++)if(awk_rules[i].when==1)awk_execute(awk_rules[i].action);
    int needs_input=0;for(int i=0;i<awk_rule_count;i++)if(awk_rules[i].when!=1)needs_input=1;
    if(needs_input && !awk_error && awk_flow!=4){int files=0;for(int i=at;i<cli_argc && !awk_error && awk_flow!=4;i++){int assignment=awk_cli_assignment(cli_argv[i]);if(assignment<0)break;if(assignment)continue;
            files++;int fd=cli_input(i);if(fd<0){awk_fail("input file");break;}awk_input(fd,cli_argv[i]);if(fd)sc_stream_close(fd,0);}if(!files && !awk_error && awk_flow!=4)awk_input(0,"-");}
    awk_flow=0;awk_phase=2;awk_steps=0;for(int i=0;i<awk_rule_count && !awk_error && !awk_flow;i++)if(awk_rules[i].when==2)awk_execute(awk_rules[i].action);
    result=awk_error?2:awk_exit_status;
done:
    awk_finish();return result;
}
