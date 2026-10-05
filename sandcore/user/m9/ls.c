#include "../SCFILE.H"
typedef struct {char name[64];int kind;u32 size;} ls_entry;
static int long_form,all,recursive,directory_only,reverse,classify,sort_size,unsorted;
static int compare(const ls_entry *a,const ls_entry *b)
{int n=sort_size?(a->size>b->size?-1:a->size<b->size?1:0):0;if(!n)n=text_compare(a->name,(u32)length(a->name),b->name,(u32)length(b->name),0);return reverse?-n:n;}
static int show(const char *path,const char *label,int kind,u32 bytes)
{
    if(long_form){u32 meta[8];if(sc_fsmeta(path,meta)<0)return -1;char mode[8];mode[0]=kind==2?'d':'-';
        for(int i=0;i<3;i++){u32 rw=(meta[5]>>(i*2))&3;mode[1+i*2]=rw&1?'r':'-';mode[2+i*2]=rw&2?'w':'-';}mode[7]=0;
        if(cli_text(1,mode)<0 || cli_text(1," ")<0)return -1;cli_number(1,(int)meta[3]);cli_text(1," ");cli_number(1,(int)meta[4]);cli_text(1," ");text_unsigned(1,bytes);cli_text(1," ");}
    if(cli_text(1,label)<0 || (classify && kind==2 && cli_text(1,"/")<0) || cli_text(1,"\n")<0)return -1;return 0;
}
static void sort_entries(ls_entry *items,int count)
{
    /* 至多1024条，原地堆排序O(n log n)，正文不随递归目录复制。 */
    for(int step=count/2-1;step>=-count;step--){int root,end;ls_entry value;
        if(step>=0){root=step;end=count;value=items[root];}
        else {end=count+step;if(end<=0)break;value=items[end];items[end]=items[0];root=0;}
        while(root*2+1<end){int child=root*2+1;if(child+1<end && compare(&items[child],&items[child+1])<0)child++;
            if(compare(&value,&items[child])>=0)break;items[root]=items[child];root=child;}items[root]=value;
    }
}
static int listing(const char *name,int depth,int heading)
{
    if(depth>32)return -2;char path[64];u32 info[2];if(cli_path(name,path)<0 || sc_stat(path,info)<0)return -1;
    if(info[0]!=2 || directory_only)return show(path,name,(int)info[0],info[1]);
    if(heading){cli_text(1,name);cli_text(1,":\n");}
    char *buffer=sc_alloc(65536);ls_entry *items=sc_alloc(1026*sizeof(ls_entry));if(!buffer || !items){if(buffer)sc_free(buffer);if(items)sc_free(items);return -4;}
    int r=sc_dir(path,buffer,65536),count=0;if(r<0){sc_free(buffer);sc_free(items);return r;}
    if(all==1){items[count++]=(ls_entry){".",2,0};items[count++]=(ls_entry){"..",2,0};}
    const char *p=buffer;while(*p){if(count==1026){r=-2;goto done;}ls_entry *e=&items[count];e->kind=*p=='D'?2:1;p++;
        if(*p++!=' '){r=-1;goto done;}int n=0;while(*p && *p!=' ' && *p!='\n'){if(n==63){r=-2;goto done;}e->name[n++]=*p++;}e->name[n]=0;
        if(*p++!=' '){r=-1;goto done;}char size[12];n=0;while(*p && *p!='\n'){if(n==11){r=-1;goto done;}size[n++]=*p++;}size[n]=0;if(*p)p++;
        if(text_number(size,&e->size)<0){r=-1;goto done;}if(all || e->name[0]!='.')count++;
    }
    if(!unsorted)sort_entries(items,count);
    for(int i=0;i<count;i++){char child[64];if(equal(items[i].name,"."))copy(child,path,64);
        else if(equal(items[i].name,"..")){copy(child,path,64);int n=length(child);while(n && child[n-1]!='/')n--;child[n?n-1:0]=0;}
        else if(file_join(child,path,items[i].name)<0){r=-1;goto done;}
        if(show(child,items[i].name,items[i].kind,items[i].size)<0){r=-1;goto done;}}
    if(recursive){
        int directories=0;for(int i=0;i<count;i++)if(items[i].kind==2 && !equal(items[i].name,".") && !equal(items[i].name,".."))items[directories++]=items[i];
        sc_free(buffer);buffer=0;for(int i=0;i<directories;i++){char child[64],rooted[65];if(file_join(child,path,items[i].name)<0){r=-1;break;}
            rooted[0]='/';copy(rooted+1,child,64);cli_text(1,"\n");r=listing(rooted,depth+1,1);if(r<0)break;}
    }else r=0;
done:if(buffer)sc_free(buffer);sc_free(items);return r<0?r:0;
}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0;for(;at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1];at++){
        if(equal(cli_argv[at],"--")){at++;break;}for(int i=1;cli_argv[at][i];i++){char c=cli_argv[at][i];
            if(c=='l')long_form=1;else if(c=='a'||c=='A')all=c=='a'?1:2;else if(c=='R')recursive=1;else if(c=='d')directory_only=1;
            else if(c=='r')reverse=1;else if(c=='F'||c=='p')classify=1;else if(c=='S')sort_size=1;else if(c=='U')unsorted=1;else if(c=='1'){}else return 2;}}
    int result=0;if(at==cli_argc){int r=listing(".",0,recursive);return r<0?cli_error("ls",r):0;}
    for(int i=at;i<cli_argc;i++){if(i>at)cli_text(1,"\n");int r=listing(cli_argv[i],0,cli_argc-at>1 || recursive);if(r<0){cli_error(cli_argv[i],r);result=1;}}return result;
}
