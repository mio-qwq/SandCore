#include "../SCLAUNCH.H"
#include "../SCTEXT.H"
static int permitted(const char *name)
{if(!*name)return 0;for(int i=0;name[i];i++)if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='A'&&name[i]<='Z')||(name[i]>='0'&&name[i]<='9')||name[i]=='_'||name[i]=='-'))return 0;return 1;}
int main(void)
{
    if(cli_parse()<1)return 2;int at=0,test=0,reverse=0;const char *extra[16];int extras=0;
    for(;at<cli_argc;at++){const char *p=cli_argv[at];if(equal(p,"--test"))test=1;else if(equal(p,"--reverse"))reverse=1;
        else if(equal(p,"-a")){if(++at==cli_argc || extras==16)return 2;extra[extras++]=cli_argv[at];}else if(equal(p,"--")){at++;break;}else if(*p=='-')return 2;else break;}
    if(cli_argc-at!=1)return 2;char path[64];if(cli_path(cli_argv[at],path)<0)return 2;char *list=sc_alloc(65536);char (*names)[64]=sc_alloc(1024*64);
    if(!list || !names){if(list)sc_free(list);if(names)sc_free(names);return 1;}int count=0,result=0;if(sc_dir(path,list,65536)<0){result=1;goto done;}
    for(const char *p=list;*p;){char kind=*p++;if(*p++!=' '){result=1;goto done;}char name[64];int n=0;while(*p && *p!=' ' && *p!='\n'){if(n==63){result=1;goto done;}name[n++]=*p++;}name[n]=0;
        while(*p && *p!='\n')p++;if(*p)p++;if(kind!='F' || !permitted(name))continue;if(count==1024){result=1;goto done;}copy(names[count++],name,64);}
    for(int i=1;i<count;i++){char name[64];copy(name,names[i],64);int j=i;while(j && text_compare(names[j-1],(u32)length(names[j-1]),name,(u32)length(name),0)>0){copy(names[j],names[j-1],64);j--;}copy(names[j],name,64);}
    for(int k=0;k<count;k++){int i=reverse?count-1-k:k;char full[65];full[0]='/';copy(full+1,path,64);if(path[0])append(full,"/",65);
        if(length(full)+length(names[i])>=65){result=1;break;}append(full,names[i],65);char command[1024];int used=0;
        if(launch_argument(command,&used,full,1)<0){result=1;break;}for(int a=0;a<extras;a++)if(launch_argument(command,&used,extra[a],0)<0){result=1;goto done;}
        if(test){if(cli_text(1,command)<0 || cli_text(1,"\n")<0){result=1;break;}continue;}
        int fds[3]={0,1,2},job=sc_spawn2(command,fds,0);if(job<0 || launch_wait((u32)job)){result=1;cli_text(2,"run-parts: failed: ");cli_text(2,full);cli_text(2,"\n");}}
done:
    sc_free(names);sc_free(list);return result;
}
