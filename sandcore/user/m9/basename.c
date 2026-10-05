#include "../SCFILE.H"
int main(void){if(cli_parse()<1 || cli_argc>2)return 2;char *name=cli_argv[0];int n=length(name);while(n>1 && (name[n-1]=='/' || name[n-1]=='\\'))name[--n]=0;
    const char *base=n==1 && name[0]=='/'?name:file_basename(name);u32 size=(u32)length(base);
    if(cli_argc==2){u32 suffix=(u32)length(cli_argv[1]);if(suffix<size && !text_compare(base+size-suffix,suffix,cli_argv[1],suffix,0))size-=suffix;}
    return cli_write(1,base,size)<0 || cli_text(1,"\n")<0;}
