#include "SCUIPRIVATE.H"
#include "../textdata/SCUTEXTDATA.H"

/* 属性表只有完整校验后才发布，编辑与普通文字绘制共享同一固定版本。 */
typedef struct {ScU32 first,last,flags;
}
UiUnicodeRange;
static unsigned char *ui_unicode_data;
static UiUnicodeRange *ui_unicode_ranges;
static ScU32 ui_unicode_count;
static ScStatus ui_unicode_prepare(void)
{
    ScFileOpenDesc d;
    ScFile f=0;
    ScFileInfo i;
    ScIoResult io;
    unsigned char *data=0;
    ScU32 done=0,count=0,crc=0xffffffffu;
    ScStatus s;
    if(ui_unicode_data)return 0;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.path="/SYS/UI/UAX15_1.SCU";
    d.flags=SC32_FILE_READ;
    s=ScFileOpen(&d,&f);
    if(s<0)return ui_cause(0,s,UI_OP_DATA,0);
    memset(&i,0,sizeof(i));
    i.size=sizeof(i);
    i.version=1;
    s=ScFileQuery(f,&i);
    if(s==0 && (i.length<64 || i.length>64u+0x110000u*12u))s=SC_UI_E_INVALID;
    if(s==0){data=(unsigned char *)malloc(i.length);
        if(!data)s=SC_UI_E_MEMORY;
    }
    while(s==0 && done<i.length){
        ScU32 take=i.length-done;
        if(take>65536u)take=65536u;
        memset(&io,0,sizeof(io));
        io.size=sizeof(io);
        io.version=1;
        s=ScFileRead(f,data+done,take,&io);
        if(s==0 && (!io.transferred || io.transferred>take))s=SC_UI_E_IO;
        if(s==0)done+=io.transferred;
    }
    {ScStatus closed=ScFileClose(f);
        if(s==0)s=closed;
    }
    if(s==0){
        count=ui_data_u32(data+32);
        if(memcmp(data,"SCUT15_1",8) || ui_data_u32(data+8)!=64 || ui_data_u32(data+12)!=1
            || ui_data_u32(data+16)!=15 || ui_data_u32(data+20)!=1 || ui_data_u32(data+24)
            || ui_data_u32(data+28)!=i.length || !count || count>(i.length-64)/12
            || 64+count*12!=i.length || ui_data_u32(data+36)!=12
            || ui_data_u32(data+44)!=0x10ffff || ui_data_u32(data+48)!=SC_UT_FLAGS_MASK
            || ui_data_u32(data+52) || ui_data_u32(data+56) || ui_data_u32(data+60))s=SC_UI_E_INVALID;
    }
    if(s==0){
        ScU32 next=0,last_flags=UINT_MAX;
        for(ScU32 n=0;n<count;n++){
            const unsigned char *p=data+64+n*12;
            ScU32 a=ui_data_u32(p),b=ui_data_u32(p+4),flags=ui_data_u32(p+8);
            if(a!=next || b<a || b>0x10ffff || flags&~SC_UT_FLAGS_MASK
                || (flags&15)>SC_UT_GCB_LVT || ((flags>>8)&31)>SC_UT_WB_WSEGSPACE
                || flags==last_flags){s=SC_UI_E_INVALID;
                break;
            }
            next=b+1;
            last_flags=flags;
        }
        if(next!=0x110000)s=SC_UI_E_INVALID;
        for(ScU32 n=64;n<i.length;n++){
            crc^=data[n];
            for(ScU32 bit=0;bit<8;bit++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
        }
        if((crc^0xffffffffu)!=ui_data_u32(data+40))s=SC_UI_E_INVALID;
    }
    if(s<0){free(data);
        return ui_cause(0,s,UI_OP_DATA,0);
    }
    ui_unicode_data=data;
    ui_unicode_ranges=(UiUnicodeRange *)(data+64);
    ui_unicode_count=count;
    return 0;
}
static void ui_unicode_finish(void)
{free(ui_unicode_data);
    ui_unicode_data=0;
    ui_unicode_ranges=0;
    ui_unicode_count=0;
}
static ScU32 ui_unicode_flags(ScU32 scalar)
{
    ScU32 lo=0,hi=ui_unicode_count;
    while(lo<hi){ScU32 mid=lo+(hi-lo)/2;
        UiUnicodeRange *r=ui_unicode_ranges+mid;
        if(scalar<r->first)hi=mid;
        else if(scalar>r->last)lo=mid+1;
        else return r->flags;
    }
    return 0;
}

typedef struct UiChunk UiChunk;
typedef struct UiPiece UiPiece;
typedef struct UiEdit UiEdit;
typedef struct UiUndo UiUndo;
typedef struct UiEditLine UiEditLine;
typedef struct {ScU32 offset;
    ScI32 x;
}
UiTextBlock;
typedef struct {ScU32 start,end,next,hard_line;
}
UiWrapRow;
struct UiEditLine {UiEditLine *next;
    ScU32 line,start,end,pos,font_epoch,role;
    ScI32 x;
    ScArray *blocks;
};
struct UiChunk {ScU32 references,bytes;
    unsigned char data[1];
};
struct UiPiece {
    ScU32 references,height,bytes,breaks,storage;
    unsigned char first,last;
    UiPiece *left,*right;
    UiChunk *chunk;
    ScU32 offset,length;
};
typedef struct {ScU32 serial,anchor,caret,after_anchor,after_caret;
}
UiSavedSelection;
struct UiUndo {
    UiUndo *previous,*next;
    UiPiece *before,*after;
    ScU32 before_identity,after_identity,cost,count;
    UiSavedSelection *selections;
};
struct ScUiDocument {
    UiObject object;
    UiPiece *root;
    UiUndo *undo_first,*undo_last,*redo_first,*redo_last,*group;
    UiEdit *views;
    ScU32 revision,identity,next_identity,saved_identity,saved_revision,flags,budget;
    ScU32 undo_bytes,redo_bytes,group_depth,group_discard,next_view;
    UiNode *silent_node;
};
struct UiEdit {
    UiEdit *previous,*next;
    ScUiDocument *document;
    UiNode *node;
    ScU32 serial,anchor,caret,affinity,dragging,goal_valid,caret_on,caret_timer,revision;
    ScI32 goal_x;
    ScArray *spans;
    ScU32 span_revision;
    UiEditLine *lines;
    ScU32 last_click_tick,last_click_byte,clicks;
    ScI32 click_x,click_y;
    ScU32 suppress_scalar;
    ScArray *wraps;
    ScU32 wrap_revision,wrap_width,wrap_font;
};
typedef struct {
    const unsigned char *flat;
    UiPiece *root;
    ScU32 bytes,cache_start,cache_end;
    const unsigned char *cache;
    ScU32 ri_at,ri_count,ri_valid;
}
UiTextSource;
typedef struct {UiEdit *view;
    UiNode *node;
    ScUiContext *context;
    ScU32 anchor,caret;
}
UiEditHold;
static ScU32 ui_piece_allocated;
static void ui_edit_lines_changed(UiEdit *,ScU32);

static ScU32 ui_piece_height(UiPiece *p){return p?p->height:0;
}
static void ui_piece_retain(UiPiece *p){if(p)p->references++;
}
static void ui_chunk_release(UiChunk *p){if(p && !--p->references)free(p);
}
static void ui_piece_release(UiPiece *p)
{
    if(!p || --p->references)return;
    ui_piece_release(p->left);
    ui_piece_release(p->right);
    ui_chunk_release(p->chunk);
    free(p);
}
static void ui_piece_charge(ScU32 bytes)
{if(bytes>UINT_MAX-ui_piece_allocated)ui_piece_allocated=UINT_MAX;
    else ui_piece_allocated+=bytes;
}
static ScStatus ui_piece_leaf(UiChunk *chunk,ScU32 offset,ScU32 bytes,UiPiece **out)
{
    UiPiece *p;
    if(!bytes){*out=0;
        return 0;
    }
    if(!chunk || offset>chunk->bytes || bytes>chunk->bytes-offset || chunk->references==UINT_MAX)return SC_UI_E_RANGE;
    p=(UiPiece *)calloc(1,sizeof(*p));
    if(!p)return SC_UI_E_MEMORY;
    p->references=p->height=1;
    p->bytes=p->length=bytes;
    p->offset=offset;
    p->chunk=chunk;
    chunk->references++;
    p->first=chunk->data[offset];
    p->last=chunk->data[offset+bytes-1];
    p->storage=sizeof(*p)+sizeof(*chunk)+chunk->bytes;
    for(ScU32 n=0;n<bytes;n++)if(chunk->data[offset+n]=='\r' || (chunk->data[offset+n]=='\n' && (!n || chunk->data[offset+n-1]!='\r')))p->breaks++;
    ui_piece_charge(sizeof(*p));
    *out=p;
    return 0;
}
static ScStatus ui_piece_branch(UiPiece *a,UiPiece *b,UiPiece **out)
{
    UiPiece *p;
    ScU32 total;
    if(!a || !b){p=a?a:b;
        if(p && p->references==UINT_MAX)return SC_UI_E_RANGE;
        ui_piece_retain(p);
        *out=p;
        return 0;
    }
    if(!ui_add(a->bytes,b->bytes,&total) || a->references==UINT_MAX || b->references==UINT_MAX)return SC_UI_E_RANGE;
    p=(UiPiece *)calloc(1,sizeof(*p));
    if(!p)return SC_UI_E_MEMORY;
    p->references=1;
    p->height=(a->height>b->height?a->height:b->height)+1;
    p->bytes=total;
    p->left=a;
    p->right=b;
    ui_piece_retain(a);
    ui_piece_retain(b);
    p->first=a->first;
    p->last=b->last;
    p->breaks=a->breaks+b->breaks-(a->last=='\r' && b->first=='\n');
    if(!ui_add(a->storage,b->storage,&total) || !ui_add(total,sizeof(*p),&p->storage))p->storage=UINT_MAX;
    ui_piece_charge(sizeof(*p));
    *out=p;
    return 0;
}
static ScStatus ui_piece_balance(UiPiece *a,UiPiece *b,UiPiece **out)
{
    ScU32 ah=ui_piece_height(a),bh=ui_piece_height(b);
    UiPiece *x=0,*y=0;
    ScStatus s;
    if(ah>bh+1){
        if(ui_piece_height(a->left)>=ui_piece_height(a->right)){
            s=ui_piece_branch(a->right,b,&x);
            if(s==0)s=ui_piece_branch(a->left,x,out);
        }
        else{
            s=ui_piece_branch(a->left,a->right->left,&x);
            if(s==0)s=ui_piece_branch(a->right->right,b,&y);
            if(s==0)s=ui_piece_branch(x,y,out);
        }
    }
    else if(bh>ah+1){
        if(ui_piece_height(b->right)>=ui_piece_height(b->left)){
            s=ui_piece_branch(a,b->left,&x);
            if(s==0)s=ui_piece_branch(x,b->right,out);
        }
        else{
            s=ui_piece_branch(a,b->left->left,&x);
            if(s==0)s=ui_piece_branch(b->left->right,b->right,&y);
            if(s==0)s=ui_piece_branch(x,y,out);
        }
    }
    else return ui_piece_branch(a,b,out);
    ui_piece_release(x);
    ui_piece_release(y);
    return s;
}
static ScStatus ui_piece_join(UiPiece *a,UiPiece *b,UiPiece **out)
{
    UiPiece *middle=0;
    ScStatus s;
    if(ui_piece_height(a)>ui_piece_height(b)+1){s=ui_piece_join(a->right,b,&middle);
        if(s==0)s=ui_piece_balance(a->left,middle,out);
    }
    else if(ui_piece_height(b)>ui_piece_height(a)+1){s=ui_piece_join(a,b->left,&middle);
        if(s==0)s=ui_piece_balance(middle,b->right,out);
    }
    else return ui_piece_branch(a,b,out);
    ui_piece_release(middle);
    return s;
}
static ScStatus ui_piece_split(UiPiece *p,ScU32 at,UiPiece **left,UiPiece **right)
{
    UiPiece *a=0,*b=0,*c=0;
    ScStatus s;
    *left=*right=0;
    if(!p)return at?SC_UI_E_RANGE:0;
    if(at>p->bytes)return SC_UI_E_RANGE;
    if(at==0){ui_piece_retain(p);
        *right=p;
        return 0;
    }
    if(at==p->bytes){ui_piece_retain(p);
        *left=p;
        return 0;
    }
    if(p->chunk){s=ui_piece_leaf(p->chunk,p->offset,at,&a);
        if(s==0)s=ui_piece_leaf(p->chunk,p->offset+at,p->length-at,&b);
    }
    else if(at<p->left->bytes){s=ui_piece_split(p->left,at,&a,&c);
        if(s==0)s=ui_piece_join(c,p->right,&b);
    }
    else{s=ui_piece_split(p->right,at-p->left->bytes,&c,&b);
        if(s==0)s=ui_piece_join(p->left,c,&a);
    }
    ui_piece_release(c);
    if(s<0){ui_piece_release(a);
        ui_piece_release(b);
        return s;
    }
    *left=a;
    *right=b;
    return 0;
}
static ScStatus ui_piece_text(const void *data,ScU32 bytes,UiPiece **out)
{
    const unsigned char *text=(const unsigned char *)data;
    UiPiece *root=0;
    ScStatus s=0;
    ScU32 offset=0;
    while(offset<bytes){
        ScU32 take=bytes-offset;
        if(take>4096)take=4096;
        while(offset+take<bytes && (text[offset+take]&0xc0)==0x80)take--;
        UiChunk *chunk=(UiChunk *)malloc(sizeof(*chunk)+take);
        UiPiece *leaf=0,*next=0;
        if(!chunk){s=SC_UI_E_MEMORY;
            break;
        }
        chunk->references=1;
        chunk->bytes=take;
        memcpy(chunk->data,text+offset,take);
        ui_piece_charge(sizeof(*chunk)+take);
        s=ui_piece_leaf(chunk,0,take,&leaf);
        ui_chunk_release(chunk);
        if(s==0)s=ui_piece_join(root,leaf,&next);
        ui_piece_release(leaf);
        if(s<0)break;
        ui_piece_release(root);
        root=next;
        offset+=take;
    }
    if(s<0){ui_piece_release(root);
        return s;
    }
    *out=root;
    return 0;
}
static const unsigned char *ui_piece_span(UiPiece *p,ScU32 at,ScU32 *available,ScU32 *begin)
{
    ScU32 start=0;
    if(!p || at>=p->bytes)return 0;
    while(!p->chunk){if(at<p->left->bytes)p=p->left;
        else{at-=p->left->bytes;
            start+=p->left->bytes;
            p=p->right;
        }
    }
    *available=p->length-at;
    *begin=start;
    return p->chunk->data+p->offset+at;
}
static unsigned char ui_source_byte(UiTextSource *source,ScU32 at)
{
    if(source->flat)return source->flat[at];
    if(at>=source->cache_start && at<source->cache_end && source->cache)return source->cache[at-source->cache_start];
    ScU32 available=0,begin=0;
    const unsigned char *p=ui_piece_span(source->root,at,&available,&begin);
    source->cache=p;
    source->cache_start=at;
    source->cache_end=at+available;
    return p?p[0]:0;
}
static ScStatus ui_source_scalar(UiTextSource *source,ScU32 at,ScU32 *scalar,ScU32 *next)
{
    unsigned char bytes[4];
    ScU32 count=source->bytes-at,offset=0;
    if(at>=source->bytes)return SC_UI_END;
    if(count>4)count=4;
    for(ScU32 n=0;n<count;n++)bytes[n]=ui_source_byte(source,at+n);
    ScStatus s=ScUtf8Decode(bytes,count,&offset,scalar);
    if(s<0)return SC_UI_E_UTF8;
    *next=at+offset;
    return 0;
}
static ScStatus ui_source_previous(UiTextSource *source,ScU32 at,ScU32 *scalar,ScU32 *previous)
{
    ScU32 p=at,next;
    if(!at || at>source->bytes)return SC_UI_END;
    p--;
    while(p && (ui_source_byte(source,p)&0xc0)==0x80)p--;
    ScStatus s=ui_source_scalar(source,p,scalar,&next);
    if(s<0 || next!=at)return SC_UI_E_UTF8;
    *previous=p;
    return 0;
}
static int ui_source_boundary(UiTextSource *source,ScU32 at)
{return at<=source->bytes && (at==source->bytes || (ui_source_byte(source,at)&0xc0)!=0x80);
}
static ScU32 ui_wb(ScU32 flags){return (flags>>SC_UT_WB_SHIFT)&SC_UT_WB_MASK;
}
static int ui_wb_ignored(ScU32 property){return property==SC_UT_WB_EXTEND || property==SC_UT_WB_FORMAT || property==SC_UT_WB_ZWJ;
}
static int ui_wb_letter(ScU32 property){return property==SC_UT_WB_ALETTER || property==SC_UT_WB_HEBREW;
}
static int ui_wb_newline(ScU32 property){return property==SC_UT_WB_CR || property==SC_UT_WB_LF || property==SC_UT_WB_NEWLINE;
}
static int ui_wb_midletter(ScU32 p){return p==SC_UT_WB_MIDLETTER || p==SC_UT_WB_MIDNUMLET || p==SC_UT_WB_SINGLEQUOTE;
}
static int ui_wb_midnum(ScU32 p){return p==SC_UT_WB_MIDNUM || p==SC_UT_WB_MIDNUMLET || p==SC_UT_WB_SINGLEQUOTE;
}
static ScU32 ui_source_wb_before(UiTextSource *source,ScU32 *at)
{
    ScU32 scalar,previous;
    while(*at){if(ui_source_previous(source,*at,&scalar,&previous)<0)break;
        *at=previous;
        ScU32 p=ui_wb(ui_unicode_flags(scalar));
        if(!ui_wb_ignored(p))return p;
    }
    return UINT_MAX;
}
static ScU32 ui_source_wb_after(UiTextSource *source,ScU32 *at)
{
    ScU32 scalar,next;
    while(*at<source->bytes){if(ui_source_scalar(source,*at,&scalar,&next)<0)break;
        *at=next;
        ScU32 p=ui_wb(ui_unicode_flags(scalar));
        if(!ui_wb_ignored(p))return p;
    }
    return UINT_MAX;
}
/* 规则按UAX29顺序，GB9c与GB11向左只读所需局部上下文，不扫描整文件。 */
static int ui_source_break(UiTextSource *source,ScU32 at,int word)
{
    ScU32 left,right,lp,rn,lf,rf,a,b;
    if(!at || at==source->bytes)return 1;
    if(ui_source_previous(source,at,&left,&lp)!=0 || ui_source_scalar(source,at,&right,&rn)!=0)return 1;
    lf=ui_unicode_flags(left);
    rf=ui_unicode_flags(right);
    if(!word){
        a=lf&15;
        b=rf&15;
        if(a==SC_UT_GCB_CR && b==SC_UT_GCB_LF)return 0;
        if(a==SC_UT_GCB_CR || a==SC_UT_GCB_LF || a==SC_UT_GCB_CONTROL || b==SC_UT_GCB_CR || b==SC_UT_GCB_LF || b==SC_UT_GCB_CONTROL)return 1;
        if(a==SC_UT_GCB_L && (b==SC_UT_GCB_L || b==SC_UT_GCB_V || b==SC_UT_GCB_LV || b==SC_UT_GCB_LVT))return 0;
        if((a==SC_UT_GCB_LV || a==SC_UT_GCB_V) && (b==SC_UT_GCB_V || b==SC_UT_GCB_T))return 0;
        if((a==SC_UT_GCB_LVT || a==SC_UT_GCB_T) && b==SC_UT_GCB_T)return 0;
        if(b==SC_UT_GCB_EXTEND || b==SC_UT_GCB_ZWJ || b==SC_UT_GCB_SPACING || a==SC_UT_GCB_PREPEND)return 0;
        if(((rf>>20)&3)==SC_UT_INCB_CONSONANT){
            ScU32 pos=at,scalar,previous;
            int linker=0;
            while(pos && ui_source_previous(source,pos,&scalar,&previous)==0){
                ScU32 incb=(ui_unicode_flags(scalar)>>20)&3;
                pos=previous;
                if(incb==SC_UT_INCB_LINKER){linker=1;
                    continue;
                }
                if(incb==SC_UT_INCB_EXTEND)continue;
                if(incb==SC_UT_INCB_CONSONANT && linker)return 0;
                break;
            }
        }
        if((rf&SC_UT_EXTENDED_PICTOGRAPHIC) && a==SC_UT_GCB_ZWJ){
            ScU32 pos=lp,scalar,previous;
            while(pos && ui_source_previous(source,pos,&scalar,&previous)==0){
                ScU32 flags=ui_unicode_flags(scalar);
                pos=previous;
                if((flags&15)==SC_UT_GCB_EXTEND)continue;
                if(flags&SC_UT_EXTENDED_PICTOGRAPHIC)return 0;
                break;
            }
        }
        if(a==SC_UT_GCB_RI && b==SC_UT_GCB_RI){
            ScU32 pos=at,scalar,previous,count=0;
            if(source->ri_valid && source->ri_at==lp)count=source->ri_count+1;
            else while(pos && ui_source_previous(source,pos,&scalar,&previous)==0){
                if((ui_unicode_flags(scalar)&15)!=SC_UT_GCB_RI)break;
                count++;
                pos=previous;
            }
            source->ri_at=at;
            source->ri_count=count;
            source->ri_valid=1;
            if(count&1)return 0;
        }
        return 1;
    }
    a=ui_wb(lf);
    b=ui_wb(rf);
    if(a==SC_UT_WB_CR && b==SC_UT_WB_LF)return 0;
    if(ui_wb_newline(a) || ui_wb_newline(b))return 1;
    if(a==SC_UT_WB_ZWJ && (rf&SC_UT_EXTENDED_PICTOGRAPHIC))return 0;
    if(a==SC_UT_WB_WSEGSPACE && b==SC_UT_WB_WSEGSPACE)return 0;
    if(ui_wb_ignored(b))return 0;
    ScU32 before=at,after=rn;
    a=ui_source_wb_before(source,&before);
    if(a==UINT_MAX || ui_wb_newline(a))return 1;
    if(ui_wb_letter(a) && ui_wb_letter(b))return 0;
    if(a==SC_UT_WB_HEBREW && b==SC_UT_WB_SINGLEQUOTE)return 0;
    ScU32 previous=before,next=after;
    ScU32 prev=ui_source_wb_before(source,&previous),following=ui_source_wb_after(source,&next);
    if(ui_wb_letter(a) && ui_wb_midletter(b) && ui_wb_letter(following))return 0;
    if(ui_wb_letter(prev) && ui_wb_midletter(a) && ui_wb_letter(b))return 0;
    if(a==SC_UT_WB_HEBREW && b==SC_UT_WB_DOUBLEQUOTE && following==SC_UT_WB_HEBREW)return 0;
    if(prev==SC_UT_WB_HEBREW && a==SC_UT_WB_DOUBLEQUOTE && b==SC_UT_WB_HEBREW)return 0;
    if((a==SC_UT_WB_NUMERIC && b==SC_UT_WB_NUMERIC) || (ui_wb_letter(a) && b==SC_UT_WB_NUMERIC) || (a==SC_UT_WB_NUMERIC && ui_wb_letter(b)))return 0;
    if(a==SC_UT_WB_NUMERIC && ui_wb_midnum(b) && following==SC_UT_WB_NUMERIC)return 0;
    if(prev==SC_UT_WB_NUMERIC && ui_wb_midnum(a) && b==SC_UT_WB_NUMERIC)return 0;
    if(a==SC_UT_WB_KATAKANA && b==SC_UT_WB_KATAKANA)return 0;
    if((ui_wb_letter(a) || a==SC_UT_WB_NUMERIC || a==SC_UT_WB_KATAKANA || a==SC_UT_WB_EXTENDNUMLET) && b==SC_UT_WB_EXTENDNUMLET)return 0;
    if(a==SC_UT_WB_EXTENDNUMLET && (ui_wb_letter(b) || b==SC_UT_WB_NUMERIC || b==SC_UT_WB_KATAKANA))return 0;
    if(a==SC_UT_WB_RI && b==SC_UT_WB_RI){
        ScU32 pos=at,count=0;
        while(pos && ui_source_wb_before(source,&pos)==SC_UT_WB_RI)count++;
        if(count&1)return 0;
    }
    return 1;
}
static ScStatus ui_source_step(UiTextSource *source,ScU32 at,ScU32 *out,int word,int backwards)
{
    ScU32 scalar,next;
    ScStatus s=ui_unicode_prepare();
    if(s<0)return s;
    if(!out || !ui_source_boundary(source,at))return SC_UI_E_RANGE;
    if(backwards){
        if(!at){*out=0;
            return 0;
        }
        do{s=ui_source_previous(source,at,&scalar,&next);
            if(s<0)return s;
            at=next;
        }
        while(at && !ui_source_break(source,at,word));
    }
    else{
        if(at==source->bytes){*out=at;
            return 0;
        }
        do{s=ui_source_scalar(source,at,&scalar,&next);
            if(s<0)return s;
            at=next;
        }
        while(at<source->bytes && !ui_source_break(source,at,word));
    }
    *out=at;
    return 0;
}
static ScStatus ui_flat_step(const void *bytes,ScU32 count,ScU32 at,ScU32 *out,int word,int backwards)
{
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.flat=(const unsigned char *)bytes;
    source.bytes=count;
    if(!bytes && count)return SC_UI_E_INVALID;
    return ui_source_step(&source,at,out,word,backwards);
}
static ScStatus ui_grapheme_next(const void *p,ScU32 n,ScU32 a,ScU32 *out){return ui_flat_step(p,n,a,out,0,0);
}
static ScStatus ui_grapheme_previous(const void *p,ScU32 n,ScU32 a,ScU32 *out){return ui_flat_step(p,n,a,out,0,1);
}
static ScStatus ui_word_next(const void *p,ScU32 n,ScU32 a,ScU32 *out){return ui_flat_step(p,n,a,out,1,0);
}
static ScStatus ui_word_previous(const void *p,ScU32 n,ScU32 a,ScU32 *out){return ui_flat_step(p,n,a,out,1,1);
}

ScStatus ScUiDocumentBoundary(ScUiDocument *document,ScU32 offset,ScU32 kind,ScI32 direction,ScU32 *out)
{
    if(!out || (kind!=SC_UI_BOUNDARY_GRAPHEME && kind!=SC_UI_BOUNDARY_WORD) || (direction!=-1 && direction!=1))return SC_UI_E_INVALID;
    ScStatus s=ui_object_enter((UiObject *)document,UI_DOCUMENT);
    if(s<0)return s;
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.root=document->root;
    source.bytes=document->root?document->root->bytes:0;
    ScU32 candidate;
    if(document->root && document->root->chunk){const void *flat=document->root->chunk->data+document->root->offset;
        if(kind==SC_UI_BOUNDARY_GRAPHEME)s=direction<0?ui_grapheme_previous(flat,source.bytes,offset,&candidate):ui_grapheme_next(flat,source.bytes,offset,&candidate);
        else s=direction<0?ui_word_previous(flat,source.bytes,offset,&candidate):ui_word_next(flat,source.bytes,offset,&candidate);
    }
    else s=ui_source_step(&source,offset,&candidate,kind==SC_UI_BOUNDARY_WORD,direction<0);
    if(s==0)*out=candidate;
    s=ui_cause(0,s,UI_OP_DATA,0);
    ui_object_leave(&document->object);
    return s;
}

static ScU32 ui_document_bytes(const ScUiDocument *d){return d->root?d->root->bytes:0;
}
static ScU32 ui_document_adjust(ScU32 offset,ScU32 start,ScU32 removed,ScU32 inserted)
{
    if(offset<start)return offset;
    if(offset<=start+removed)return start+inserted;
    return offset-removed+inserted;
}
static void ui_undo_free(UiUndo *u)
{if(!u)return;
    ui_piece_release(u->before);
    ui_piece_release(u->after);
    free(u->selections);
    free(u);
}
static void ui_undo_clear(UiUndo **first,UiUndo **last,ScU32 *bytes)
{UiUndo *u=*first;
    while(u){UiUndo *next=u->next;
        ui_undo_free(u);
        u=next;
    }
    *first=*last=0;
    *bytes=0;
}
static void ui_undo_append(ScUiDocument *d,UiUndo *record)
{
    if(record->cost>d->budget){ui_undo_free(record);
        return;
    }
    while(d->undo_first && d->undo_bytes>d->budget-record->cost){UiUndo *old=d->undo_first;
        d->undo_first=old->next;
        if(d->undo_first)d->undo_first->previous=0;
        else d->undo_last=0;
        d->undo_bytes-=old->cost;
        ui_undo_free(old);
    }
    record->previous=d->undo_last;
    record->next=0;
    if(d->undo_last)d->undo_last->next=record;
    else d->undo_first=record;
    d->undo_last=record;
    d->undo_bytes+=record->cost;
}
static ScStatus ui_edit_holds(ScUiDocument *d,UiEditHold **out,ScU32 *count)
{
    UiEdit *v;
    ScU32 n=0,bytes;
    UiEditHold *holds=0;
    for(v=d->views;v;v=v->next){if(n==UINT_MAX)return SC_UI_E_RANGE;
        n++;
    }
    if(!ui_multiply(n,sizeof(*holds),&bytes))return SC_UI_E_RANGE;
    if(bytes){holds=(UiEditHold *)calloc(1,bytes);
        if(!holds)return SC_UI_E_MEMORY;
    }
    n=0;
    for(v=d->views;v;v=v->next){
        ScStatus s=ScUiContextRetain(v->node->owner);
        if(s<0){for(ScU32 i=0;i<n;i++){ui_node_leave(holds[i].node);
                ScUiContextRelease(holds[i].context);
            }
            free(holds);
            return s;
        }
        ui_node_hold(v->node);
        UiEditHold hold={v,v->node,v->node->owner,v->anchor,v->caret};
        holds[n++]=hold;
    }
    *out=holds;
    *count=n;
    return 0;
}
static void ui_edit_holds_release(UiEditHold *holds,ScU32 count)
{
    for(ScU32 i=0;i<count;i++){ui_node_leave(holds[i].node);
        ScUiContextRelease(holds[i].context);
    }
    free(holds);
}
static void ui_document_notify(ScUiDocument *d,UiEditHold *holds,ScU32 count,int notify,ScU32 changed_at)
{
    for(ScU32 i=0;i<count;i++){
        UiNode *n=holds[i].node;
        UiEdit *v=holds[i].view;
        if((n->private_flags&UI_NODE_RETIRED) || v->document!=d)continue;
        v->revision=d->revision;
        v->goal_valid=0;
        v->caret_on=1;
        ui_edit_lines_changed(v,changed_at);
        ScArrayClear(v->spans);
        v->span_revision=0;
        if(n!=d->silent_node && n->value_revision<UINT_MAX)n->value_revision++;
        ui_node_layout_dirty(n);
        ui_node_damage(n);
        if(notify && !ui_quiet){ScStatus s=ui_event(n,SC_UI_EVENT_VALUE_CHANGED,0,0);
            if(s<0)n->owner->notification_error=s;
        }
    }
}
static ScStatus ui_undo_candidate(ScUiDocument *d,UiEditHold *holds,ScU32 count,UiUndo **out)
{
    UiUndo *u=(UiUndo *)calloc(1,sizeof(*u));
    ScU32 bytes;
    if(!u)return SC_UI_E_MEMORY;
    if(!ui_multiply(count,sizeof(*u->selections),&bytes)){free(u);
        return SC_UI_E_RANGE;
    }
    if(bytes){u->selections=(UiSavedSelection *)malloc(bytes);
        if(!u->selections){free(u);
            return SC_UI_E_MEMORY;
        }
    }
    u->before=d->root;
    ui_piece_retain(u->before);
    u->before_identity=d->identity;
    u->count=count;
    u->cost=sizeof(*u)+bytes;
    for(ScU32 i=0;i<count;i++){UiSavedSelection saved={holds[i].view->serial,holds[i].anchor,holds[i].caret,holds[i].anchor,holds[i].caret};
        u->selections[i]=saved;
    }
    *out=u;
    return 0;
}
static ScStatus ui_piece_replace(UiPiece *old,ScU32 start,ScU32 removed,UiPiece *insert,UiPiece **out,ScU32 *retained)
{
    UiPiece *a=0,*tail=0,*gone=0,*b=0,*head=0,*candidate=0;
    ScStatus s;
    s=ui_piece_split(old,start,&a,&tail);
    if(s==0)s=ui_piece_split(tail,removed,&gone,&b);
    if(s==0)s=ui_piece_join(a,insert,&head);
    if(s==0)s=ui_piece_join(head,b,&candidate);
    if(retained)*retained=gone?gone->storage:0;
    ui_piece_release(a);
    ui_piece_release(tail);
    ui_piece_release(gone);
    ui_piece_release(b);
    ui_piece_release(head);
    if(s<0){ui_piece_release(candidate);
        return s;
    }
    *out=candidate;
    return 0;
}
/* 所有失败点在root交换前。回调只看到完整正文与已调整的各view选区。 */
static ScStatus ui_document_commit(ScUiDocument *d,UiPiece *candidate,ScU32 start,ScU32 removed,ScU32 inserted,
    ScU32 flags,ScU32 cost,int notify,UiEditHold *prepared,ScU32 prepared_count,UiUndo *prepared_undo,
    ScArray *matches,ScU32 needle_bytes,ScU32 replacement_bytes)
{
    UiEditHold *holds=prepared;
    ScU32 count=prepared_count;
    UiUndo *u=prepared_undo;
    ScStatus s;
    if(d->revision==UINT_MAX || d->next_identity==UINT_MAX)return SC_UI_E_RANGE;
    if(!prepared){s=ui_edit_holds(d,&holds,&count);
        if(s<0)return s;
    }
    if(!u && !(flags&SC_UI_REPLACE_NO_UNDO) && !d->group_discard && !d->group){s=ui_undo_candidate(d,holds,count,&u);
        if(s<0){ui_edit_holds_release(holds,count);
            return s;
        }
    }
    UiPiece *old=d->root;
    d->root=candidate;
    d->identity=++d->next_identity;
    d->revision++;
    for(ScU32 i=0;i<count;i++){
        UiEdit *v=holds[i].view;
        if(matches){ScContainerInfo info;
            memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            ScArrayQuery(matches,&info);
            for(ScU32 n=info.length;n;n--){ScU32 at;
                ScArrayGet(matches,n-1,&at);
                v->anchor=ui_document_adjust(v->anchor,at,needle_bytes,replacement_bytes);
                v->caret=ui_document_adjust(v->caret,at,needle_bytes,replacement_bytes);
            }
        }
        else{v->anchor=ui_document_adjust(v->anchor,start,removed,inserted);
            v->caret=ui_document_adjust(v->caret,start,removed,inserted);
        }
        UiTextSource source;
        memset(&source,0,sizeof(source));
        source.root=candidate;
        source.bytes=candidate?candidate->bytes:0;
        if(!ui_source_break(&source,v->anchor,0))ui_source_step(&source,v->anchor,&v->anchor,0,0);
        if(!ui_source_break(&source,v->caret,0))ui_source_step(&source,v->caret,&v->caret,0,0);
    }
    ui_undo_clear(&d->redo_first,&d->redo_last,&d->redo_bytes);
    if(flags&SC_UI_REPLACE_NO_UNDO){
        ui_undo_clear(&d->undo_first,&d->undo_last,&d->undo_bytes);
        ui_undo_free(d->group);
        d->group=0;
        if(d->group_depth)d->group_discard=1;
    }
    else if(!d->group_discard){
        UiUndo *record=d->group?d->group:u;
        if(!ui_add(record->cost,cost,&record->cost))record->cost=UINT_MAX;
        ui_piece_release(record->after);
        record->after=candidate;
        ui_piece_retain(candidate);
        record->after_identity=d->identity;
        for(ScU32 i=0;i<record->count;i++)for(ScU32 j=0;j<count;j++)if(record->selections[i].serial==holds[j].view->serial){
            record->selections[i].after_anchor=holds[j].view->anchor;
            record->selections[i].after_caret=holds[j].view->caret;
            break;
        }
        if(d->group_depth){
            d->group=record;
            if(record->cost>d->budget){ui_undo_free(record);
                d->group=0;
                d->group_discard=1;
            }
        }
        else{
            ui_undo_append(d,record);
        }
    }
    ui_piece_release(old);
    ui_document_notify(d,holds,count,notify,start);
    ui_edit_holds_release(holds,count);
    return 0;
}
static void ui_document_dispose(UiObject *object,int quiet)
{
    ScUiDocument *d=(ScUiDocument *)object;
    (void)quiet;
    ui_piece_release(d->root);
    d->root=0;
    ui_undo_clear(&d->undo_first,&d->undo_last,&d->undo_bytes);
    ui_undo_clear(&d->redo_first,&d->redo_last,&d->redo_bytes);
    ui_undo_free(d->group);
    d->group=0;
}
ScStatus ScUiDocumentCreate(const ScUiDocumentDesc *desc,ScUiDocument **out)
{
    ScStatus s=ui_input(desc,sizeof(*desc));
    ScUiDocument *d;
    void *candidate=0;
    UiPiece *root=0;
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    if((desc->flags&~(SC_UI_DOCUMENT_READONLY|SC_UI_DOCUMENT_PRESERVE_EOL)) || !ui_data_reserved(desc->reserved,4))return SC_UI_E_INVALID;
    s=ui_input(&desc->text,sizeof(desc->text));
    if(s<0)return s;
    if((!desc->text.data && desc->text.bytes) || ScUtf8Validate(desc->text.data,desc->text.bytes,0)<0)return SC_UI_E_UTF8;
    s=ui_unicode_prepare();
    if(s<0)return s;
    s=ui_piece_text(desc->text.data,desc->text.bytes,&root);
    if(s<0)return s;
    s=ui_object_new(sizeof(*d),UI_DOCUMENT,ui_document_dispose,&candidate);
    if(s<0){ui_piece_release(root);
        return s;
    }
    d=(ScUiDocument *)candidate;
    d->root=root;
    d->revision=d->identity=d->next_identity=d->saved_identity=d->saved_revision=1;
    d->flags=desc->flags;
    d->budget=desc->undo_budget_bytes?desc->undo_budget_bytes:262144u;
    d->next_view=1;
    *out=d;
    return 0;
}
ScStatus ScUiDocumentRetain(ScUiDocument *d){return ui_object_retain((UiObject *)d,UI_DOCUMENT);
}
void ScUiDocumentRelease(ScUiDocument *d){ui_object_release((UiObject *)d);
}
ScStatus ScUiDocumentQuery(const ScUiDocument *d,ScUiDocumentInfo *out)
{
    ScUiDocumentInfo i;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!d || d->object.kind!=UI_DOCUMENT || d->object.closing)return SC_UI_E_STALE;
    memset(&i,0,sizeof(i));
    i.size=sizeof(i);
    i.version=1;
    i.bytes=ui_document_bytes(d);
    i.lines=(d->root?d->root->breaks:0)+1;
    i.revision=d->revision;
    i.saved_revision=d->saved_revision;
    i.undo_bytes=d->undo_bytes+(d->group?d->group->cost:0);
    i.redo_bytes=d->redo_bytes;
    i.flags=d->flags|(d->identity!=d->saved_identity?SC_UI_DOCUMENT_DIRTY:0);
    i.can_undo=!d->group_depth && d->undo_last!=0;
    i.can_redo=!d->group_depth && d->redo_last!=0;
    memcpy(out,&i,sizeof(i));
    return 0;
}
static ScStatus ui_document_copy_root(UiPiece *root,ScU32 start,ScU32 count,ScString **out)
{
    ScU32 bytes=root?root->bytes:0;
    unsigned char *body=0;
    ScStatus s;
    if(!out || start>bytes || count>bytes-start)return SC_UI_E_RANGE;
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.root=root;
    source.bytes=bytes;
    if(!ui_source_boundary(&source,start) || !ui_source_boundary(&source,start+count))return SC_UI_E_RANGE;
    if(count){body=(unsigned char *)malloc(count);
        if(!body)return SC_UI_E_MEMORY;
    }
    for(ScU32 done=0;done<count;){ScU32 available=0,begin=0;
        const unsigned char *p=ui_piece_span(root,start+done,&available,&begin);
        /* 已验范围仍须核索引的实际span，坏树不复制NULL也不以零步长空转。 */
        if(!p || !available){free(body);
            return SC_UI_E_FAILED;
            }
        ScU32 take=count-done;
        if(take>available)take=available;
        memcpy(body+done,p,take);
        done+=take;
    }
    s=ScStringCreate(body,count,out);
    free(body);
    return ui_data_status(s);
}
ScStatus ScUiDocumentCopy(const ScUiDocument *d,ScU32 start,ScU32 count,ScString **out)
{
    if(!d || d->object.kind!=UI_DOCUMENT || d->object.closing)return SC_UI_E_STALE;
    return ui_document_copy_root(d->root,start,count,out);
}
static ScStatus ui_document_replace(ScUiDocument *d,ScU32 start,ScU32 removed,const ScUiText *text,ScU32 flags,int notify)
{
    ScStatus s=ui_input(text,sizeof(*text));
    UiPiece *insert=0,*candidate=0;
    ScU32 bytes,total,retained=0,cost;
    if(s<0 || flags&~3u)return s<0?s:SC_UI_E_INVALID;
    if(d->flags&SC_UI_DOCUMENT_READONLY)return SC_UI_E_CANCELLED;
    bytes=ui_document_bytes(d);
    if(start>bytes || removed>bytes-start || !ui_add(bytes-removed,text->bytes,&total))return SC_UI_E_RANGE;
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.root=d->root;
    source.bytes=bytes;
    if(!ui_source_boundary(&source,start) || !ui_source_boundary(&source,start+removed))return SC_UI_E_RANGE;
    if((!text->data && text->bytes) || ScUtf8Validate(text->data,text->bytes,0)<0)return SC_UI_E_UTF8;
    if(!removed && !text->bytes)return 0;
    ui_piece_allocated=0;
    s=ui_piece_text(text->data,text->bytes,&insert);
    if(s==0)s=ui_piece_replace(d->root,start,removed,insert,&candidate,&retained);
    cost=ui_piece_allocated;
    if(!ui_add(cost,retained,&cost))cost=UINT_MAX;
    if(s==0)s=ui_document_commit(d,candidate,start,removed,text->bytes,flags,cost,notify,0,0,0,0,0,0);
    if(s<0)ui_piece_release(candidate);
    ui_piece_release(insert);
    return s;
}
ScStatus ScUiDocumentReplace(ScUiDocument *d,ScU32 start,ScU32 removed,const ScUiText *text,ScU32 flags)
{
    ScStatus s=ui_object_enter((UiObject *)d,UI_DOCUMENT);
    if(s<0)return s;
    s=ui_document_replace(d,start,removed,text,flags,1);
    ui_object_leave(&d->object);
    return s;
}
ScStatus ScUiDocumentBeginUndo(ScUiDocument *d)
{
    ScStatus s=ui_object_enter((UiObject *)d,UI_DOCUMENT);
    UiEditHold *holds=0;
    ScU32 count=0;
    if(s<0)return s;
    if(d->group_depth==UINT_MAX)s=SC_UI_E_RANGE;
    else if(d->group_depth){d->group_depth++;
        s=0;
    }
    else{s=ui_edit_holds(d,&holds,&count);
        if(s==0)s=ui_undo_candidate(d,holds,count,&d->group);
        ui_edit_holds_release(holds,count);
        if(s==0){d->group_depth=1;
            d->group_discard=0;
        }
    }
    ui_object_leave(&d->object);
    return s;
}
ScStatus ScUiDocumentEndUndo(ScUiDocument *d)
{
    ScStatus s=ui_object_enter((UiObject *)d,UI_DOCUMENT);
    if(s<0)return s;
    if(!d->group_depth)s=SC_UI_E_INVALID;
    else if(!--d->group_depth){UiUndo *u=d->group;
        d->group=0;
        d->group_discard=0;
        if(u && u->after)ui_undo_append(d,u);
        else ui_undo_free(u);
    }
    ui_object_leave(&d->object);
    return s;
}
static ScStatus ui_document_history(ScUiDocument *d,int redo)
{
    UiUndo **first=redo?&d->redo_first:&d->undo_first,**last=redo?&d->redo_last:&d->undo_last;
    UiUndo **target_first=redo?&d->undo_first:&d->redo_first,**target_last=redo?&d->undo_last:&d->redo_last;
    ScU32 *from_bytes=redo?&d->redo_bytes:&d->undo_bytes,*to_bytes=redo?&d->undo_bytes:&d->redo_bytes;
    UiUndo *u=*last;
    UiEditHold *holds=0;
    ScU32 count=0;
    ScStatus s;
    if(d->flags&SC_UI_DOCUMENT_READONLY)return SC_UI_E_CANCELLED;
    if(d->group_depth)return SC_UI_E_BUSY;
    if(!u)return SC_UI_E_NOT_FOUND;
    if(d->revision==UINT_MAX)return SC_UI_E_RANGE;
    s=ui_edit_holds(d,&holds,&count);
    if(s<0)return s;
    UiPiece *next=redo?u->after:u->before;
    ui_piece_retain(next);
    ui_piece_release(d->root);
    d->root=next;
    d->identity=redo?u->after_identity:u->before_identity;
    d->revision++;
    *last=u->previous;
    if(*last)(*last)->next=0;
    else *first=0;
    *from_bytes-=u->cost;
    u->previous=*target_last;
    u->next=0;
    if(*target_last)(*target_last)->next=u;
    else *target_first=u;
    *target_last=u;
    *to_bytes+=u->cost;
    for(ScU32 i=0;i<count;i++){
        UiEdit *v=holds[i].view;
        ScU32 bytes=ui_document_bytes(d);
        v->anchor=v->anchor<bytes?v->anchor:bytes;
        v->caret=v->caret<bytes?v->caret:bytes;
        for(ScU32 j=0;j<u->count;j++)if(u->selections[j].serial==v->serial){
            v->anchor=redo?u->selections[j].after_anchor:u->selections[j].anchor;
            v->caret=redo?u->selections[j].after_caret:u->selections[j].caret;
            break;
        }
        UiTextSource source;
        memset(&source,0,sizeof(source));
        source.root=d->root;
        source.bytes=bytes;
        while(v->anchor && !ui_source_boundary(&source,v->anchor))v->anchor--;
        while(v->caret && !ui_source_boundary(&source,v->caret))v->caret--;
        if(!ui_source_break(&source,v->anchor,0))ui_source_step(&source,v->anchor,&v->anchor,0,1);
        if(!ui_source_break(&source,v->caret,0))ui_source_step(&source,v->caret,&v->caret,0,1);
    }
    ui_document_notify(d,holds,count,1,UINT_MAX);
    ui_edit_holds_release(holds,count);
    return 0;
}
ScStatus ScUiDocumentUndo(ScUiDocument *d)
{ScStatus s=ui_object_enter((UiObject *)d,UI_DOCUMENT);
    if(s<0)return s;
    s=ui_document_history(d,0);
    ui_object_leave(&d->object);
    return s;
}
ScStatus ScUiDocumentRedo(ScUiDocument *d)
{ScStatus s=ui_object_enter((UiObject *)d,UI_DOCUMENT);
    if(s<0)return s;
    s=ui_document_history(d,1);
    ui_object_leave(&d->object);
    return s;
}
ScStatus ScUiDocumentMarkSaved(ScUiDocument *d)
{if(!d || d->object.kind!=UI_DOCUMENT || d->object.closing)return SC_UI_E_STALE;
    d->saved_identity=d->identity;
    d->saved_revision=d->revision;
    return 0;
}

static unsigned char ui_find_fold(unsigned char c,ScU32 flags)
{return (flags&SC_UI_FIND_ASCII_CASELESS) && c>='A' && c<='Z'?(unsigned char)(c+('a'-'A')):c;
}
static ScStatus ui_document_matches(const ScUiDocument *d,const ScUiFindDesc *find,ScArray **out)
{
    ScStatus s=ui_input(find,sizeof(*find));
    ScArray *matches=0;
    ScU32 *prefix=0,bytes=ui_document_bytes(d),n=find?find->needle.bytes:0;
    if(s<0)return s;
    if((find->flags&~15u) || !ui_data_reserved(find->reserved,3) || find->start>find->end || find->end>bytes)return SC_UI_E_INVALID;
    s=ui_input(&find->needle,sizeof(find->needle));
    if(s<0)return s;
    if(!n || !find->needle.data || ScUtf8Validate(find->needle.data,n,0)<0)return SC_UI_E_UTF8;
    ScU32 allocation;
    if(!ui_multiply(n,sizeof(*prefix),&allocation))return SC_UI_E_RANGE;
    prefix=(ScU32 *)malloc(allocation);
    if(!prefix)return SC_UI_E_MEMORY;
    s=ui_array_new(sizeof(ScU32),&matches);
    if(s<0){free(prefix);
        return ui_data_status(s);
    }
    const unsigned char *needle=(const unsigned char *)find->needle.data;
    prefix[0]=0;
    for(ScU32 i=1,k=0;i<n;i++){
        unsigned char c=ui_find_fold(needle[i],find->flags);
        while(k && c!=ui_find_fold(needle[k],find->flags))k=prefix[k-1];
        if(c==ui_find_fold(needle[k],find->flags))k++;
        prefix[i]=k;
    }
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.root=d->root;
    source.bytes=bytes;
    if(!ui_source_boundary(&source,find->start) || !ui_source_boundary(&source,find->end))s=SC_UI_E_RANGE;
    for(ScU32 i=find->start,k=0;s==0 && i<find->end;i++){
        unsigned char c=ui_find_fold(ui_source_byte(&source,i),find->flags);
        while(k && c!=ui_find_fold(needle[k],find->flags))k=prefix[k-1];
        if(c==ui_find_fold(needle[k],find->flags))k++;
        if(k==n){ScU32 start=i+1-n;
            if(!(find->flags&SC_UI_FIND_WHOLE_WORD) || (ui_source_break(&source,start,1) && ui_source_break(&source,i+1,1))){
                ScContainerInfo info;
                memset(&info,0,sizeof(info));
                info.size=sizeof(info);
                info.version=1;
                s=ScArrayQuery(matches,&info);
                if(s==0)s=ScArrayInsert(matches,info.length,&start,1);
                k=0;
            }
            else k=prefix[k-1];
        }
        if((i&65535u)==65535u)ScProcessYield();
    }
    free(prefix);
    if(s<0){ScArrayRelease(matches);
        return ui_data_status(s);
    }
    *out=matches;
    return 0;
}
ScStatus ScUiDocumentFind(const ScUiDocument *d,const ScUiFindDesc *find,ScUiTextSelection *out)
{
    ScArray *matches=0;
    ScContainerInfo info;
    ScUiTextSelection value;
    ScU32 found;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!d || d->object.kind!=UI_DOCUMENT || d->object.closing)return SC_UI_E_STALE;
    s=ui_document_matches(d,find,&matches);
    if(s<0)return s;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(matches,&info);
    if(!info.length && (find->flags&SC_UI_FIND_WRAP)){
        ScArrayRelease(matches);
        ScUiFindDesc whole=*find;
        whole.start=0;
        whole.end=ui_document_bytes(d);
        s=ui_document_matches(d,&whole,&matches);
        if(s<0)return s;
        ScArrayQuery(matches,&info);
    }
    if(!info.length){ScArrayRelease(matches);
        return SC_UI_E_NOT_FOUND;
    }
    s=ScArrayGet(matches,(find->flags&SC_UI_FIND_BACKWARD)?info.length-1:0,&found);
    ScArrayRelease(matches);
    if(s<0)return ui_data_status(s);
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    value.anchor=found;
    value.caret=found+find->needle.bytes;
    value.revision=d->revision;
    memcpy(out,&value,sizeof(value));
    return 0;
}
ScStatus ScUiDocumentReplaceAll(ScUiDocument *d,const ScUiFindDesc *find,const ScUiText *text,ScU32 *changed)
{
    ScStatus s=ui_object_enter((UiObject *)d,UI_DOCUMENT);
    ScArray *matches=0;
    ScContainerInfo info;
    UiPiece *insert=0,*candidate=0;
    ScU32 retained=0,cost=0,total=0;
    if(s<0)return s;
    if(!changed || ui_input(text,sizeof(*text))<0 || (!text->data && text->bytes)){s=SC_UI_E_INVALID;
        goto done;
    }
    if(d->flags&SC_UI_DOCUMENT_READONLY){s=SC_UI_E_CANCELLED;
        goto done;
    }
    if(ScUtf8Validate(text->data,text->bytes,0)<0){s=SC_UI_E_UTF8;
        goto done;
    }
    s=ui_document_matches(d,find,&matches);
    if(s<0)goto done;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScArrayQuery(matches,&info);
    total=info.length;
    if(!total){*changed=0;
        goto done;
    }
    ui_piece_allocated=0;
    s=ui_piece_text(text->data,text->bytes,&insert);
    if(s<0)goto done;
    candidate=d->root;
    ui_piece_retain(candidate);
    for(ScU32 i=total;i && s==0;i--){ScU32 start,extra=0;
        UiPiece *next=0;
        s=ScArrayGet(matches,i-1,&start);
        if(s==0)s=ui_piece_replace(candidate,start,find->needle.bytes,insert,&next,&extra);
        if(s==0){ui_piece_release(candidate);
            candidate=next;
            if(!ui_add(retained,extra,&retained))retained=UINT_MAX;
        }
        if((i&255u)==0)ScProcessYield();
    }
    cost=ui_piece_allocated;
    if(!ui_add(cost,retained,&cost))cost=UINT_MAX;
    if(s==0){
        /* 整批事务的其它view先保住合法字素位置；下面按原匹配集调整，绝不再搜新文本。 */
        ScU32 old_bytes=ui_document_bytes(d),new_bytes=candidate?candidate->bytes:0;
        s=ui_document_commit(d,candidate,0,old_bytes,new_bytes,SC_UI_REPLACE_NEW_UNDO,cost,1,0,0,0,matches,find->needle.bytes,text->bytes);
        if(s==0){candidate=0;
            *changed=total;
        }
    }
    done:ui_piece_release(candidate);
    ui_piece_release(insert);
    ScArrayRelease(matches);
    ui_object_leave(&d->object);
    return s;
}

static ScU32 ui_piece_prefix_lines(UiPiece *p,ScU32 at)
{
    if(!p || !at)return 0;
    if(at>=p->bytes)return p->breaks;
    if(p->chunk){ScU32 count=0;
        const unsigned char *text=p->chunk->data+p->offset;
        for(ScU32 i=0;i<at;i++)if(text[i]=='\r' || (text[i]=='\n' && (!i || text[i-1]!='\r')))count++;
        return count;
    }
    if(at<=p->left->bytes)return ui_piece_prefix_lines(p->left,at);
    return p->left->breaks+ui_piece_prefix_lines(p->right,at-p->left->bytes)-(p->left->last=='\r' && p->right->first=='\n');
}
static ScU32 ui_piece_line_offset(UiPiece *p,ScU32 line)
{
    ScU32 start=0;
    if(!line)return 0;
    if(!p || line>p->breaks)return p?p->bytes:0;
    while(!p->chunk){
        if(line<=p->left->breaks)p=p->left;
        else{line-=p->left->breaks;
            if(p->left->last=='\r' && p->right->first=='\n')line++;
            start+=p->left->bytes;
            p=p->right;
        }
    }
    const unsigned char *text=p->chunk->data+p->offset;
    for(ScU32 i=0;i<p->length;i++)if(text[i]=='\r' || (text[i]=='\n' && (!i || text[i-1]!='\r'))){if(!--line)return start+i+1;
    }
    return start+p->length;
}
static ScU32 ui_document_line(ScUiDocument *d,ScU32 line)
{
    ScU32 at=ui_piece_line_offset(d->root,line),bytes=ui_document_bytes(d);
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.root=d->root;
    source.bytes=bytes;
    if(at && at<bytes && ui_source_byte(&source,at-1)=='\r' && ui_source_byte(&source,at)=='\n')at++;
    return at;
}
static ScU32 ui_document_line_end(ScUiDocument *d,ScU32 line)
{
    ScU32 end=ui_document_line(d,line+1),bytes=ui_document_bytes(d);
    UiTextSource source;
    if(line>=(d->root?d->root->breaks:0))return bytes;
    memset(&source,0,sizeof(source));
    source.root=d->root;
    source.bytes=bytes;
    if(end && ui_source_byte(&source,end-1)=='\n')end--;
    if(end && ui_source_byte(&source,end-1)=='\r')end--;
    return end;
}
static void ui_edit_line_free(UiEditLine *line){if(line){ScArrayRelease(line->blocks);
        free(line);
    }
}
static void ui_edit_lines_changed(UiEdit *v,ScU32 at)
{
    if(v->wraps){ScView view;
        memset(&view,0,sizeof(view));
        view.size=sizeof(view);
        view.version=1;
        if(ScArrayView(v->wraps,&view)==0){const UiWrapRow *rows=(const UiWrapRow *)view.data;
            ScU32 count=view.bytes/sizeof(*rows),keep=0;
            if(at!=UINT_MAX)while(keep<count && rows[keep].next<=at && rows[keep].end<at)keep++;
            ScArrayErase(v->wraps,keep,count-keep);
        }
        v->wrap_revision=0;
    }
    UiEditLine **slot=&v->lines;
    while(*slot){UiEditLine *line=*slot;
        if(at==UINT_MAX || at<line->start){*slot=line->next;
            ui_edit_line_free(line);
            continue;
        }
        if(at<=line->end){
            ScView blocks;
            memset(&blocks,0,sizeof(blocks));
            blocks.size=sizeof(blocks);
            blocks.version=1;
            if(ScArrayView(line->blocks,&blocks)==0){const UiTextBlock *items=(const UiTextBlock *)blocks.data;
                ScU32 count=blocks.bytes/sizeof(*items),keep=0;
                while(keep+1<count && items[keep+1].offset<at)keep++;
                if(count){line->pos=items[keep].offset;
                    line->x=items[keep].x;
                    ScArrayErase(line->blocks,keep+1,count-keep-1);
                }
            }
            line->end=ui_document_line_end(v->document,line->line);
        }
        slot=&line->next;
    }
}
static UiEdit *ui_edit_of(UiNode *n){UiControl *c=ui_control(n);
    return c?(UiEdit *)c->edit:0;
}
static ScStatus ui_edit_attach(UiEdit *v,ScUiDocument *d)
{
    ScStatus s=ScUiDocumentRetain(d);
    if(s<0)return s;
    if(d->next_view==UINT_MAX){ScUiDocumentRelease(d);
        return SC_UI_E_RANGE;
    }
    if(v->document){ScUiDocument *old=v->document;
        if(v->previous)v->previous->next=v->next;
        else old->views=v->next;
        if(v->next)v->next->previous=v->previous;
        ScUiDocumentRelease(old);
    }
    v->document=d;
    v->serial=d->next_view++;
    v->previous=0;
    v->next=d->views;
    if(d->views)d->views->previous=v;
    d->views=v;
    v->anchor=v->caret=0;
    v->revision=d->revision;
    v->span_revision=0;
    v->goal_valid=0;
    ui_edit_lines_changed(v,UINT_MAX);
    ScArrayClear(v->spans);
    return 0;
}
static ScStatus ui_edit_init(UiNode *n)
{
    UiControl *c=ui_control(n);
    UiEdit *v=(UiEdit *)calloc(1,sizeof(*v));
    ScUiDocumentDesc desc;
    ScUiDocument *d=0;
    ScStatus s;
    if(!v)return SC_UI_E_MEMORY;
    v->node=n;
    v->caret_on=1;
    s=ui_array_new(sizeof(ScUiStyleSpan),&v->spans);
    if(s==0)s=ui_array_new(sizeof(UiWrapRow),&v->wraps);
    if(s<0){ScArrayRelease(v->spans);
        free(v);
        return ui_data_status(s);
    }
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    desc.undo_budget_bytes=n->owner->description.undo_budget_bytes;
    ui_text_view(n->text,&desc.text);
    s=ScUiDocumentCreate(&desc,&d);
    if(s==0)s=ui_edit_attach(v,d);
    ScUiDocumentRelease(d);
    if(s<0){ScArrayRelease(v->spans);
        ScArrayRelease(v->wraps);
        free(v);
        return s;
    }
    c->edit=v;
    return 0;
}
static void ui_edit_destroy(UiNode *n)
{
    UiControl *c=ui_control(n);
    UiEdit *v=ui_edit_of(n);
    if(!v)return;
    ScUiDocument *d=v->document;
    if(v->previous)v->previous->next=v->next;
    else if(d)d->views=v->next;
    if(v->next)v->next->previous=v->previous;
    v->document=0;
    ScUiDocumentRelease(d);
    UiEditLine *line=v->lines;
    while(line){UiEditLine *next=line->next;
        ui_edit_line_free(line);
        line=next;
    }
    ScArrayRelease(v->spans);
    ScArrayRelease(v->wraps);
    free(v);
    c->edit=0;
}
ScStatus ScUiEditBind(ScUiContext *context,ScUiNode id,ScUiDocument *d)
{
    UiNode *n=ui_node(context,id);
    UiEdit *v=ui_edit_of(n);
    if(!n)return SC_UI_E_STALE;
    if(!v || !ui_control_editor(n))return SC_UI_E_INVALID;
    if(!d || d->object.kind!=UI_DOCUMENT || d->object.closing)return SC_UI_E_STALE;
    if(n->kind!=SC_UI_EDIT && n->kind!=SC_UI_LABEL && d->root && d->root->breaks)return SC_UI_E_INVALID;
    ScStatus s=ui_edit_attach(v,d);
    if(s==0){ui_node_layout_dirty(n);
        ui_node_damage(n);
    }
    return s;
}
static ScStatus ui_edit_text(UiNode *n,const ScUiText *text)
{
    UiEdit *v=ui_edit_of(n);
    if(!v)return SC_UI_E_INVALID;
    ScUiDocument *d=v->document;
    ScStatus s=ui_object_enter(&d->object,UI_DOCUMENT);
    if(s<0)return s;
    UiNode *previous=d->silent_node;
    d->silent_node=n;
    s=ui_document_replace(d,0,ui_document_bytes(d),text,SC_UI_REPLACE_NEW_UNDO,0);
    d->silent_node=previous;
    ui_object_leave(&d->object);
    return s;
}
static ScStatus ui_edit_copy_text(UiNode *n,ScString **out)
{UiEdit *v=ui_edit_of(n);
    return v?ScUiDocumentCopy(v->document,0,ui_document_bytes(v->document),out):SC_UI_E_INVALID;
}
static ScStatus ui_edit_value(UiNode *n,const ScValue *value)
{
    char number[16];
    int count=snprintf(number,sizeof(number),"%d",(ScI32)value->lo);
    if(count<0)return SC_UI_E_FAILED;
    ScUiText text={sizeof(text),1,number,(ScU32)count};
    ScStatus s=ui_edit_text(n,&text);
    if(s==0){n->state&=~SC_UI_INVALID;
        ui_tree_effective(n);
    }
    return s;
}
static ScStatus ui_edit_measure(UiNode *n,const ScUiSize *constraint,ScUiSize *out)
{
    ScU32 height,baseline;
    ui_font_line(n->owner,n->font_role,&height,&baseline);
    (void)baseline;
    (void)constraint;
    out->width=ui_scale(n->owner,n->kind==SC_UI_EDIT?200:140);
    out->height=(ScI32)height*(n->kind==SC_UI_EDIT?6:1)+ui_scale(n->owner,8);
    return 0;
}
static ScStatus ui_edit_line(UiEdit *v,ScU32 number,UiEditLine **out)
{
    UiNode *n=v->node;
    ScFontInfo *font;
    ScU32 face,pixels;
    ui_font_role(n->owner,n->font_role,&face,&pixels,&font);
    ScU32 epoch=font->generation^(pixels<<16);
    UiEditLine **slot=&v->lines;
    while(*slot){UiEditLine *line=*slot;
        if(line->line==number){
            if(line->font_epoch==epoch && line->role==n->font_role){*out=line;
                return 0;
            }
            *slot=line->next;
            ui_edit_line_free(line);
            break;
        }
        slot=&line->next;
    }
    UiEditLine *line=(UiEditLine *)calloc(1,sizeof(*line));
    if(!line)return SC_UI_E_MEMORY;
    line->line=number;
    line->start=ui_document_line(v->document,number);
    line->end=ui_document_line_end(v->document,number);
    line->pos=line->start;
    line->font_epoch=epoch;
    line->role=n->font_role;
    ScStatus s=ui_array_new(sizeof(UiTextBlock),&line->blocks);
    UiTextBlock first={line->start,0};
    if(s==0)s=ScArrayInsert(line->blocks,0,&first,1);
    if(s<0){ui_edit_line_free(line);
        return ui_data_status(s);
    }
    line->next=v->lines;
    v->lines=line;
    *out=line;
    return 0;
}
static ScStatus ui_edit_cluster(UiEdit *v,UiTextSource *source,ScU32 at,ScI32 x,ScU32 *end,ScI32 *width)
{
    ScStatus s=ui_source_step(source,at,end,0,0);
    if(s<0)return s;
    ScU32 walk=at,scalar,next;
    ScI32 advance=0;
    UiNode *n=v->node;
    if(n->flags&SC_UI_PASSWORD){const ScFontGlyph *g;
        ScU32 lease;
        s=ui_glyph(n->owner,n->font_role,0x2022,&g,&lease);
        if(s<0)return s;
        advance=g->advance;
        ui_glyph_release(n->owner,lease);
    }
    else while(walk<*end){s=ui_source_scalar(source,walk,&scalar,&next);
        if(s<0)return s;
        walk=next;
        if(scalar=='\t'){ScI32 tab=ui_scale(n->owner,32);
            if(tab<1)tab=1;
            advance=tab-(x+advance)%tab;
            continue;
        }
        if(ScUnicodeIsControl(scalar))scalar=0x2400+(scalar<=31?scalar:31);
        const ScFontGlyph *g;
        ScU32 lease;
        s=ui_glyph(n->owner,n->font_role,scalar,&g,&lease);
        if(s<0)return s;
        if(g->advance<0 || advance>INT_MAX-g->advance){ui_glyph_release(n->owner,lease);
            return SC_UI_E_RANGE;
        }
        advance+=g->advance;
        ui_glyph_release(n->owner,lease);
    }
    *width=advance;
    return 0;
}
static ScStatus ui_edit_line_build(UiEdit *v,UiEditLine *line,ScU32 offset,ScI32 target_x)
{
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.root=v->document->root;
    source.bytes=ui_document_bytes(v->document);
    ScU32 batch=0;
    while(line->pos<line->end && (line->pos<offset || line->x<target_x)){
        ScU32 end;
        ScI32 width;
        ScStatus s=ui_edit_cluster(v,&source,line->pos,line->x,&end,&width);
        if(s<0)return s;
        if(line->x>INT_MAX-width)return SC_UI_E_RANGE;
        line->pos=end;
        line->x+=width;
        if((++batch&31u)==0 || line->pos==line->end){ScContainerInfo info;
            memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            s=ScArrayQuery(line->blocks,&info);
            UiTextBlock block={line->pos,line->x};
            if(s==0)s=ScArrayInsert(line->blocks,info.length,&block,1);
            if(s<0)return ui_data_status(s);
        }
        if((batch&4095u)==0)ScProcessYield();
    }
    return 0;
}
static ScStatus ui_edit_line_seek(UiEdit *v,UiEditLine *line,ScU32 requested,ScI32 goal,int by_x,ScU32 *offset,ScI32 *x)
{
    ScStatus s=ui_edit_line_build(v,line,by_x?line->start:requested,by_x?goal:0);
    if(s<0)return s;
    ScView view;
    memset(&view,0,sizeof(view));
    view.size=sizeof(view);
    view.version=1;
    s=ScArrayView(line->blocks,&view);
    if(s<0)return ui_data_status(s);
    const UiTextBlock *blocks=(const UiTextBlock *)view.data;
    ScU32 count=view.bytes/sizeof(*blocks),low=0,high=count;
    while(low+1<high){ScU32 mid=low+(high-low)/2;
        if(by_x?blocks[mid].x<=goal:blocks[mid].offset<=requested)low=mid;
        else high=mid;
    }
    ScU32 at=blocks[low].offset;
    ScI32 position=blocks[low].x;
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.root=v->document->root;
    source.bytes=ui_document_bytes(v->document);
    while(at<line->end){ScU32 next;
        ScI32 width;
        s=ui_edit_cluster(v,&source,at,position,&next,&width);
        if(s<0)return s;
        if(by_x){if(goal<position+(by_x==2?width:width/2))break;
            if(goal<position+width){at=next;
                position+=width;
                break;
            }
        }
        else if(at>=requested || next>requested)break;
        at=next;
        position+=width;
    }
    *offset=at;
    *x=position;
    return 0;
}
static ScUiRect ui_edit_area(UiNode *n)
{
    ScI32 pad=ui_scale(n->owner,n->kind==SC_UI_LABEL?0:4);
    ScUiRect r={pad,pad,n->rectangle.width-pad*2,n->rectangle.height-pad*2};
    if(n->kind==SC_UI_SPINBOX)r.width-=ui_scale(n->owner,18);
    if(r.width<0)r.width=0;
    if(r.height<0)r.height=0;
    return r;
}
static ScStatus ui_edit_wrap(UiEdit *v)
{
    UiNode *n=v->node;
    ScUiRect area=ui_edit_area(n);
    ScU32 width=area.width>0?(ScU32)area.width:1;
    ScFontInfo *font;
    ScU32 face,pixels;
    ui_font_role(n->owner,n->font_role,&face,&pixels,&font);
    ScU32 epoch=font->generation^(pixels<<16);
    if(v->wrap_width!=width || v->wrap_font!=epoch){ScArrayClear(v->wraps);
        v->wrap_revision=0;
        v->wrap_width=width;
        v->wrap_font=epoch;
    }
    if(v->wrap_revision==v->document->revision)return 0;
    ScContainerInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScStatus s=ScArrayQuery(v->wraps,&info);
    if(s<0)return ui_data_status(s);
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.root=v->document->root;
    source.bytes=ui_document_bytes(v->document);
    ScU32 at=0,hard=0,count=info.length,batch=0;
    if(count){UiWrapRow last;
        s=ScArrayGet(v->wraps,count-1,&last);
        if(s<0)return ui_data_status(s);
        at=last.next;
        hard=ui_piece_prefix_lines(source.root,at);
    }
    for(;;){UiWrapRow row={at,at,at,hard};
        ScI32 x=0;
        while(at<source.bytes){ScU32 scalar,next,end;
            ScI32 advance;
            s=ui_source_scalar(&source,at,&scalar,&next);
            if(s<0)return s;
            if(scalar=='\r' || scalar=='\n'){row.end=at;
                at=next;
                if(scalar=='\r' && at<source.bytes && ui_source_byte(&source,at)=='\n')at++;
                row.next=at;
                hard++;
                break;
            }
            s=ui_edit_cluster(v,&source,at,x,&end,&advance);
            if(s<0)return s;
            if(x && ((ScU32)x>=width || (ScU32)advance>width-(ScU32)x)){row.end=row.next=at;
                break;
            }
            if(x>INT_MAX-advance)return SC_UI_E_RANGE;
            x+=advance;
            at=end;
            row.end=row.next=at;
            if((++batch&4095u)==0)ScProcessYield();
        }
        s=ScArrayInsert(v->wraps,count++,&row,1);
        if(s<0)return ui_data_status(s);
        if(at==source.bytes){if(row.next>row.end){UiWrapRow empty={at,at,at,hard};
                s=ScArrayInsert(v->wraps,count++,&empty,1);
            }
            break;
        }
    }
    if(s<0)return ui_data_status(s);
    v->wrap_revision=v->document->revision;
    return 0;
}
static ScU32 ui_edit_wrap_row(UiEdit *v,ScU32 offset)
{
    ScView view;
    memset(&view,0,sizeof(view));
    view.size=sizeof(view);
    view.version=1;
    if(ScArrayView(v->wraps,&view)<0)return 0;
    const UiWrapRow *rows=(const UiWrapRow *)view.data;
    ScU32 lo=0,hi=view.bytes/sizeof(*rows);
    if(!hi)return 0;
    while(lo+1<hi){ScU32 mid=lo+(hi-lo)/2;
        if(rows[mid].start<=offset)lo=mid;
        else hi=mid;
    }
    return lo;
}
static ScStatus ui_edit_wrap_seek(UiEdit *v,const UiWrapRow *row,ScU32 requested,ScI32 goal,int by_x,ScU32 *out,ScI32 *position)
{
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.root=v->document->root;
    source.bytes=ui_document_bytes(v->document);
    ScU32 at=row->start;
    ScI32 x=0;
    while(at<row->end){ScU32 next;
        ScI32 advance;
        ScStatus s=ui_edit_cluster(v,&source,at,x,&next,&advance);
        if(s<0)return s;
        if(by_x){if(goal<x+advance/2)break;
            if(goal<x+advance){at=next;
                x+=advance;
                break;
            }
        }
        else if(at>=requested || next>requested)break;
        at=next;
        x+=advance;
    }
    *out=at;
    *position=x;
    return 0;
}
static void ui_edit_layout(UiNode *n)
{
    UiEdit *v=ui_edit_of(n);
    if(!v)return;
    ScU32 line,baseline;
    ui_font_line(n->owner,n->font_role,&line,&baseline);
    (void)baseline;
    ScU32 count=(v->document->root?v->document->root->breaks:0)+1;
    if((n->flags&SC_UI_WRAP) && n->kind==SC_UI_EDIT){ScStatus s=ui_edit_wrap(v);
        if(s<0){n->owner->notification_error=s;
            return;
        }
        ScContainerInfo rows;
        memset(&rows,0,sizeof(rows));
        rows.size=sizeof(rows);
        rows.version=1;
        ScArrayQuery(v->wraps,&rows);
        count=rows.length;
        n->content_width.lo=(ScU32)ui_edit_area(n).width;
        n->content_width.hi=0;
        n->scroll_x.lo=0;
        n->scroll_x.hi=0;
    }
    ScWideCounter height={0,0},part={count,0};
    for(ScU32 bit=0;bit<32;bit++){if(line&(1u<<bit)){ScU32 old=height.lo;
            height.lo+=part.lo;
            height.hi+=part.hi+(height.lo<old);
        }
        part.hi=(part.hi<<1)|(part.lo>>31);
        part.lo<<=1;
    }
    n->content_height=height;
    n->scroll_step_y=line;
    n->scroll_step_x=(ScU32)ui_scale(n->owner,8);
    ScUiRect area=ui_edit_area(n);
    if(n->content_width.lo<(ScU32)area.width)n->content_width.lo=(ScU32)area.width;
}
static ScStatus ui_edit_selection_set(UiEdit *v,ScU32 anchor,ScU32 caret,ScU32 affinity,int notify)
{
    UiNode *n=v->node;
    ScUiDocument *d=v->document;
    UiTextSource source;
    ScU32 bytes=ui_document_bytes(d);
    ScStatus s;
    memset(&source,0,sizeof(source));
    source.root=d->root;
    source.bytes=bytes;
    if(affinity>1 || !ui_source_boundary(&source,anchor) || !ui_source_boundary(&source,caret) || !ui_source_break(&source,anchor,0) || !ui_source_break(&source,caret,0))return SC_UI_E_RANGE;
    if(v->anchor==anchor && v->caret==caret && v->affinity==affinity)return 0;
    ScUiTextSelection selected={sizeof(selected),1,anchor,caret,affinity,d->revision,0,0};
    ScUiEvent event;
    ui_event_record(&event,n,SC_UI_EVENT_SELECTION_CHANGING);
    event.flags=SC_UI_EVENT_PROGRAMMATIC|SC_UI_EVENT_CANCELABLE;
    event.data=&selected;
    event.bytes=sizeof(selected);
    ScU32 revision=d->revision;
    s=notify?ui_emit(n->owner,n,&event):0;
    if(s==SC_UI_VETO)s=SC_UI_E_CANCELLED;
    else if(s>0)s=0;
    if(s<0)return s;
    if(n->private_flags&UI_NODE_RETIRED || v->document!=d || d->revision!=revision)return SC_UI_E_STALE;
    v->anchor=anchor;
    v->caret=caret;
    v->affinity=affinity;
    v->caret_on=1;
    ui_node_damage(n);
    if(notify){s=ui_event(n,SC_UI_EVENT_SELECTION_CHANGED,&selected,sizeof(selected));
        if(s<0)n->owner->notification_error=s;
    }
    return 0;
}
ScStatus ScUiEditGetSelection(ScUiContext *context,ScUiNode id,ScUiTextSelection *out)
{
    UiNode *n=ui_node(context,id);
    UiEdit *v=ui_edit_of(n);
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!n)return SC_UI_E_STALE;
    if(!v)return SC_UI_E_INVALID;
    ScUiTextSelection selected={sizeof(selected),1,v->anchor,v->caret,v->affinity,v->document->revision,0,0};
    memcpy(out,&selected,sizeof(selected));
    return 0;
}
ScStatus ScUiEditSetSelection(ScUiContext *context,ScUiNode id,const ScUiTextSelection *selection)
{
    UiNode *n=ui_node(context,id);
    UiEdit *v=ui_edit_of(n);
    ScStatus s=ui_input(selection,sizeof(*selection));
    if(s<0)return s;
    if(!n)return SC_UI_E_STALE;
    if(!v || selection->flags || selection->reserved)return SC_UI_E_INVALID;
    if(selection->revision && selection->revision!=v->document->revision)return SC_UI_E_STALE;
    ui_node_hold(n);
    s=ui_edit_selection_set(v,selection->anchor,selection->caret,selection->affinity,1);
    ui_node_leave(n);
    return s;
}
ScStatus ScUiEditSelectAll(ScUiContext *context,ScUiNode id)
{
    UiNode *n=ui_node(context,id);
    UiEdit *v=ui_edit_of(n);
    if(!n)return SC_UI_E_STALE;
    if(!v)return SC_UI_E_INVALID;
    ui_node_hold(n);
    ScStatus s=ui_edit_selection_set(v,0,ui_document_bytes(v->document),0,1);
    ui_node_leave(n);
    return s;
}
ScStatus ScUiEditHitTest(ScUiContext *context,ScUiNode id,ScI32 x,ScI32 y,ScU32 *offset,ScU32 *affinity)
{
    UiNode *n=ui_node(context,id);
    UiEdit *v=ui_edit_of(n);
    if(!n)return SC_UI_E_STALE;
    if(!v || !offset || !affinity)return SC_UI_E_INVALID;
    ScUiRect area=ui_edit_area(n);
    ScU32 height,baseline;
    ui_font_line(context,n->font_role,&height,&baseline);
    (void)baseline;
    if(!height)return SC_UI_E_FAILED;
    ScI32 py=y-area.y;
    if(py<0)py=0;
    ScU32 line=n->kind==SC_UI_EDIT?(ScU32)py/height+n->scroll_y.lo/height:0;
    if((n->flags&SC_UI_WRAP) && n->kind==SC_UI_EDIT){ScStatus s=ui_edit_wrap(v);
        if(s<0)return s;
        ScContainerInfo info;
        UiWrapRow row;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        ScArrayQuery(v->wraps,&info);
        if(line>=info.length)line=info.length-1;
        s=ScArrayGet(v->wraps,line,&row);
        ScI32 px=x-area.x;
        if(px<0)px=0;
        ScI32 actual;
        if(s==0)s=ui_edit_wrap_seek(v,&row,0,px,1,offset,&actual);
        if(s==0)*affinity=(*offset==row.end && row.end==row.next);
        return s;
    }
    ScU32 lines=v->document->root?v->document->root->breaks:0;
    if(line>lines)line=lines;
    UiEditLine *layout;
    ScStatus s=ui_edit_line(v,line,&layout);
    if(s<0)return s;
    ScI32 px=x-area.x;
    if(px<0)px=0;
    if(n->scroll_x.hi || n->scroll_x.lo>(ScU32)(INT_MAX-px))return SC_UI_E_RANGE;
    px+=(ScI32)n->scroll_x.lo;
    ScI32 actual;
    s=ui_edit_line_seek(v,layout,0,px,1,offset,&actual);
    if(s==0)*affinity=px>actual;
    return s;
}
ScStatus ScUiEditSetSpans(ScUiContext *context,ScUiNode id,const ScUiStyleSpan *spans,ScU32 count)
{
    UiNode *n=ui_node(context,id);
    UiEdit *v=ui_edit_of(n);
    ScArray *candidate=0;
    ScStatus s;
    ScU32 last=0;
    if(!n)return SC_UI_E_STALE;
    if(!v || (!spans && count))return SC_UI_E_INVALID;
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.root=v->document->root;
    source.bytes=ui_document_bytes(v->document);
    for(ScU32 i=0;i<count;i++){
        s=ui_input(spans+i,sizeof(*spans));
        if(s<0)return s;
        if(spans[i].revision!=v->document->revision || spans[i].start<last || spans[i].end<spans[i].start || spans[i].end>source.bytes
            || spans[i].foreground_role>=24 || spans[i].background_role>=24 || spans[i].flags&~3u
            || !ui_source_boundary(&source,spans[i].start) || !ui_source_boundary(&source,spans[i].end))return SC_UI_E_INVALID;
        last=spans[i].end;
    }
    s=ui_array_new(sizeof(*spans),&candidate);
    if(s==0 && count)s=ScArrayInsert(candidate,0,spans,count);
    if(s<0){ScArrayRelease(candidate);
        return ui_data_status(s);
    }
    ScArray *old=v->spans;
    v->spans=candidate;
    v->span_revision=v->document->revision;
    ScArrayRelease(old);
    ui_node_damage(n);
    return 0;
}

static ScStatus ui_edit_draw_cluster(UiEdit *v,ScUiPainter *p,UiTextSource *source,ScU32 at,ScU32 end,ScI32 x,ScI32 y,ScU32 baseline,ScU32 role)
{
    UiNode *n=v->node;
    ScU32 scalar,next,walk=at;
    ScStatus s=0;
    p->text_role=role;
    while(walk<end){
        if(n->flags&SC_UI_PASSWORD){scalar=0x2022;
            walk=end;
        }
        else{s=ui_source_scalar(source,walk,&scalar,&next);
            if(s<0)return s;
            walk=next;
        }
        if(scalar=='\t')continue;
        if(ScUnicodeIsControl(scalar))scalar=0x2400+(scalar<=31?scalar:31);
        const ScFontGlyph *g;
        ScU32 lease;
        s=ui_glyph(n->owner,n->font_role,scalar,&g,&lease);
        if(s<0)return s;
        for(ScU32 row=0;row<g->height;row++){
            ScI32 py=p->origin_y+y+(ScI32)baseline-g->baseline+(ScI32)row;
            if(py<p->clip.y || py>=p->clip.y+p->clip.height)continue;
            for(ScU32 column=0;column<g->width;column++)if(g->rows[row][column/32]&(1u<<(31-(column&31)))){
                ScI32 px=p->origin_x+x+g->left+(ScI32)column;
                if(px>=p->clip.x && px<p->clip.x+p->clip.width)p->root->pixels[(ScU32)py*p->root->width+(ScU32)px]=ui_color(n->owner,role);
            }
        }
        x+=g->advance;
        ui_glyph_release(n->owner,lease);
    }
    return s;
}
static ScStatus ui_edit_paint(UiNode *n,ScUiPainter *p)
{
    UiEdit *v=ui_edit_of(n);
    ScUiRect outer={0,0,n->rectangle.width,n->rectangle.height},area=ui_edit_area(n);
    ScStatus s;
    ScU32 height,baseline;
    ui_font_line(n->owner,n->font_role,&height,&baseline);
    if(n->kind==SC_UI_LABEL)ui_fill(p,&outer,SC32_COLOR_FACE);
    else ui_control_surface(n,p,&outer,SC32_COLOR_PAPER,1);
    if(n->state&SC_UI_INVALID){ScUiRect invalid={1,outer.height-2,outer.width-2,1};
        ui_fill(p,&invalid,SC32_COLOR_ALERT);
    }
    if(n->kind==SC_UI_SPINBOX){ScI32 side=ui_scale(n->owner,18);
        ScUiRect up={outer.width-side,1,side-1,outer.height/2-1},down={outer.width-side,outer.height/2,side-1,outer.height/2-1};
        ui_fill(p,&up,SC32_COLOR_FACE);
        ui_fill(p,&down,SC32_COLOR_FACE);
        ui_border(p,&up,0);
        ui_border(p,&down,0);
        ui_control_arrow(p,up.x+side/2-3,up.y+up.height/2-2,4,1,SC32_COLOR_TEXT);
        ui_control_arrow(p,down.x+side/2-3,down.y+down.height/2-2,4,0,SC32_COLOR_TEXT);
    }
    s=ScUiPainterClip(p,&area);
    if(s<0)return s;
    UiTextSource source;
    memset(&source,0,sizeof(source));
    source.root=v->document->root;
    source.bytes=ui_document_bytes(v->document);
    ScView styles;
    memset(&styles,0,sizeof(styles));
    styles.size=sizeof(styles);
    styles.version=1;
    ScArrayView(v->spans,&styles);
    const ScUiStyleSpan *spans=(const ScUiStyleSpan *)styles.data;
    ScU32 span_count=v->span_revision==v->document->revision?styles.bytes/sizeof(*spans):0;
    int wrap=(n->flags&SC_UI_WRAP) && n->kind==SC_UI_EDIT;
    ScU32 first=n->kind==SC_UI_EDIT?n->scroll_y.lo/height:0,lines=v->document->root?v->document->root->breaks:0;
    if(wrap){s=ui_edit_wrap(v);
        if(s<0){ScUiPainterRestore(p);
            return s;
        }
        ScContainerInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        ScArrayQuery(v->wraps,&info);
        lines=info.length-1;
    }
    ScI32 y=area.y-(n->kind==SC_UI_EDIT?(ScI32)(n->scroll_y.lo%height):0);
    ScU32 selected_start=v->anchor<v->caret?v->anchor:v->caret,selected_end=v->anchor>v->caret?v->anchor:v->caret;
    for(ScU32 row=first;s==0 && row<=lines && y<area.y+area.height;row++,y+=(ScI32)height){
        UiEditLine *line,temporary;
        UiWrapRow wrapped;
        ScU32 offset;
        ScI32 position;
        if(wrap){s=ScArrayGet(v->wraps,row,&wrapped);
            if(s<0)break;
            memset(&temporary,0,sizeof(temporary));
            temporary.start=wrapped.start;
            temporary.end=wrapped.end;
            temporary.pos=wrapped.end;
            line=&temporary;
            offset=wrapped.start;
            position=0;
        }
        else{s=ui_edit_line(v,row,&line);
            if(s<0)break;
            s=ui_edit_line_seek(v,line,0,(ScI32)n->scroll_x.lo,2,&offset,&position);
            if(s<0)break;
        }
        while(offset<line->end && position-(ScI32)n->scroll_x.lo<area.width){
            ScU32 next,foreground=(n->effective&SC_UI_ENABLED)?SC32_COLOR_TEXT:SC32_COLOR_MUTED,background=SC32_COLOR_PAPER,decorations=0;
            ScI32 width;
            s=ui_edit_cluster(v,&source,offset,position,&next,&width);
            if(s<0)break;
            for(ScU32 j=0;j<span_count;j++)if(offset>=spans[j].start && offset<spans[j].end){foreground=spans[j].foreground_role;
                background=spans[j].background_role;
                decorations=spans[j].flags;
                break;
            }
            int selected=offset<selected_end && next>selected_start;
            if(selected){foreground=SC32_COLOR_SELECT_TEXT;
                background=SC32_COLOR_SELECT;
            }
            ScUiRect segment={area.x+position-(ScI32)n->scroll_x.lo,y,width,(ScI32)height};
            if(background!=SC32_COLOR_PAPER)ui_fill(p,&segment,background);
            s=ui_edit_draw_cluster(v,p,&source,offset,next,segment.x,y,baseline,foreground);
            if(decorations&SC_UI_STYLE_UNDERLINE){ScUiRect edge={segment.x,y+(ScI32)baseline+1,width,1};
                ui_fill(p,&edge,foreground);
            }
            if(decorations&SC_UI_STYLE_STRIKE){ScUiRect edge={segment.x,y+(ScI32)baseline/2,width,1};
                ui_fill(p,&edge,foreground);
            }
            if((n->effective&SC_UI_FOCUSED) && v->caret_on && v->caret==offset){ScUiRect caret={segment.x,y,1,(ScI32)height};
                ui_fill(p,&caret,SC32_COLOR_TEXT);
            }
            offset=next;
            position+=width;
        }
        if(s==0 && (!wrap || wrapped.next>wrapped.end) && selected_start<=line->end && selected_end>line->end){ScUiRect eol={area.x+position-(ScI32)n->scroll_x.lo,y,ui_scale(n->owner,5),(ScI32)height};
            ui_fill(p,&eol,SC32_COLOR_SELECT);
        }
        if(s==0 && offset==line->end && (n->effective&SC_UI_FOCUSED) && v->caret_on && v->caret==line->end
            && (!wrap || wrapped.next>wrapped.end || row==lines || v->affinity)){ScUiRect caret={area.x+position-(ScI32)n->scroll_x.lo,y,1,(ScI32)height};
            ui_fill(p,&caret,SC32_COLOR_TEXT);
        }
        if(!wrap && line->pos==line->end && (ScU32)line->x>n->content_width.lo)n->content_width.lo=(ScU32)line->x;
    }
    ScUiPainterRestore(p);
    return s<0?s:p->error;
}
static ScStatus ui_edit_reveal(UiEdit *v)
{
    UiNode *n=v->node;
    ScU32 line=ui_piece_prefix_lines(v->document->root,v->caret),height,baseline,offset;
    ScI32 x;
    ui_font_line(n->owner,n->font_role,&height,&baseline);
    (void)baseline;
    UiEditLine *layout;
    ScStatus s=ui_edit_line(v,line,&layout);
    if(s<0)return s;
    if((n->flags&SC_UI_WRAP) && n->kind==SC_UI_EDIT){s=ui_edit_wrap(v);
        if(s<0)return s;
        line=ui_edit_wrap_row(v,v->caret);
        UiWrapRow row;
        s=ScArrayGet(v->wraps,line,&row);
        if(s==0)s=ui_edit_wrap_seek(v,&row,v->caret,0,0,&offset,&x);
    }
    else s=ui_edit_line_seek(v,layout,v->caret,0,0,&offset,&x);
    if(s<0)return s;
    ScUiRect area=ui_edit_area(n);
    if(area.width<1 || area.height<1)return 0;
    if(n->scroll_x.hi || n->scroll_x.lo>(ScU32)x){n->scroll_x.lo=(ScU32)x;
        n->scroll_x.hi=0;
    }
    else if((ScU32)x>n->scroll_x.lo+(ScU32)area.width-1)n->scroll_x.lo=(ScU32)(x-area.width+1);
    if(n->kind==SC_UI_EDIT){ScU32 top=line*height;
        if(line && top/line!=height)return SC_UI_E_RANGE;
        if(n->scroll_y.hi || n->scroll_y.lo>top){n->scroll_y.lo=top;
            n->scroll_y.hi=0;
        }
        else if(top+height>n->scroll_y.lo+(ScU32)area.height)n->scroll_y.lo=top+height-(ScU32)area.height;
    }
    return 0;
}
static ScStatus ui_edit_filter(UiNode *n,const ScUiText *text,ScString **out)
{
    if(n->kind==SC_UI_EDIT || n->kind==SC_UI_LABEL)return ui_text_copy(text,out);
    unsigned char *filtered=0;
    if(text->bytes){filtered=(unsigned char *)malloc(text->bytes);
        if(!filtered)return SC_UI_E_MEMORY;
    }
    ScU32 used=0;
    for(ScU32 i=0;i<text->bytes;i++)if(text->data[i]!='\r' && text->data[i]!='\n')filtered[used++]=(unsigned char)text->data[i];
    ScStatus s=ScStringCreate(filtered,used,out);
    free(filtered);
    return ui_data_status(s);
}
static ScStatus ui_edit_replace_range(UiEdit *v,const ScUiText *text,ScU32 start,ScU32 end)
{
    UiNode *n=v->node;
    ScUiDocument *d=v->document;
    ScString *filtered=0;
    ScStatus s;
    if((n->state&SC_UI_READONLY) || d->flags&SC_UI_DOCUMENT_READONLY)return SC_UI_E_CANCELLED;
    s=ui_edit_filter(n,text,&filtered);
    if(s<0)return s;
    ScUiText actual;
    ui_text_view(filtered,&actual);
    ScUiEvent event;
    ui_event_record(&event,n,SC_UI_EVENT_VALUE_CHANGING);
    event.flags=SC_UI_EVENT_DEVICE|SC_UI_EVENT_CANCELABLE;
    event.data=&actual;
    event.bytes=sizeof(actual);
    ScU32 revision=d->revision;
    s=ui_emit(n->owner,n,&event);
    if(s==SC_UI_VETO)s=SC_UI_E_CANCELLED;
    else if(s>0)s=0;
    if(s==0 && (n->private_flags&UI_NODE_RETIRED || v->document!=d || d->revision!=revision))s=SC_UI_E_STALE;
    if(s==0)s=ScUiDocumentReplace(d,start,end-start,&actual,SC_UI_REPLACE_NEW_UNDO);
    ScStringRelease(filtered);
    if(s<0 || n->private_flags&UI_NODE_RETIRED || v->document!=d)return s;
    s=ui_edit_reveal(v);
    ui_node_damage(n);
    if(n->kind==SC_UI_SPINBOX){ScString *whole=0;
        const char *z=0;
        ScStatus copy=ui_edit_copy_text(n,&whole);
        if(copy==0)copy=ScStringCStr(whole,&z);
        if(copy==0){char *tail;
            errno=0;
            long number=strtol(z,&tail,10);
            UiControl *c=ui_control(n);
            if(errno==ERANGE || tail==z || *tail || number<c->minimum || number>c->maximum)n->state|=SC_UI_INVALID;
            else{n->state&=~SC_UI_INVALID;
                ScValue value={sizeof(ScValue),1,SC_VALUE_I32,0,(ScU32)number,0,0,0};
                n->value.value=value;
            }
            ui_tree_effective(n);
        }
        ScStringRelease(whole);
        if(copy<0)n->owner->notification_error=copy;
    }
    return s;
}
static ScStatus ui_edit_replace_selection(UiEdit *v,const ScUiText *text)
{return ui_edit_replace_range(v,text,v->anchor<v->caret?v->anchor:v->caret,v->anchor>v->caret?v->anchor:v->caret);
}
static ScStatus ui_edit_clipboard_copy(UiEdit *v)
{
    UiNode *n=v->node;
    if(n->flags&SC_UI_PASSWORD)return SC_UI_E_CANCELLED;
    ScU32 start=v->anchor<v->caret?v->anchor:v->caret,end=v->anchor>v->caret?v->anchor:v->caret;
    if(start==end)return SC_UI_E_NOT_FOUND;
    ScString *copy=0;
    const char *z=0;
    ScStatus s=ScUiDocumentCopy(v->document,start,end-start,&copy);
    if(s==0)s=ScStringCStr(copy,&z);
    if(s==0 && memchr(z,0,end-start))s=SC_UI_E_NOT_SUPPORTED;
    ScClipboardInfo information;
    memset(&information,0,sizeof(information));
    information.size=sizeof(information);
    information.version=1;
    if(s==0)s=ScClipboardQuery(&information);
    ScClipboardDesc desc;
    ScClipboardCandidate candidate=0;
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    /* public Offer本身支持完整正文的分段候选；预算须同时容纳格式元数据。 */
    if(!ui_add(end-start,4097u,&desc.budget_bytes))s=SC_UI_E_RANGE;
    ScU32 epoch=information.epoch;
    if(s==0)s=ScClipboardBegin(&desc,&candidate);
    if(s==0)s=ScClipboardOffer(candidate,SC32_CLIPBOARD_TEXT,z,end-start+1);
    if(s==0)s=ScClipboardCommit(candidate,epoch,&information);
    if(s<0 && candidate)ScClipboardAbort(candidate);
    ScStringRelease(copy);
    return ui_cause(n->owner,s,UI_OP_NODE,n->id);
}
ScStatus ScUiEditCopy(ScUiContext *context,ScUiNode id)
{
    UiNode *n=ui_node(context,id);
    UiEdit *v=ui_edit_of(n);
    if(!n)return SC_UI_E_STALE;
    if(!v)return SC_UI_E_INVALID;
    ui_node_hold(n);
    ScStatus s=ui_edit_clipboard_copy(v);
    ui_node_leave(n);
    return s;
}
ScStatus ScUiEditCut(ScUiContext *context,ScUiNode id)
{
    UiNode *n=ui_node(context,id);
    UiEdit *v=ui_edit_of(n);
    UiPiece *candidate=0;
    UiEditHold *holds=0;
    UiUndo *undo=0;
    ScU32 count=0,retained=0,cost=0;
    ScStatus s;
    if(!n)return SC_UI_E_STALE;
    if(!v)return SC_UI_E_INVALID;
    if(n->state&SC_UI_READONLY || n->flags&SC_UI_PASSWORD)return SC_UI_E_CANCELLED;
    ScUiDocument *d=v->document;
    s=ui_object_enter(&d->object,UI_DOCUMENT);
    if(s<0)return s;
    ui_node_hold(n);
    if(d->flags&SC_UI_DOCUMENT_READONLY || d->revision==UINT_MAX || d->next_identity==UINT_MAX){s=SC_UI_E_CANCELLED;
        goto done;
    }
    ScU32 start=v->anchor<v->caret?v->anchor:v->caret,end=v->anchor>v->caret?v->anchor:v->caret;
    if(start==end){s=SC_UI_E_NOT_FOUND;
        goto done;
    }
    ScUiText empty={sizeof(empty),1,"",0};
    ScUiEvent event;
    ui_event_record(&event,n,SC_UI_EVENT_VALUE_CHANGING);
    event.flags=SC_UI_EVENT_CANCELABLE|SC_UI_EVENT_PROGRAMMATIC;
    event.data=&empty;
    event.bytes=sizeof(empty);
    ScU32 revision=d->revision;
    s=ui_emit(context,n,&event);
    if(s==SC_UI_VETO)s=SC_UI_E_CANCELLED;
    else if(s>0)s=0;
    if(s==0 && (n->private_flags&UI_NODE_RETIRED || v->document!=d || d->revision!=revision))s=SC_UI_E_STALE;
    if(s<0)goto done;
    ui_piece_allocated=0;
    s=ui_piece_replace(d->root,start,end-start,0,&candidate,&retained);
    cost=ui_piece_allocated;
    if(!ui_add(cost,retained,&cost))cost=UINT_MAX;
    if(s==0)s=ui_edit_holds(d,&holds,&count);
    if(s==0 && !d->group && !d->group_discard)s=ui_undo_candidate(d,holds,count,&undo);
    if(s==0)s=ui_edit_clipboard_copy(v);
    if(s==0){s=ui_document_commit(d,candidate,start,end-start,0,SC_UI_REPLACE_NEW_UNDO,cost,1,holds,count,undo,0,0,0);
        if(s==0){candidate=0;
            holds=0;
            count=0;
            undo=0;
        }
    }
    done:ui_undo_free(undo);
    ui_edit_holds_release(holds,count);
    ui_piece_release(candidate);
    ui_node_leave(n);
    ui_object_leave(&d->object);
    return s;
}
ScStatus ScUiEditPaste(ScUiContext *context,ScUiNode id)
{
    UiNode *n=ui_node(context,id);
    UiEdit *v=ui_edit_of(n);
    ScClipboardInfo info;
    ScClipboardFormatInfo formats[64];
    ScClipboardPage page;
    ScU32 bytes=0,cursor=0;
    unsigned char *body=0;
    ScStatus s;
    if(!n)return SC_UI_E_STALE;
    if(!v)return SC_UI_E_INVALID;
    if(n->state&SC_UI_READONLY)return SC_UI_E_CANCELLED;
    ui_node_hold(n);
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScClipboardOpen(&info);
    if(s<0)goto done;
    while(s==0 && !bytes){memset(&page,0,sizeof(page));
        page.size=sizeof(page);
        page.version=1;
        s=ScClipboardFormats(info.handle,cursor,formats,64,&page);
        if(s<0)break;
        for(ScU32 i=0;i<page.count;i++)if(formats[i].format==SC32_CLIPBOARD_TEXT)bytes=formats[i].bytes;
        if(page.flags&SC32_CLIPBOARD_FINISHED)break;
        if(page.cursor==cursor){s=SC_UI_E_IO;
            break;
        }
        cursor=page.cursor;
    }
    if(s==0 && !bytes)s=SC_UI_E_NOT_FOUND;
    if(s==0){body=(unsigned char *)malloc(bytes);
        if(!body)s=SC_UI_E_MEMORY;
    }
    for(ScU32 at=0;s==0 && at<bytes;){ScU32 take=bytes-at;
        if(take>65536)take=65536;
        ScBrokerIoResult io;
        memset(&io,0,sizeof(io));
        io.size=sizeof(io);
        io.version=1;
        s=ScClipboardRead(info.handle,SC32_CLIPBOARD_TEXT,at,body+at,take,&io);
        if(s==0 && (!io.transferred || io.transferred>take || io.total_bytes!=bytes))s=SC_UI_E_IO;
        if(s==0)at+=io.transferred;
    }
    if(s==0 && (body[bytes-1] || memchr(body,0,bytes-1) || ScUtf8Validate(body,bytes-1,0)<0))s=SC_UI_E_UTF8;
    if(s==0){ScUiText text={sizeof(text),1,(const char *)body,bytes-1};
        s=ui_edit_replace_selection(v,&text);
    }
    {ScStatus close=ScClipboardClose(info.handle);
        if(s==0)s=close;
    }
    done:free(body);
    s=ui_cause(context,s,UI_OP_NODE,id);
    ui_node_leave(n);
    return s;
}

static ScStatus ui_edit_move(UiEdit *v,ScU32 key,ScU32 modifiers)
{
    UiNode *n=v->node;
    ScUiDocument *d=v->document;
    UiTextSource source;
    ScU32 next=v->caret,line=ui_piece_prefix_lines(d->root,v->caret);
    ScStatus s=0;
    memset(&source,0,sizeof(source));
    source.root=d->root;
    source.bytes=ui_document_bytes(d);
    int shift=(modifiers&SC32_MOD_SHIFT)!=0,control=(modifiers&SC32_MOD_CONTROL)!=0;
    int wrap=(n->flags&SC_UI_WRAP) && n->kind==SC_UI_EDIT;
    UiWrapRow wrapped;
    if(wrap){s=ui_edit_wrap(v);
        if(s<0)return s;
        line=ui_edit_wrap_row(v,v->caret);
        s=ScArrayGet(v->wraps,line,&wrapped);
        if(s<0)return ui_data_status(s);
    }
    if(key==SC32_KEY_LEFT || key==SC32_KEY_RIGHT){
        if(!shift && v->anchor!=v->caret)next=key==SC32_KEY_LEFT?(v->anchor<v->caret?v->anchor:v->caret):(v->anchor>v->caret?v->anchor:v->caret);
        else s=ui_source_step(&source,v->caret,&next,control,key==SC32_KEY_LEFT);
        v->goal_valid=0;
    }
    else if(key==SC32_KEY_HOME){next=control?0:wrap?wrapped.start:ui_document_line(d,line);
        v->goal_valid=0;
    }
    else if(key==SC32_KEY_END){next=control?source.bytes:wrap?wrapped.end:ui_document_line_end(d,line);
        v->goal_valid=0;
    }
    else{
        ScU32 height,baseline,offset;
        ScI32 x;
        ui_font_line(n->owner,n->font_role,&height,&baseline);
        (void)baseline;
        UiEditLine *layout;
        s=ui_edit_line(v,line,&layout);
        if(s<0)return s;
        if(!v->goal_valid){s=wrap?ui_edit_wrap_seek(v,&wrapped,v->caret,0,0,&offset,&x):ui_edit_line_seek(v,layout,v->caret,0,0,&offset,&x);
            if(s<0)return s;
            v->goal_x=x;
            v->goal_valid=1;
        }
        ScU32 step=(key==SC32_KEY_PAGEUP || key==SC32_KEY_PAGEDOWN)?(ScU32)(ui_edit_area(n).height/(ScI32)height):1;
        if(!step)step=1;
        ScU32 maximum=d->root?d->root->breaks:0;
        if(wrap){ScContainerInfo info;
            memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            ScArrayQuery(v->wraps,&info);
            maximum=info.length-1;
        }
        if(key==SC32_KEY_UP || key==SC32_KEY_PAGEUP)line=line<step?0:line-step;
        else line=step>maximum-line?maximum:line+step;
        if(wrap){s=ScArrayGet(v->wraps,line,&wrapped);
            if(s==0)s=ui_edit_wrap_seek(v,&wrapped,0,v->goal_x,1,&next,&x);
        }
        else{s=ui_edit_line(v,line,&layout);
            if(s==0)s=ui_edit_line_seek(v,layout,0,v->goal_x,1,&next,&x);
        }
    }
    if(s==0)s=ui_edit_selection_set(v,shift?v->anchor:next,next,0,1);
    if(s==0 && !(n->private_flags&UI_NODE_RETIRED))s=ui_edit_reveal(v);
    return s;
}
static ScStatus ui_edit_event(UiNode *n,const ScUiEvent *e)
{
    UiEdit *v=ui_edit_of(n);
    ScStatus s=0;
    if(!v)return 0;
    if(e->type==SC_UI_EVENT_FOCUS_IN){v->caret_on=1;
        ui_control_timer(n,50,50,&v->caret_timer);
        return 0;
    }
    if(e->type==SC_UI_EVENT_FOCUS_OUT){ui_control_timer_cancel(n,&v->caret_timer);
        v->caret_on=0;
        v->dragging=0;
        ui_node_damage(n);
        return 0;
    }
    if(e->type==SC_UI_EVENT_TIMER && e->command==v->caret_timer){v->caret_on=!v->caret_on;
        ui_node_damage(n);
        return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_POINTER_ENTER){UiNode *root=ui_root(n->owner,n->id);
        ScPointerSetCursor(root->window->window,SC32_CURSOR_TEXT);
    }
    if(e->type==SC_UI_EVENT_CAPTURE_LOST || e->type==SC_UI_EVENT_INPUT_SYNC){v->dragging=0;
        v->suppress_scalar=0;
        return 0;
    }
    if(e->type==SC_UI_EVENT_POINTER_DOWN && ui_control_left_edge(e) && (e->buttons&1)){
        ScU32 offset,affinity;
        s=ScUiEditHitTest(n->owner,n->id,e->x,e->y,&offset,&affinity);
        if(s<0)return s;
        ScUiFocus(n->owner,n->id);
        if(n->private_flags&UI_NODE_RETIRED)return SC_UI_E_CANCELLED;
        if(e->tick-v->last_click_tick<=35 && offset==v->last_click_byte && e->x-v->click_x<4 && v->click_x-e->x<4 && e->y-v->click_y<4 && v->click_y-e->y<4)v->clicks=(v->clicks%3)+1;
        else v->clicks=1;
        v->last_click_tick=e->tick;
        v->last_click_byte=offset;
        v->click_x=e->x;
        v->click_y=e->y;
        ScU32 anchor=(e->modifiers&SC32_MOD_SHIFT)?v->anchor:offset,caret=offset;
        if(v->clicks==2){UiTextSource source;
            memset(&source,0,sizeof(source));
            source.root=v->document->root;
            source.bytes=ui_document_bytes(v->document);
            anchor=offset;
            if(!ui_source_break(&source,offset,1))ui_source_step(&source,offset,&anchor,1,1);
            ui_source_step(&source,anchor,&caret,1,0);
        }
        else if(v->clicks==3){ScU32 line=ui_piece_prefix_lines(v->document->root,offset);
            anchor=ui_document_line(v->document,line);
            caret=ui_document_line(v->document,line+1);
        }
        s=ui_edit_selection_set(v,anchor,caret,affinity,1);
        if(s==0){v->dragging=1;
            ScUiCapture(n->owner,n->id,1);
        }
        return s<0?s:SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_POINTER_MOVE && v->dragging){
        ScU32 offset,affinity;
        s=ScUiEditHitTest(n->owner,n->id,e->x,e->y,&offset,&affinity);
        if(s==0)s=ui_edit_selection_set(v,v->anchor,offset,affinity,1);
        if(s==0)s=ui_edit_reveal(v);
        return s<0?s:SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_POINTER_UP && ui_control_left_edge(e) && v->dragging){v->dragging=0;
        ScUiCapture(n->owner,n->id,0);
        return SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_WHEEL && n->kind==SC_UI_EDIT){
        ScI32 ticks=e->message?(ScI32)e->message->data[2]:0;
        ScWideCounter y=n->scroll_y;
        ScU32 step=n->scroll_step_y*3;
        if(ticks>0)y.lo=y.lo<step?0:y.lo-step;
        else if(ticks<0)ui_wide_add(&y,step);
        s=ScUiScrollTo(n->owner,n->id,&n->scroll_x,&y);
        return s<0?s:SC_UI_HANDLED;
    }
    if(e->type==SC_UI_EVENT_TEXT){
        if(v->suppress_scalar==e->scalar){v->suppress_scalar=0;
            return SC_UI_HANDLED;
        }
        v->suppress_scalar=0;
        if(e->modifiers&(SC32_MOD_CONTROL|SC32_MOD_ALT))return 0;
        if(ScUnicodeIsControl(e->scalar) && e->scalar!='\t' && e->scalar!='\n')return 0;
        char encoded[4];
        ScU32 bytes;
        s=ScUtf8Encode(e->scalar,encoded,sizeof(encoded),&bytes);
        if(s<0)return SC_UI_E_UTF8;
        ScUiText text={sizeof(text),1,encoded,bytes};
        s=ui_edit_replace_selection(v,&text);
        return s<0?s:SC_UI_HANDLED;
    }
    if(e->type!=SC_UI_EVENT_KEY_DOWN)return 0;
    ScU32 key=e->key;
    if(e->modifiers&SC32_MOD_CONTROL){
        if((n->state&SC_UI_READONLY) && (key=='z' || key=='Z' || key=='y' || key=='Y'))return SC_UI_HANDLED;
        if(key=='a' || key=='A')s=ScUiEditSelectAll(n->owner,n->id);
        else if(key=='c' || key=='C')s=ScUiEditCopy(n->owner,n->id);
        else if(key=='x' || key=='X')s=ScUiEditCut(n->owner,n->id);
        else if(key=='v' || key=='V')s=ScUiEditPaste(n->owner,n->id);
        else if(key=='z' || key=='Z')s=(e->modifiers&SC32_MOD_SHIFT)?ScUiDocumentRedo(v->document):ScUiDocumentUndo(v->document);
        else if(key=='y' || key=='Y')s=ScUiDocumentRedo(v->document);
        else goto navigation;
        return s<0?s:SC_UI_HANDLED;
    }
    navigation:
    if(key>=SC32_KEY_UP && key<=SC32_KEY_PAGEDOWN){s=ui_edit_move(v,key,e->modifiers);
        return s<0?s:SC_UI_HANDLED;
    }
    if(key==8 || key==SC32_KEY_DELETE){
        ScU32 anchor=v->anchor,caret=v->caret;
        if(anchor==caret){UiTextSource source;
            memset(&source,0,sizeof(source));
            source.root=v->document->root;
            source.bytes=ui_document_bytes(v->document);
            s=ui_source_step(&source,caret,&anchor,(e->modifiers&SC32_MOD_CONTROL)!=0,key==8);
            if(s<0)return s;
            if(key==SC32_KEY_DELETE){ScU32 swap=anchor;
                anchor=caret;
                caret=swap;
            }
        }
        ScU32 start=anchor<caret?anchor:caret,end=anchor>caret?anchor:caret;
        ScUiText empty={sizeof(empty),1,"",0};
        s=ui_edit_replace_range(v,&empty,start,end);
        return s<0?s:SC_UI_HANDLED;
    }
    if((key=='\n' && n->kind==SC_UI_EDIT) || (key=='\t' && n->kind==SC_UI_EDIT && (n->flags&SC_UI_EDITOR_TAB))){
        char byte=(char)key;
        ScUiText text={sizeof(text),1,&byte,1};
        s=ui_edit_replace_selection(v,&text);
        if(s==0)v->suppress_scalar=key;
        return s<0?s:SC_UI_HANDLED;
    }
    return 0;
}
