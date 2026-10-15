#include "sc32_font.h"
#include "sc32_core.h"
#include "task.h"
#include "paging.h"
#include "ttf.h"
#include "../user/fontapi/SC32FONTWIRE.H"

typedef char font_query_request_16[(sizeof(Sc32FontQueryRequest)==16)?1:-1];
typedef char font_info_96[(sizeof(ScFontInfo)==96)?1:-1];
typedef char font_glyph_desc_32[(sizeof(ScFontGlyphDesc)==32)?1:-1];
typedef char font_glyph_1600[(sizeof(ScFontGlyph)==1600)?1:-1];
typedef char font_rows_match[(TTF_BITMAP_WORDS==400u)?1:-1];

static void font_zero(void *memory,u32 bytes)
{for(u32 n=0;n<bytes;n++)((u8 *)memory)[n]=0;}
static int font_face_valid(u32 face)
{return face==SC32_FONT_PHOENIX12 || face==SC32_FONT_PHOENIX16;}
static int font_publish(int pid,u32 output,void *value,u32 bytes)
{return paging_user_copy(TASK(pid).pd,output,value,bytes,1)?SC32_E_INVALID:SC32_OK;}

static int font_query(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScFontInfo));if(error)return error;
    Sc32FontQueryRequest input;
    error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
    if(!font_face_valid(input.face) || input.reserved)return SC32_E_INVALID;
    u32 raw[16];if(ttf_info(input.face,raw))return SC32_E_FAILED;
    ScFontInfo result;font_zero(&result,sizeof(result));
    result.size=sizeof(result);result.version=1;result.face=input.face;
    result.generation=raw[1];result.active=raw[3];
    result.source_kind=result.active?SC32_FONT_SOURCE_PHOENIX_TTF:SC32_FONT_SOURCE_NONE;
    result.glyph_count=raw[4];result.mapped_count=raw[5];result.units_per_em=raw[6];
    result.ascent=(i32)raw[7];result.descent=(i32)raw[8];result.line_gap=(i32)raw[9];
    result.data_bytes=raw[10];result.face_pages=raw[11];result.cache_pages=raw[12];
    result.cache_hits=raw[13];result.cache_misses=raw[14];result.horizontal_metrics=raw[15];
    result.native_pixels=input.face;result.flags=SC32_FONT_MONO;
    return font_publish(pid,output,&result,sizeof(result));
}

static int font_glyph(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScFontGlyph));if(error)return error;
    ScFontGlyphDesc input;
    error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
    if(!font_face_valid(input.face) || input.flags || input.reserved
       || input.scalar>0x10ffffu || (input.scalar>=0xd800u && input.scalar<=0xdfffu))return SC32_E_INVALID;
    if(!input.pixels || input.pixels>64u)return SC32_E_RANGE;
    u32 information[16];if(ttf_info(input.face,information))return SC32_E_FAILED;
    if(input.expected_generation && input.expected_generation!=information[1])return SC32_E_STALE;
    if(!information[3])return SC32_E_NOT_FOUND;
    /* 每任务只有4KiB内核栈：旧/新位图共用一个1600B候选，先复制
     * 64B头再就地转换。不能同时留两张大位图，也不向用户缓冲直接
     * 栅格化；完整typed输出预检先于缓存申请/命中计数等任何效果。
     * gate的IF=0在本次请求中保持字体epoch不变，不跨任务借暂存。 */
    union { ScFontGlyph value;u32 raw[TTF_BITMAP_WORDS]; } candidate;
    int status=ttf_bitmap(input.scalar,input.face,input.pixels,candidate.raw);
    if(status<0)return status==-4?SC32_E_MEMORY:SC32_E_FAILED;
    u32 header[16];for(u32 n=0;n<16;n++)header[n]=candidate.raw[n];
    ScFontGlyph *value=&candidate.value;
    font_zero(value,64);value->size=sizeof(*value);value->version=1;
    value->face=header[10];value->generation=header[1];value->glyph_id=header[2];
    value->mapped=header[3];value->pixels=header[4];value->advance=(i32)header[5];
    value->width=header[6];value->height=header[7];value->baseline=(i32)header[8];
    value->left=(i32)header[9];value->bearing_26_6=(i32)header[11];
    value->advance_26_6=(i32)header[12];value->flags=SC32_FONT_MONO;
    return font_publish(pid,output,value,sizeof(*value));
}

int sc32_font_call(int pid,u32 operation,u32 request,u32 output)
{
    switch(operation){
    case SC32_FONT_QUERY:return font_query(pid,request,output);
    case SC32_FONT_GLYPH:return font_glyph(pid,request,output);
    default:return SC32_E_NOT_SUPPORTED;
    }
}
