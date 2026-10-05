/* mio：SCCC 运行语义验收。失败编号写入文件，不能仅凭编译成功判断。
 * 宏、数据布局、控制流、递归、指针/函数指针与内联 syscall 都真实执行。
 */
#include "SCAPI.H"
#define INC(x) ((x)+1)
#define ALIAS INC
#define TEXT(x) #x
#define JOIN(a,b) a##b
#define VAR(a,...) ((a)+(__VA_ARGS__))
#if defined(INC) && (1 || 1/0)
#define CHOSEN 13
#else
#error inactive test failed
#endif
#undef ALIAS
#define ALIAS INC

typedef struct { char c; short s; int n; int a[2]; } Item;
union Word { unsigned int u; unsigned char b[4]; };
enum { E_ONE=1,E_SEVEN=E_ONE*7,E_NEXT };
static int seed=9;
static int *seed_ptr=&seed;
static int matrix[2][3]={{1,2,3},{4,5,6}};
static char literal[]="A\0B";
static const char *name="SCCC";
static int fib(int n) { if(n<2) return n; return fib(n-1)+fib(n-2); }
static int plus(int a,int b) { return a+b; }
static int (*adder)(int,int)=plus;
static char result_path[64];
static int fail(int n)
{
    char text[16]; decimal(text,n); sc_write(result_path,text,length(text)); return n;
}
int main(void)
{
    int win=sc_open("SCCC semantics / mio",306,164);
    sc_args(result_path,sizeof(result_path));
    if(!result_path[0]) copy(result_path,"HOME/C-RESULT",sizeof(result_path));
    sc_write(result_path,"RUN",3);
    if(INC(INC(2))!=4 || ALIAS(3)!=4 || VAR(1,2,3)!=4 || CHOSEN!=13) return fail(1);
    int JOIN(va,lue)=21;
    if(value!=21 || !equal(TEXT(a+b),"a+b") || !equal(TEXT(a /*gap*/ + b),"a + b")) return fail(2);
    if(E_SEVEN!=7 || E_NEXT!=8 || sizeof(Item)!=16 || sizeof(matrix)!=24) return fail(3);
    Item x={2,300,41,{5,6}},y; y=x; y.n++; Item *p=&y;
    if(p->c!=2 || p->s!=300 || p->n!=42 || p->a[1]!=6 || x.n!=41) return fail(4);
    union Word u; u.u=0x12345678u; if(u.b[0]!=0x78 || u.b[3]!=0x12) return fail(5);
    int *q=&matrix[0][0]; if(matrix[1][2]!=6 || *(q+4)!=5 || (q+5)-q!=5) return fail(6);
    if(*seed_ptr!=9 || literal[1]!=0 || literal[2]!='B' || name[2]!='C') return fail(7);
    if(fib(8)!=21 || adder(5,7)!=12) return fail(8);
    int sum=0; for(int i=0;i<8;i++) { if(i==2) continue; if(i==6) break; sum+=i; }
    if(sum!=13) return fail(9);
    int count=0; while(count<4) { count++; if(count==2) continue; sum++; }
    do { sum--; } while(sum>14); if(sum!=14) return fail(10);
    int sw=0; switch(2) { case 1: sw=3; break; case 2: sw=7; case 3: sw+=4; break; default: sw=99; }
    if(sw!=11) return fail(11);
    unsigned int big=0xFFFFFFFFu;
    if(big/3!=1431655765u || big%3!=0 || big>>31!=1 || !(big>2)) return fail(12);
    signed char c=(char)255; unsigned short s=(unsigned short)65537;
    if(c!=-1 || s!=1 || ((short)65535)!=-1 || (-9/2)!=-4 || (-9%2)!=-1) return fail(13);
    int side=0; if((0 && side++) || !(1 || side++) || side || (1?7:1/0)!=7) return fail(14);
    int a[3]={1},*ptr=a; int old=*ptr++; if(old!=1 || ptr!=a+1 || a[1]!=0 || a[2]!=0) return fail(15);
    int side2=0; a[side2++]+=4; if(side2!=1 || a[0]!=5) return fail(16);
    int cv=7; const int *cp=&cv; if(*cp!=7 || sizeof(*cp)!=4) return fail(17);
    int go=0; goto later; go=99;
later: go++; if(go!=1) return fail(18);
    sc_write(result_path,"PASS",4);
    page(win,"SCCC native C + macros","Esc: return"); sc_text(win,8,42,"18 semantic groups: PASS",PAL_TITLE);
    wait_escape(); return 0;
}
