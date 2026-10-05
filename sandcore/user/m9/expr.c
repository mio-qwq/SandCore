#include "../SCRE.H"
#include "../SCTEXT.H"
typedef struct {const char *text;} expr_value;
static int cursor,error,depth,arena_used;static char arena[4096];
static expr_value expression(int,int);
static expr_value number(int n)
{char value[12];decimal(value,n);int bytes=length(value)+1;if(arena_used+bytes>4096){error=1;return (expr_value){""};}char *p=arena+arena_used;copy(p,value,bytes);arena_used+=bytes;return (expr_value){p};}
static int truth(expr_value v){return v.text[0] && !equal(v.text,"0");}
static int match(expr_value value,expr_value pattern,int active)
{
    if(!active)return 0;re_program program={0};if(re_program_compile(&program,pattern.text,0,0)<0){error=1;return 0;}u32 begin=0,end=0;
    int yes=re_program_search(&program,value.text,(u32)length(value.text),0,&begin,&end);re_program_free(&program);return yes && !begin?(int)end:0;
}
static expr_value atom(int active)
{
    if(cursor==cli_argc || ++depth>32){error=1;depth--;return (expr_value){""};}const char *p=cli_argv[cursor++];expr_value out={p};
    if(equal(p,"(")){out=expression(0,active);if(cursor==cli_argc || !equal(cli_argv[cursor++],")"))error=1;}
    else if(equal(p,"+")){if(cursor==cli_argc)error=1;else out.text=cli_argv[cursor++];}
    else if(equal(p,"length")){expr_value v=atom(active);out=number(length(v.text));}
    else if(equal(p,"index")){expr_value v=atom(active),chars=atom(active);int found=0;for(int i=0;v.text[i] && !found;i++)for(int j=0;chars.text[j];j++)if(v.text[i]==chars.text[j]){found=i+1;break;}out=number(found);}
    else if(equal(p,"substr")){expr_value v=atom(active),from=atom(active),count=atom(active);int at,n;if(cli_integer(from.text,&at)<0 || cli_integer(count.text,&n)<0){error=1;out.text="";}
        else if(at<=0 || n<=0 || at>length(v.text))out.text="";else {int size=length(v.text)-at+1;if(size>n)size=n;
            if(arena_used+size+1>4096){error=1;out.text="";}else {char *to=arena+arena_used;for(int i=0;i<size;i++)to[i]=v.text[at-1+i];to[size]=0;arena_used+=size+1;out.text=to;}}}
    else if(equal(p,"match")){expr_value v=atom(active),re=atom(active);out=number(match(v,re,active));}
    depth--;return out;
}
static int operation(const char *p,int *level)
{
    if(equal(p,"|")){*level=1;return '|';}if(equal(p,"&")){*level=2;return '&';}
    if(equal(p,"=")||equal(p,"==")){*level=3;return 256;}if(equal(p,"!=")){*level=3;return 257;}if(equal(p,"<=")){*level=3;return 258;}if(equal(p,">=")){*level=3;return 259;}
    if(equal(p,"<")||equal(p,">")){*level=3;return *p;}if(equal(p,"+")||equal(p,"-")){*level=4;return *p;}
    if(equal(p,"*")||equal(p,"/")||equal(p,"%")){*level=5;return *p;}if(equal(p,":")){*level=6;return ':';}return 0;
}
static expr_value expression(int minimum,int active)
{
    if(++depth>32){error=1;depth--;return (expr_value){""};}expr_value left=atom(active);
    while(!error && cursor<cli_argc){int level,op=operation(cli_argv[cursor],&level);if(!op || level<minimum)break;cursor++;
        expr_value right=expression(level+1,active && !(op=='|' && truth(left)) && !(op=='&' && !truth(left)));if(!active)continue;
        if(op=='|'){if(!truth(left))left=truth(right)?right:(expr_value){"0"};}
        else if(op=='&'){if(!truth(left)||!truth(right))left.text="0";}
        else if(op==':')left=number(match(left,right,active));
        else {int a,b,an=cli_integer(left.text,&a)==0,bn=cli_integer(right.text,&b)==0;
            if(op=='+'||op=='-'||op=='*'||op=='/'||op=='%'){if(!an || !bn || ((op=='/'||op=='%') && !b)){error=1;break;}
                left=number(op=='+'?(int)((u32)a+(u32)b):op=='-'?(int)((u32)a-(u32)b):op=='*'?(int)((u32)a*(u32)b):a==(int)0x80000000u && b==-1?(op=='/'?a:0):op=='/'?a/b:a%b);}
            else {int cmp=an && bn?(a<b?-1:a>b?1:0):text_compare(left.text,(u32)length(left.text),right.text,(u32)length(right.text),0);
                left=number(op==256?cmp==0:op==257?cmp!=0:op==258?cmp<=0:op==259?cmp>=0:op=='<'?cmp<0:cmp>0);}}}
    depth--;return left;
}
int main(void)
{if(cli_parse()<1)return 2;if(equal(cli_argv[0],"--"))cursor++;expr_value out=expression(0,1);if(error || cursor!=cli_argc){cli_text(2,"expr: invalid/unsupported expression\n");return 2;}
    if(cli_text(1,out.text)<0 || cli_text(1,"\n")<0)return 2;return !truth(out);}
