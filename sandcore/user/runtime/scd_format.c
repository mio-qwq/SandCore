#include "SCDYN.H"

/* mio：SCD1 常驻在应用初始化载荷末，所有表读取先由减法范围检查
 * 支配。解析器不依赖 libc、浮点、对齐指针或本机结构体宽度。 */
unsigned int scd_read32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1]<<8)
        | ((unsigned int)p[2]<<16) | ((unsigned int)p[3]<<24);
}
static int scd_name(const unsigned char *strings,unsigned int size,unsigned int offset,int library)
{
    if(offset>=size || (offset && strings[offset-1]))return 0;
    unsigned int count=0,limit=library?35u:127u;
    while(count<size-offset && strings[offset+count]){
        if(count>=limit)return 0;
        unsigned int c=strings[offset+count];
        if(library){
            /* 最后四字节单独检查 .SCL，名字主体只接受系统库身份字母。 */
            if(c>=128u)return 0;
        }else if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_'
                   || (count && c>='0' && c<='9')))return 0;
        count++;
    }
    if(!count || count>=size-offset)return 0;
    if(!library)return 1;
    if(count<5u || strings[offset+count-4]!='.' || strings[offset+count-3]!='S'
       || strings[offset+count-2]!='C' || strings[offset+count-1]!='L')return 0;
    for(unsigned int i=0;i<count-4u;i++){
        unsigned int c=strings[offset+i];
        if(!((c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_'))return 0;
    }
    return 1;
}
static int scd_name_order(const unsigned char *a,const unsigned char *b)
{
    while(*a && *a==*b){a++;b++;}
    return (int)*a-(int)*b;
}
static int scd_table(const unsigned char *h,unsigned int size,unsigned int field,
                     unsigned int width,unsigned int *cursor)
{
    unsigned int offset=scd_read32(h+field),count=scd_read32(h+field+4u);
    if(offset!=*cursor || *cursor>size || count>(size-*cursor)/width)return 0;
    *cursor+=count*width;return 1;
}
int scd_validate(const void *metadata,unsigned int size,unsigned int metadata_rva)
{
    const unsigned char *h=metadata;
    if(!h || size<SCD_HEADER_SIZE || size>SCD_MAX_PROGRAM_IMAGE)return SCD_E_RANGE;
    const char *magic=SCD_MAGIC;
    for(unsigned int i=0;i<8u;i++)if(h[i]!=(unsigned char)magic[i])return SCD_E_FORMAT;
    if(scd_read32(h+SCD_H_VERSION)!=SCD_FORMAT_VERSION)return SCD_E_VERSION;
    if(scd_read32(h+SCD_H_HEADER_SIZE)!=SCD_HEADER_SIZE)return SCD_E_FORMAT;
    unsigned int load=scd_read32(h+SCD_H_PROGRAM_LOAD),bss=scd_read32(h+SCD_H_PROGRAM_BSS);
    if(scd_read32(h+SCD_H_METADATA_SIZE)!=size || !load || load>SCD_MAX_PROGRAM_IMAGE
       || bss>SCD_MAX_PROGRAM_IMAGE-load || metadata_rva>load || size!=load-metadata_rva
       || scd_read32(h+SCD_H_MAIN)<SCD_CRT_SIZE || scd_read32(h+SCD_H_MAIN)>=metadata_rva)return SCD_E_RANGE;
    unsigned int cursor=SCD_HEADER_SIZE;
    if(!scd_table(h,size,SCD_H_DEPS,SCD_DEPENDENCY_SIZE,&cursor)
       || !scd_table(h,size,SCD_H_IMPORTS,SCD_IMPORT_SIZE,&cursor)
       || !scd_table(h,size,SCD_H_RELOCS,SCD_RELOC_SIZE,&cursor))return SCD_E_RANGE;
    unsigned int strings_off=scd_read32(h+SCD_H_STRINGS),strings_size=scd_read32(h+SCD_H_STRINGS_SIZE);
    if(strings_off!=cursor || !strings_size || cursor>size || strings_size!=size-cursor)return SCD_E_RANGE;
    const unsigned char *strings=h+strings_off;
    if(strings[strings_size-1u])return SCD_E_SYMBOL;
    unsigned int dep_off=scd_read32(h+SCD_H_DEPS),dep_count=scd_read32(h+SCD_H_DEP_COUNT);
    unsigned int previous_name=0;
    for(unsigned int i=0;i<dep_count;i++){
        const unsigned char *d=h+dep_off+i*SCD_DEPENDENCY_SIZE;
        unsigned int name=scd_read32(d+SCD_D_NAME);
        if(!scd_name(strings,strings_size,name,1))return SCD_E_SYMBOL;
        if(!scd_read32(d+SCD_D_ABI_MAJOR))return SCD_E_VERSION;
        if(scd_read32(d+SCD_D_FLAGS))return SCD_E_FORMAT;
        if(i && scd_name_order(strings+previous_name,strings+name)>=0)return SCD_E_SYMBOL;
        previous_name=name;
    }
    unsigned int import_off=scd_read32(h+SCD_H_IMPORTS),import_count=scd_read32(h+SCD_H_IMPORT_COUNT);
    unsigned int previous_dep=0;
    previous_name=0;
    for(unsigned int i=0;i<import_count;i++){
        const unsigned char *d=h+import_off+i*SCD_IMPORT_SIZE;
        unsigned int name=scd_read32(d+SCD_I_NAME),dep=scd_read32(d+SCD_I_DEPENDENCY);
        unsigned int kind=scd_read32(d+SCD_I_KIND);
        if(!scd_name(strings,strings_size,name,0) || !scd_read32(d+SCD_I_SIZE)
           || (kind!=SCD_SYMBOL_FUNCTION && kind!=SCD_SYMBOL_DATA))return SCD_E_SYMBOL;
        if(dep>=dep_count)return SCD_E_RANGE;
        if(i && (dep<previous_dep || (dep==previous_dep
           && scd_name_order(strings+previous_name,strings+name)>=0)))return SCD_E_SYMBOL;
        previous_dep=dep;previous_name=name;
    }
    unsigned int reloc_off=scd_read32(h+SCD_H_RELOCS),reloc_count=scd_read32(h+SCD_H_RELOC_COUNT),previous_end=0;
    for(unsigned int i=0;i<reloc_count;i++){
        const unsigned char *r=h+reloc_off+i*SCD_RELOC_SIZE;
        unsigned int patch=scd_read32(r+SCD_R_PATCH),kind=scd_read32(r+SCD_R_TYPE);
        if(metadata_rva<4u || patch<SCD_CRT_SIZE || patch<previous_end || patch>metadata_rva-4u
           || scd_read32(r+SCD_R_IMPORT)>=import_count)return SCD_E_RANGE;
        if(kind!=SCD_RELOC_ABS32 && kind!=SCD_RELOC_PC32)return SCD_E_FORMAT;
        previous_end=patch+4u;
    }
    return 0;
}
