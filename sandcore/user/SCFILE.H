#ifndef SCFILE_H
#define SCFILE_H
#include "SCTEXT.H"
typedef int (*file_visit)(const char *,int,u32,int,void *);
static int file_walk(const char *path,int depth,file_visit visit,void *context)
{
    if(depth>32)return -2;char absolute[64];u32 info[2];if(cli_path(path,absolute)<0 || sc_stat(absolute,info)<0)return -1;
    int before=visit(absolute,(int)info[0],info[1],0,context);if(before<0)return before;
    if(info[0]==2 && !before){
        char *list=sc_alloc(65536);if(!list)return -4;int n=sc_dir(absolute,list,65536);if(n<0){sc_free(list);return n;}
        char *p=list;int result=0;
        while(*p){char kind=*p++;if(*p++!=' '){result=-1;break;}char name[64];int used=0;
            while(*p && *p!=' ' && *p!='\n'){if(used==63){result=-2;break;}name[used++]=*p++;}name[used]=0;
            while(*p && *p!='\n')p++;if(*p)p++;if(result)break;
            char child[64];copy(child,absolute,64);if(*child)append(child,"/",64);
            if(length(child)+used>=64){result=-2;break;}append(child,name,64);
            char rooted[65];rooted[0]='/';copy(rooted+1,child,64);
            result=file_walk(rooted,depth+1,visit,context);if(result<0)break;(void)kind;
        }sc_free(list);if(result<0)return result;
    }return visit(absolute,(int)info[0],info[1],1,context)<0?-1:0;
}
static const char *file_basename(const char *name)
{const char *last=name;for(const char *p=name;*p;p++)if(*p=='/' || *p=='\\')last=p+1;return last;}
static int file_join(char out[64],const char *parent,const char *name)
{if(length(parent)+length(name)+2>64)return -1;copy(out,parent,64);if(*out)append(out,"/",64);append(out,name,64);return 0;}
static int file_mode(const char *value,u32 *out)
{
    /* 三个0..3数字分别表示u/g/o的rw（r=1,w=2），没有x。 */
    if(length(value)!=3)return -1;u32 mode=0;for(int i=0;i<3;i++){if(value[i]<'0'||value[i]>'3')return -1;mode|=(u32)(value[i]-'0')<<(i*2);}*out=mode;return 0;
}
#endif
