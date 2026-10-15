#include "SCUI.H"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

/* 预期边界仅来自官方15.1 divide/multiply标记，应用不含SCUI实现。
 * 逐文档走正式API，既测正向也测反向；不投造KEY来代替内核输入。 */
static ScU32 cases,boundaries;
static ScU32 word(const unsigned char *p)
{return (ScU32)p[0]|((ScU32)p[1]<<8)|((ScU32)p[2]<<16)|((ScU32)p[3]<<24);}
static ScU32 crc32(const unsigned char *p,ScU32 bytes)
{
    ScU32 crc=0xffffffffu;
    for(ScU32 i=0;i<bytes;i++){
        crc^=p[i];for(ScU32 bit=0;bit<8;bit++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    return crc^0xffffffffu;
}
static int corpus(const char *path,ScU32 kind,ScU32 expected_cases)
{
    FILE *file=fopen(path,"rb");unsigned char *data=0;long length;int status=1;
    if(!file){printf("SCUI_TEXT_FAIL open %s\n",path);return 1;}
    if(fseek(file,0,SEEK_END) || (length=ftell(file))<32 || length>65536 || fseek(file,0,SEEK_SET))goto done;
    data=(unsigned char *)malloc((size_t)length);if(!data)goto done;
    if(fread(data,1,(size_t)length,file)!=(size_t)length)goto done;
    if(memcmp(data,"SCUB15_1",8) || word(data+8)!=1 || word(data+12)!=kind
        || word(data+16)!=expected_cases || word(data+20)!=(ScU32)length || word(data+28)
        || crc32(data+32,(ScU32)length-32)!=word(data+24))goto done;
    ScU32 offset=32;
    for(ScU32 row=0;row<expected_cases;row++){
        if(offset>(ScU32)length || (ScU32)length-offset<12)goto done;
        ScU32 bytes=word(data+offset),count=word(data+offset+4),line=word(data+offset+8);offset+=12;
        if(!bytes || bytes>4096 || count<2 || count>bytes+1)goto done;
        ScU32 padded=(bytes+3u)&~3u;
        if(padded>(ScU32)length-offset || count>((ScU32)length-offset-padded)/4u)goto done;
        unsigned char *text=data+offset,*expected=text+padded;offset+=padded+count*4u;
        if(word(expected)!=0 || word(expected+(count-1u)*4u)!=bytes)goto done;
        for(ScU32 i=bytes;i<padded;i++)if(text[i])goto done;
        for(ScU32 i=1;i<count;i++)if(word(expected+i*4u)<=word(expected+(i-1u)*4u))goto done;
        ScUiDocumentDesc description;ScUiDocument *document=0;
        memset(&description,0,sizeof(description));description.size=sizeof(description);description.version=1;
        description.flags=SC_UI_DOCUMENT_PRESERVE_EOL;description.text.size=sizeof(description.text);
        description.text.version=1;description.text.data=(const char *)text;description.text.bytes=bytes;
        ScStatus error=ScUiDocumentCreate(&description,&document);
        if(error<0){printf("SCUI_TEXT_FAIL create kind=%u line=%u error=%d\n",kind,line,error);goto done;}
        int failed=0;
        for(ScU32 i=0;i+1u<count;i++){
            ScU32 actual=0xdeadbeefu,at=word(expected+i*4u),wanted=word(expected+(i+1u)*4u);
            error=ScUiDocumentBoundary(document,at,kind,1,&actual);
            if(error<0 || actual!=wanted){
                printf("SCUI_TEXT_FAIL kind=%u line=%u direction=1 at=%u wanted=%u actual=%u error=%d\n",kind,line,at,wanted,actual,error);
                failed=1;break;
            }
            boundaries++;
        }
        if(!failed)for(ScU32 i=count-1u;i;i--){
            ScU32 actual=0xdeadbeefu,at=word(expected+i*4u),wanted=word(expected+(i-1u)*4u);
            error=ScUiDocumentBoundary(document,at,kind,-1,&actual);
            if(error<0 || actual!=wanted){
                printf("SCUI_TEXT_FAIL kind=%u line=%u direction=-1 at=%u wanted=%u actual=%u error=%d\n",kind,line,at,wanted,actual,error);
                failed=1;break;
            }
            boundaries++;
        }
        ScUiDocumentRelease(document);if(failed)goto done;cases++;
    }
    if(offset!=(ScU32)length)goto done;
    status=0;
done:
    free(data);if(fclose(file))status=1;
    if(status)printf("SCUI_TEXT_FAIL corpus kind=%u completed=%u\n",kind,cases);
    return status;
}
int main(void)
{
    if(corpus("/HOME/M10UI/GRAPHEME.SCT",SC_UI_BOUNDARY_GRAPHEME,1187))return 1;
    if(corpus("/HOME/M10UI/WORD.SCT",SC_UI_BOUNDARY_WORD,1826))return 1;
    printf("SCUI_TEXT_PASS version=15.1 cases=%u boundaries=%u\n",cases,boundaries);return 0;
}
