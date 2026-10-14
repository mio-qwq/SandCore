#include "SCLFMT.H"

/* mio：每个读取都由上层已验证的范围支配。磁盘缓冲即使不对齐也逐字节
 * 解析；不能用宿主 sizeof(struct) 或把磁盘地址直接转成 u32 指针。 */
unsigned int scl_read32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
        | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int scl_zero(const unsigned char *p, unsigned int n)
{
    for (unsigned int i = 0; i < n; i++) if (p[i]) return 0;
    return 1;
}

static int scl_table(const unsigned char *h, unsigned int field,
                     unsigned int width, unsigned int size, unsigned int *cursor)
{
    unsigned int count = scl_read32(h + field + 4);
    if (scl_read32(h + field) != *cursor) return 0;
    if (*cursor > size || count > (size - *cursor) / width) return 0;
    *cursor += count * width;
    return 1;
}

static int scl_name(const unsigned char *strings, unsigned int size,
                    unsigned int offset, int library)
{
    if (offset >= size || (offset && strings[offset - 1])) return 0;
    unsigned int limit = library ? 35u : 127u, n = 0;
    while (n <= limit && n < size - offset && strings[offset + n]) n++;
    if (!n || n > limit || n >= size - offset) return 0;
    if (library) {
        if (n < 5 || strings[offset+n-4] != '.' || strings[offset+n-3] != 'S'
            || strings[offset+n-2] != 'C' || strings[offset+n-1] != 'L') return 0;
        for (unsigned int i = 0; i < n - 4; i++) {
            unsigned int c = strings[offset + i];
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return 0;
        }
    } else {
        for (unsigned int i = 0; i < n; i++) {
            unsigned int c = strings[offset + i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                || c == '_' || (i && c >= '0' && c <= '9'))) return 0;
        }
    }
    return 1;
}

static int scl_compare(const unsigned char *a, const unsigned char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a < *b ? -1 : *a != *b;
}

/* 段表按 RVA 排序。每次定位只二分，不让大量符号/重定位退化成乘积扫描。 */
static const unsigned char *scl_segment(const unsigned char *h, unsigned int rva,
                                       unsigned int size)
{
    unsigned int first = 0, count = scl_read32(h + SCL_H_SEGMENT_COUNT);
    unsigned int base = scl_read32(h + SCL_H_SEGMENTS);
    while (count) {
        unsigned int half = count / 2, index = first + half;
        const unsigned char *s = h + base + index * SCL_SEGMENT_SIZE;
        unsigned int start = scl_read32(s), length = scl_read32(s + 4);
        if (rva < start) count = half;
        else if (rva - start >= length) { first = index + 1; count -= half + 1; }
        else return size <= length - (rva - start) ? s : 0;
    }
    return 0;
}

static int scl_validate_view(const void *bytes, unsigned int size, int metadata_only)
{
    const unsigned char *h = (const unsigned char *)bytes;
    const unsigned char *strings;
    unsigned int cursor = SCL_HEADER_SIZE, image_size, string_size;
    unsigned int previous_end = 0, payload_end, file_size;
    if (!h || size < SCL_HEADER_SIZE || size > SCL_MAX_FILE_SIZE) return SCL_E_RANGE;
    for (unsigned int i = 0; i < 8; i++) if (h[i] != (unsigned char)SCL_MAGIC[i]) return SCL_E_FORMAT;
    if (scl_read32(h + SCL_H_HEADER_SIZE) != SCL_HEADER_SIZE
        || scl_read32(h + SCL_H_FLAGS) || !scl_zero(h + SCL_H_RESERVED, 24)) return SCL_E_FORMAT;
    if (scl_read32(h + SCL_H_VERSION) != SCL_FORMAT_VERSION
        || scl_read32(h + SCL_H_MACHINE) != SCL_MACHINE_I386
        || !scl_read32(h + SCL_H_ABI_MAJOR)) return SCL_E_VERSION;
    file_size = scl_read32(h + SCL_H_FILE_SIZE);
    if (file_size > SCL_MAX_FILE_SIZE || file_size < size ||
        (!metadata_only && file_size != size)) return SCL_E_RANGE;
    image_size = scl_read32(h + SCL_H_IMAGE_SIZE);
    if (!image_size || image_size > SCL_MAX_IMAGE_SIZE || image_size % SCL_PAGE_SIZE) return SCL_E_RANGE;
    if (!scl_table(h,SCL_H_SEGMENTS,SCL_SEGMENT_SIZE,size,&cursor)
        || !scl_table(h,SCL_H_EXPORTS,SCL_EXPORT_SIZE,size,&cursor)
        || !scl_table(h,SCL_H_IMPORTS,SCL_IMPORT_SIZE,size,&cursor)
        || !scl_table(h,SCL_H_RELOCS,SCL_RELOC_SIZE,size,&cursor)
        || !scl_table(h,SCL_H_DEPS,SCL_DEPENDENCY_SIZE,size,&cursor)) return SCL_E_RANGE;
    string_size = scl_read32(h + SCL_H_STRINGS_SIZE);
    if (scl_read32(h + SCL_H_STRINGS) != cursor || !string_size
        || string_size > size - cursor) return SCL_E_RANGE;
    strings = h + cursor;
    cursor += string_size;
    if (metadata_only && cursor != size) return SCL_E_RANGE;
    if (strings[string_size-1] || !scl_name(strings,string_size,scl_read32(h+SCL_H_NAME),1)) return SCL_E_SYMBOL;
    if (!scl_read32(h + SCL_H_SEGMENT_COUNT)) return SCL_E_FORMAT;
    payload_end = cursor;
    for (unsigned int i = 0; i < scl_read32(h + SCL_H_SEGMENT_COUNT); i++) {
        const unsigned char *s = h + scl_read32(h+SCL_H_SEGMENTS) + i*SCL_SEGMENT_SIZE;
        unsigned int rva=scl_read32(s), mem=scl_read32(s+4), off=scl_read32(s+8);
        unsigned int file=scl_read32(s+12), flags=scl_read32(s+16);
        if (!mem || rva%SCL_PAGE_SIZE || mem%SCL_PAGE_SIZE || rva<previous_end
            || rva>image_size || mem>image_size-rva || file>mem
            || scl_read32(s+20)!=SCL_PAGE_SIZE) return SCL_E_RANGE;
        if (!(flags&SCL_SEG_R) || (flags&~7u) || ((flags&SCL_SEG_W)&&(flags&SCL_SEG_X))) return SCL_E_FORMAT;
        previous_end=rva+mem;
        if (!file) { if (off) return SCL_E_RANGE; }
        else {
            if (off<payload_end || off>file_size || file>file_size-off) return SCL_E_RANGE;
            if (!metadata_only && !scl_zero(h+payload_end,off-payload_end)) return SCL_E_FORMAT;
            payload_end=off+file;
        }
    }
    if (!metadata_only && !scl_zero(h+payload_end,size-payload_end)) return SCL_E_FORMAT;
    for (unsigned int i = 0; i < scl_read32(h + SCL_H_DEP_COUNT); i++) {
        const unsigned char *d=h+scl_read32(h+SCL_H_DEPS)+i*SCL_DEPENDENCY_SIZE;
        unsigned int name=scl_read32(d);
        if (!scl_name(strings,string_size,name,1)) return SCL_E_SYMBOL;
        if (!scl_read32(d+4)) return SCL_E_VERSION;
        if (scl_read32(d+16)) return SCL_E_FORMAT;
        if (i && scl_compare(strings+scl_read32(d-SCL_DEPENDENCY_SIZE),strings+name)>=0) return SCL_E_SYMBOL;
    }
    for (unsigned int i = 0; i < scl_read32(h + SCL_H_EXPORT_COUNT); i++) {
        const unsigned char *e=h+scl_read32(h+SCL_H_EXPORTS)+i*SCL_EXPORT_SIZE;
        unsigned int name=scl_read32(e), width=scl_read32(e+8), kind=scl_read32(e+12);
        const unsigned char *s;
        if (!scl_name(strings,string_size,name,0) || !width
            || (kind!=SCL_SYMBOL_FUNCTION && kind!=SCL_SYMBOL_DATA)) return SCL_E_SYMBOL;
        if (scl_read32(e+16)) return SCL_E_FORMAT;
        if (i && scl_compare(strings+scl_read32(e-SCL_EXPORT_SIZE),strings+name)>=0) return SCL_E_SYMBOL;
        s=scl_segment(h,scl_read32(e+4),width);
        if (!s) return SCL_E_RANGE;
        if ((kind==SCL_SYMBOL_FUNCTION) != ((scl_read32(s+16)&SCL_SEG_X)!=0)) return SCL_E_SYMBOL;
    }
    for (unsigned int i = 0; i < scl_read32(h + SCL_H_IMPORT_COUNT); i++) {
        const unsigned char *p=h+scl_read32(h+SCL_H_IMPORTS)+i*SCL_IMPORT_SIZE;
        unsigned int name=scl_read32(p), dependency=scl_read32(p+4), kind=scl_read32(p+8);
        if (!scl_name(strings,string_size,name,0) || !scl_read32(p+12)
            || (kind!=SCL_SYMBOL_FUNCTION && kind!=SCL_SYMBOL_DATA)) return SCL_E_SYMBOL;
        if (dependency>=scl_read32(h+SCL_H_DEP_COUNT)) return SCL_E_RANGE;
        if (i) {
            unsigned int old=scl_read32(p-SCL_IMPORT_SIZE+4);
            if (old>dependency || (old==dependency
                && scl_compare(strings+scl_read32(p-SCL_IMPORT_SIZE),strings+name)>=0)) return SCL_E_SYMBOL;
        }
    }
    previous_end=0;
    for (unsigned int i = 0; i < scl_read32(h + SCL_H_RELOC_COUNT); i++) {
        const unsigned char *r=h+scl_read32(h+SCL_H_RELOCS)+i*SCL_RELOC_SIZE;
        unsigned int patch=scl_read32(r), type=scl_read32(r+4);
        unsigned int target_kind=scl_read32(r+8), target=scl_read32(r+12);
        if (patch<previous_end || !scl_segment(h,patch,4)) return SCL_E_RANGE;
        previous_end=patch+4;
        if (type!=SCL_RELOC_ABS32 && type!=SCL_RELOC_PC32) return SCL_E_FORMAT;
        if (target_kind==SCL_TARGET_INTERNAL) { if (!scl_segment(h,target,1)) return SCL_E_RANGE; }
        else if (target_kind==SCL_TARGET_IMPORT) { if (target>=scl_read32(h+SCL_H_IMPORT_COUNT)) return SCL_E_RANGE; }
        else return SCL_E_FORMAT;
    }
    for (unsigned int field=SCL_H_INIT;field<=SCL_H_FINI;field+=4) {
        unsigned int rva=scl_read32(h+field);
        if (rva!=SCL_NO_ENTRY) {
            const unsigned char *s=scl_segment(h,rva,1);
            if (!s || !(scl_read32(s+SCL_S_FLAGS)&SCL_SEG_X)) return SCL_E_RANGE;
        }
    }
    return 0;
}
int scl_validate(const void *bytes, unsigned int size)
{ return scl_validate_view(bytes, size, 0); }
int scl_validate_metadata(const void *bytes, unsigned int size)
{ return scl_validate_view(bytes, size, 1); }
