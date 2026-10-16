#include "NOTES2.H"
/* 聚合仅组织本应用私有职责；系统库仍从真正独立的SCL动态装载。 */
#include "notes_ui.c"
#include "notes_io.c"
#include "notes_actions.c"

/* MODERN Shell传的是原始参数尾部，不是已拆好的argv。这里只消费一条
 * document；不把多参数拼成路径，也不随意删除路径内部的引号。 */
static ScStatus n_start_path(char *argument,ScU32 capacity,ScU32 *bytes)
{
    if(*bytes>=capacity || strlen(argument)!=*bytes)return SC32_E_INVALID;
    ScU32 i=0,used=0,quoted=0,closed=0;
    while(i<*bytes && (argument[i]==' ' || argument[i]=='\t'))i++;
    if(i==*bytes){argument[0]=0;*bytes=0;return 0;}
    quoted=argument[i]=='"';if(quoted)i++;
    while(i<*bytes){unsigned char c=(unsigned char)argument[i++];
        if(quoted && c=='"'){closed=1;break;}
        if(!quoted && (c==' ' || c=='\t'))break;
        if((!quoted && c=='"') || c<32 || c==127)return SC32_E_INVALID;
        /* Shell只对双引号和反斜杠加转义；其它反斜杠序列不是本合同。
         * 原地压缩始终写在读指针之前，不覆盖尚未检查的尾部参数。 */
        if(quoted && c=='\\'){
            if(i==*bytes || (argument[i]!='"' && argument[i]!='\\'))return SC32_E_INVALID;
            c=(unsigned char)argument[i++];
        }
        argument[used++]=(char)c;
    }
    if(quoted && !closed)return SC32_E_INVALID;
    while(i<*bytes && (argument[i]==' ' || argument[i]=='\t'))i++;
    if(i!=*bytes || !used)return SC32_E_INVALID;
    argument[used]=0;*bytes=used;return 0;
}

int main(void)
{
    ScStatus status=n_build();
    if(status>=0){
        char argument[1024];ScU32 bytes=0,decoded=0;argument[0]=0;
        status=ScProcessArguments(argument,sizeof(argument),&bytes);
        if(status>=0){status=n_start_path(argument,sizeof(argument),&bytes);if(status>=0)decoded=1;}
        if(status>=0)status=n_load(bytes?argument:"/HOME/notes.txt",bytes?0:1);
        if(status<0){n_error(status,decoded?(bytes?argument:"/HOME/notes.txt"):"启动参数必须是单一路径");status=0;}
    }
    while(status>=0 && !notes.quit){
        status=ScUiContextStep(notes.ui,notes.io?1:SC32_INFINITE);
        if(status<0 || status==SC_UI_END)break;
        if(notes.io){ScU32 before=notes.io->offset;n_io_step();
            if(notes.io && notes.io->offset!=before)n_status(0);}
    }
    n_io_release();
    ScUiDialogRelease(notes.dialog);ScUiDialogRelease(notes.notice);
    ScUiContextRelease(notes.ui);ScUiDocumentRelease(notes.document);
    for(ScU32 i=0;i<5;i++)ScUiMenuRelease(notes.menus[i]);
    if(status<0){fprintf(stderr,"Notes 2 error=%d\n",status);return 1;}return 0;
}
