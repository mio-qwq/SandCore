#ifndef SCARITH_H
#define SCARITH_H
#include "SCIO.H"
/* 自写32位整数表达式。无浮点；加减乘按二补码回绕，除零明确
 * 失败；逻辑与/或和条件表达式跳过未选分支的运算错误。 */
typedef struct {const char *at;int error,depth;int (*variable)(const char *);} arithmetic_parser;
static void arithmetic_space(arithmetic_parser *p){while(*p->at==' '||*p->at=='\t'||*p->at=='\n')p->at++;}
static int arithmetic_expression(arithmetic_parser *,int,int);
static int arithmetic_atom(arithmetic_parser *p,int active)
{
    arithmetic_space(p);if(++p->depth>32){p->error=1;p->depth--;return 0;}int result=0;char c=*p->at;
    if(c=='+'||c=='-'||c=='!'||c=='~'){p->at++;int n=arithmetic_atom(p,active);result=c=='+'?n:c=='-'?(int)(0u-(u32)n):c=='!'?!n:~n;}
    else if(c=='('){p->at++;result=arithmetic_expression(p,0,active);arithmetic_space(p);if(*p->at!=')')p->error=1;else p->at++;}
    else if(c>='0'&&c<='9'){int base=10;u32 n=0;if(c=='0'){p->at++;base=8;if(*p->at=='x'||*p->at=='X'){base=16;p->at++;if(!*p->at)p->error=1;}}
        int digits=c=='0'&&base==8;while(*p->at){c=*p->at;int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
            if(d<0 || d>=base)break;digits=1;if(n>(0xFFFFFFFFu-(u32)d)/(u32)base)p->error=1;n=n*(u32)base+(u32)d;p->at++;}if(!digits)p->error=1;result=(int)n;}
    else if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'){char name[32];int n=0;while((c=*p->at) && ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')){if(n==31){p->error=1;break;}name[n++]=c;p->at++;}name[n]=0;result=active&&p->variable?p->variable(name):0;}
    else p->error=1;p->depth--;return result;
}
static int arithmetic_operator(arithmetic_parser *p,int *bytes,int *precedence)
{
    const char *s=p->at;*bytes=1;
    if(s[0]=='|'&&s[1]=='|'){*bytes=2;*precedence=1;return 256;}if(s[0]=='&'&&s[1]=='&'){*bytes=2;*precedence=2;return 257;}
    if(s[0]=='|' ){*precedence=3;return '|';}if(s[0]=='^'){*precedence=4;return '^';}if(s[0]=='&'){*precedence=5;return '&';}
    if((s[0]=='='||s[0]=='!') && s[1]=='='){*bytes=2;*precedence=6;return s[0]=='='?258:259;}
    if((s[0]=='<'||s[0]=='>') && s[1]==s[0]){*bytes=2;*precedence=8;return s[0]=='<'?262:263;}
    if(s[0]=='<'||s[0]=='>'){*precedence=7;if(s[1]=='='){*bytes=2;return s[0]=='<'?260:261;}return s[0];}
    if(s[0]=='+'||s[0]=='-'){*precedence=9;return s[0];}if(s[0]=='*'||s[0]=='/'||s[0]=='%'){*precedence=10;return s[0];}return 0;
}
static int arithmetic_expression(arithmetic_parser *p,int minimum,int active)
{
    if(++p->depth>32){p->error=1;p->depth--;return 0;}int left=arithmetic_atom(p,active);while(!p->error){arithmetic_space(p);int bytes,level,op=arithmetic_operator(p,&bytes,&level);if(!op || level<minimum)break;
        p->at+=bytes;int use=active && !(op==256&&left) && !(op==257&&!left);int right=arithmetic_expression(p,level+1,use);if(!active)continue;
        if(op=='+')left=(int)((u32)left+(u32)right);else if(op=='-')left=(int)((u32)left-(u32)right);else if(op=='*')left=(int)((u32)left*(u32)right);
        else if(op=='/'||op=='%'){if(!right){p->error=1;break;}left=left==(int)0x80000000u&&right==-1?(op=='/'?left:0):op=='/'?left/right:left%right;}
        else if(op==262||op==263){if(right<0||right>31){p->error=1;break;}left=op==262?(int)((u32)left<<right):left>>right;}
        else if(op==256)left=left||right;else if(op==257)left=left&&right;else if(op==258)left=left==right;else if(op==259)left=left!=right;
        else if(op==260)left=left<=right;else if(op==261)left=left>=right;else if(op=='<')left=left<right;else if(op=='>')left=left>right;
        else if(op=='&')left&=right;else if(op=='|')left|=right;else if(op=='^')left^=right;
    }
    arithmetic_space(p);if(!minimum && *p->at=='?'){p->at++;int yes=arithmetic_expression(p,0,active&&left);arithmetic_space(p);if(*p->at!=':')p->error=1;else p->at++;int no=arithmetic_expression(p,0,active&&!left);left=left?yes:no;}p->depth--;return left;
}
static int arithmetic_value(const char *text,int (*variable)(const char *),int *out)
{arithmetic_parser p={text,0,0,variable};int n=arithmetic_expression(&p,0,1);arithmetic_space(&p);if(p.error || *p.at)return -1;*out=n;return 0;}
#endif
