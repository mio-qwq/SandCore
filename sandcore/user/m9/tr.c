#include "../SCTEXT.H"
static int escaped(const char **cursor)
{const char *p=*cursor;int c=(u8)*p++;if(c=='\\'){if(!*p)return -1;c=(u8)*p++;if(c=='n')c='\n';else if(c=='t')c='\t';else if(c=='r')c='\r';else if(c=='a')c=7;else if(c=='b')c=8;else if(c=='v')c=11;else if(c=='f')c=12;
    else if(c>='0'&&c<='7'){int n=c-'0';for(int i=1;i<3 && *p>='0'&&*p<='7';i++)n=n*8+*p++-'0';if(n>255)return -1;c=n;}}*cursor=p;return c;}
static int character_class(int c,const char *name)
{int upper=c>='A'&&c<='Z',lower=c>='a'&&c<='z',digit=c>='0'&&c<='9',space=c==' '||(c>=9&&c<=13),print=c>=32&&c<=126;
    if(equal(name,"upper"))return upper;if(equal(name,"lower"))return lower;if(equal(name,"alpha"))return upper||lower;if(equal(name,"digit"))return digit;
    if(equal(name,"alnum"))return upper||lower||digit;if(equal(name,"space"))return space;if(equal(name,"blank"))return c==' '||c=='\t';if(equal(name,"cntrl"))return c<32||c==127;
    if(equal(name,"print"))return print;if(equal(name,"graph"))return print&&c!=32;if(equal(name,"punct"))return print&&c!=32&&!upper&&!lower&&!digit;
    if(equal(name,"xdigit"))return digit||(c>='A'&&c<='F')||(c>='a'&&c<='f');return -1;}
static int parse_set(const char *p,u8 out[256])
{
    int used=0;while(*p){
        if(p[0]=='[' && p[1]==':'){p+=2;char name[16];int n=0;while(*p && *p!=':'){if(n==15)return -1;name[n++]=*p++;}name[n]=0;
            if(*p++!=':' || *p++!=']')return -1;for(int c=0;c<256;c++){int yes=character_class(c,name);if(yes<0)return -1;if(yes){if(used==256)return -1;out[used++]=(u8)c;}}continue;}
        int lo=escaped(&p),hi=lo;if(lo<0)return -1;if(*p=='-' && p[1]){p++;hi=escaped(&p);if(hi<lo)return -1;}
        for(int c=lo;c<=hi;c++){if(used==256)return -1;out[used++]=(u8)c;}
    }return used;
}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0,complement=0,remove=0,squeeze=0;
    while(at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1]){for(int i=1;cli_argv[at][i];i++){char c=cli_argv[at][i];if(c=='c'||c=='C')complement=1;else if(c=='d')remove=1;else if(c=='s')squeeze=1;else return 2;}at++;}
    if(cli_argc-at<1 || cli_argc-at>2)return 2;u8 first[256],second[256],present[256],squeezed[256],map[256];int n=parse_set(cli_argv[at],first),m=cli_argc-at==2?parse_set(cli_argv[at+1],second):0;if(n<0||m<0)return 2;
    for(int i=0;i<256;i++){present[i]=squeezed[i]=0;map[i]=(u8)i;}for(int i=0;i<n;i++)present[first[i]]=1;
    if(complement){n=0;for(int i=0;i<256;i++)if(!present[i])first[n++]=(u8)i;for(int i=0;i<256;i++)present[i]^=1;}
    int translate=!remove && cli_argc-at==2;if(translate && !m)return 2;if(!remove && !squeeze && !translate)return 2;
    if(translate)for(int i=0;i<n;i++)map[first[i]]=second[i<m?i:m-1];
    if(squeeze){if(m)for(int i=0;i<m;i++)squeezed[second[i]]=1;else for(int i=0;i<n;i++)squeezed[first[i]]=1;}
    u8 input[4096],output[4096];int last=-1,count;while((count=cli_read(0,input,sizeof(input)))>0){u32 used=0;for(int i=0;i<count;i++){u8 c=input[i];if(remove && present[c])continue;c=map[c];if(squeeze && last==c && squeezed[c])continue;output[used++]=c;last=c;}
        if(cli_write(1,output,used)<0)return 1;}return count<0;
}
