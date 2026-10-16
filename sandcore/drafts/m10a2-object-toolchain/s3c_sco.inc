/* SCO是独立TU的机器码，不借聚合源文件实现链接。
 * 复用shared后端仅为保留只读来源区间、静态绑定及真实指令回填记录；
 * 这里不装载清单、不解析跨TU符号，也不把旧SCX入口写进对象。 */
#include "SCOFMT.H"
typedef struct { int offset,where; } ScoCLine;
static ScoCLine *scoc_lines;
static int scoc_line_count,scoc_line_capacity;
static void sco_line(int where)
{
    if(!object_output || !where)return;
    if(scoc_line_count && scoc_lines[scoc_line_count-1].offset==code_used){
        scoc_lines[scoc_line_count-1].where=where;return;
    }
    if(scoc_line_count && scoc_lines[scoc_line_count-1].where==where)return;
    scoc_lines=sclc_grow(scoc_lines,scoc_line_count,&scoc_line_capacity,sizeof(ScoCLine));
    scoc_lines[scoc_line_count].offset=code_used;scoc_lines[scoc_line_count++].where=where;
}
static u32 sco_hash_word(u32 hash,u32 value)
{for(int i=0;i<4;i++){hash=(hash^(value&255u))*16777619u;value>>=8;}return hash;}
static u32 sco_type_hash(int type,int depth,u32 hash)
{
    if(depth>32)fatal("SCO by-value type nesting exceeds 32");
    Type *t=types+type;
    hash=sco_hash_word(hash,(u32)t->kind);hash=sco_hash_word(hash,(u32)t->size);
    hash=sco_hash_word(hash,(u32)(t->flags&Q_UNSIGNED));
    /* 指针的目标不影响本机cdecl传参布局，且不能沿自引用结构无限展开。
     * 这只是ABI形状指纹，公开C原型仍是源级类型合同。 */
    if(t->kind==TY_PTR)return hash;
    if(t->kind==TY_ARRAY){hash=sco_hash_word(hash,(u32)t->count);return sco_type_hash(t->base,depth+1,hash);}
    if(t->kind==TY_FUNC){
        hash=sco_hash_word(hash,(u32)t->count);hash=sco_hash_word(hash,(u32)t->flags);
        hash=sco_type_hash(t->base,depth+1,hash);
        for(int p=t->fields;p;p=parameters[p].next)hash=sco_type_hash(parameters[p].type,depth+1,hash);
    }else if(t->kind==TY_STRUCT || t->kind==TY_UNION){
        for(int f=t->fields;f;f=symbols[f].next){hash=sco_hash_word(hash,(u32)symbols[f].value);hash=sco_type_hash(symbols[f].type,depth+1,hash);}
    }
    return hash;
}
static u32 sco_string(u8 *target,u32 offset,const char *text)
{
    int n=length(text)+1;if(target)for(int i=0;i<n;i++)target[offset+(u32)i]=(u8)text[i];
    if((u32)n>SCO_MAX_FILE-offset)fatal("SCO strings exceed file budget");
    return offset+(u32)n;
}
static int sco_section(SclCSegment *segments,int count,u32 rva,u32 width,u32 *relative)
{
    for(int i=0;i<count;i++){
        SclCSegment *s=segments+i;u32 bytes=s->file_size?s->file_size:(u32)bss_used;
        if(rva>=s->rva && rva-s->rva<bytes && width<=bytes-(rva-s->rva)){
            *relative=rva-s->rva;
            if(s->access&SCL_SEG_X)return SCO_TEXT;
            if(!(s->access&SCL_SEG_W))return SCO_RODATA;
            return s->file_size?SCO_DATA:SCO_BSS;
        }
    }
    fatal("SCO source address is outside a retained section");return 0;
}
static void finish_sco(const char *output)
{
    SclCSegment segments[4];int count=sclc_segments(segments),used=0;
    int *lookup=sc_alloc((u32)symbol_count*4u);if(!lookup)fatal("SCO symbol lookup allocation failed");
    for(int i=0;i<symbol_count;i++)lookup[i]=-1;
    for(int i=0;i<fix_count;i++)if(fixes[i].kind!=F_DATA){
        int symbol=fixes[i].symbol;if(symbol<=0 || symbol>=symbol_count)fatal("SCO relocation symbol is invalid");lookup[symbol]=-2;
    }
    u32 strings=1u;
    for(int i=1;i<symbol_count;i++)if((symbols[i].kind==S_FUNCTION || symbols[i].kind==S_GLOBAL) && (symbols[i].defined || lookup[i]==-2)){
        if(!sclc_symbol_name(symbols[i].name))fatal("SCO symbol exceeds format name limits");
        if(!symbols[i].defined && (scl_symbol_storage[i]&2))fatal("SCO referenced static symbol is undefined");
        if(!symbols[i].defined && symbols[i].kind==S_FUNCTION && types[symbols[i].type].count<0)fatal("SCO external function needs a complete prototype");
        lookup[i]=used++;strings=sco_string(0,strings,spelling(symbols[i].name));
    }
    for(int i=0;i<source_count;i++)strings=sco_string(0,strings,sources[i].path);
    u32 section_off=SCO_HEADER,symbol_off=section_off+4u*SCO_SECTION_BYTES;
    u32 reloc_off=sclc_table_size(symbol_off,used,SCO_SYMBOL_BYTES);
    u32 file_off=sclc_table_size(reloc_off,fix_count,SCO_RELOC_BYTES);
    u32 line_off=sclc_table_size(file_off,source_count,SCO_FILE_BYTES);
    u32 string_off=sclc_table_size(line_off,scoc_line_count,SCO_LINE_BYTES);
    if(strings>SCO_MAX_FILE-string_off)fatal("SCO metadata exceeds file budget");
    u32 payload=(string_off+strings+3u)&~3u,file_size=payload;
    for(int i=0;i<count;i++){
        SclCSegment *s=segments+i;s->file_offset=0;
        if(s->file_size){file_size=(file_size+3u)&~3u;s->file_offset=file_size;
            if(s->file_size>SCO_MAX_FILE-file_size)fatal("SCO payload exceeds file budget");file_size+=s->file_size;}
    }
    u8 *image=sc_alloc(file_size);if(!image)fatal("SCO output allocation failed");
    for(u32 i=0;i<file_size;i++)image[i]=0;
    const char *magic=SCO_MAGIC;for(int i=0;i<8;i++)image[i]=(u8)magic[i];
    put32(image+8,SCO_VERSION);put32(image+12,SCO_HEADER);put32(image+16,SCO_MACHINE);put32(image+24,file_size);
    put32(image+28,section_off);put32(image+32,4);put32(image+36,symbol_off);put32(image+40,(u32)used);
    put32(image+44,reloc_off);put32(image+48,(u32)fix_count);put32(image+52,file_off);put32(image+56,(u32)source_count);
    put32(image+60,line_off);put32(image+64,(u32)scoc_line_count);put32(image+68,string_off);put32(image+72,strings);
    for(int k=1;k<=4;k++){
        u8 *row=image+section_off+(u32)(k-1)*SCO_SECTION_BYTES;
        put32(row,(u32)k);put32(row+16,4);put32(row+20,k==1?5u:k==2?1u:3u);
    }
    for(int i=0;i<count;i++){
        SclCSegment *s=segments+i;int k=(s->access&SCL_SEG_X)?1:!(s->access&SCL_SEG_W)?2:s->file_size?3:4;
        u8 *row=image+section_off+(u32)(k-1)*SCO_SECTION_BYTES;
        put32(row+4,s->file_offset);put32(row+8,s->file_size);put32(row+12,s->file_size?s->file_size:(u32)bss_used);
        if(k==1)for(int j=0;j<code_used;j++)image[s->file_offset+(u32)j]=code[36+j];
    }
    for(int i=0;i<sclc_span_count;i++){
        SclCSpan *s=sclc_spans+i;
        for(int j=0;j<count;j++)if((u32)s->rva>=segments[j].rva && (u32)s->rva-segments[j].rva<segments[j].file_size){
            u32 at=segments[j].file_offset+(u32)s->rva-segments[j].rva;
            for(int k=0;k<s->size;k++)image[at+(u32)k]=data[s->offset+k];break;
        }
    }
    u32 str=1u;
    for(int i=1;i<symbol_count;i++)if(lookup[i]>=0){
        Symbol *s=symbols+i;Type *t=types+s->type;u32 relative=0,section=0,size=(u32)scl_symbol_sizes[i],flags=0;
        if(s->defined)section=(u32)sco_section(segments,count,sclc_symbol_rva(i),size,&relative);
        else{size=s->kind==S_FUNCTION?1u:(u32)t->size;if(!size && t->kind==TY_ARRAY)size=(u32)types[t->base].size;}
        if(!size)fatal("SCO symbol has no complete storage size");
        if(s->kind==S_FUNCTION && t->count==0 && !t->flags){
            if(types[t->base].kind==TY_INT && !(types[t->base].flags&Q_UNSIGNED))flags=1u;
            if(types[t->base].kind==TY_VOID)flags=2u;
        }
        u8 *row=image+symbol_off+(u32)lookup[i]*SCO_SYMBOL_BYTES;
        put32(row,str);str=sco_string(image+string_off,str,spelling(s->name));
        put32(row+4,(scl_symbol_storage[i]&2)?SCO_LOCAL:SCO_GLOBAL);put32(row+8,s->kind==S_FUNCTION?SCO_FUNCTION:SCO_OBJECT);
        put32(row+12,section);put32(row+16,relative);put32(row+20,size);
        /* 不完整extern数组的整个长度可在另一个TU完成，链接器比较实际
         * 定义容量；不能把未知长度的指纹当作确定长度不兼容。 */
        flags|=(u32)(s->kind==S_FUNCTION?1:alignment(s->type))<<8;
        put32(row+24,s->kind==S_GLOBAL?0u:sco_type_hash(s->type,0,2166136261u));put32(row+28,flags);
    }
    for(int i=0;i<fix_count;i++){
        Fix *f=fixes+i;u32 relative=0,section,target,kind=SCO_TARGET_SYMBOL,addend=(u32)f->addend-(f->kind==F_REL?4u:0u);
        if(f->offset<0)section=(u32)sco_section(segments,count,(u32)sclc_source_rva(-f->offset-1,4),4,&relative);
        else{section=SCO_TEXT;relative=(u32)f->offset;if(relative>(u32)code_used-4u)fatal("SCO code patch exceeds text");}
        if(f->kind==F_DATA){u32 offset=0;target=(u32)sco_section(segments,count,(u32)sclc_source_rva(f->symbol,1),1,&offset);kind=SCO_TARGET_SECTION;addend+=offset;}
        else target=(u32)lookup[f->symbol];
        u8 *row=image+reloc_off+(u32)i*SCO_RELOC_BYTES;
        put32(row,section);put32(row+4,relative);put32(row+8,f->kind==F_REL?SCO_PC32:SCO_ABS32);
        put32(row+12,kind);put32(row+16,target);put32(row+20,addend);
        u8 *sr=image+section_off+(section-1u)*SCO_SECTION_BYTES;
        put32(image+scl_read32(sr+4)+relative,0);
    }
    for(int i=0;i<source_count;i++){
        u8 *row=image+file_off+(u32)i*SCO_FILE_BYTES;u32 hash=2166136261u;
        put32(row,str);str=sco_string(image+string_off,str,sources[i].path);
        for(int j=0;j<sources[i].size;j++)hash=(hash^(u8)source_arena[sources[i].source+j])*16777619u;
        put32(row+4,(u32)sources[i].size);put32(row+8,hash);
    }
    for(int i=0;i<scoc_line_count;i++){
        u8 *row=image+line_off+(u32)i*SCO_LINE_BYTES;
        put32(row,SCO_TEXT);put32(row+4,(u32)scoc_lines[i].offset);
        put32(row+8,((u32)scoc_lines[i].where>>16)&32767u);put32(row+12,(u32)scoc_lines[i].where&65535u);
    }
    if(str!=strings)fatal("SCO string layout mismatch");
    if(sc_write(map_path,map_text,map_used)!=map_used)fatal("cannot write SCO source map");
    if(sc_write(output,image,(int)file_size)!=(int)file_size)fatal("cannot write SCO object");
    sc_free(image);sc_free(lookup);if(scoc_lines)sc_free(scoc_lines);if(sclc_spans)sc_free(sclc_spans);
    sc_write(log_path,"SCCC OK SCO\n",12);sc_puts("SCCC OK SCO\n");
}
