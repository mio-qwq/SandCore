#include "SCLDR.H"
#include "SCLPRIVATE.H"

/* 唯一的引导桥依赖 start.asm；这里属于 SCLDR 私有后端。应用只
 * 包含 SCLDR.H，后续 SC32API 使用同一后端而不复制寄存器约定。 */
#ifndef SCL_BOOT_LIBRARY
extern int sc_call(int number, int a, int b, int c, int d, int e, int f);
int ScRuntimePlatformCall(int number,int a,int b,int c,int d,int e,int f)
{ return sc_call(number,a,b,c,d,e,f); }
#else
int ScRuntimePlatformCall(int number,int a,int b,int c,int d,int e,int f)
{
    /* cdecl的保留寄存器与内核陷入约定只在这个独立引导后端处理。
     * args先完整排好，再覆盖作为临时参数指针的ECX。 */
    int args[7]={number,a,b,c,d,e,f};int result;int *p=args;
    __asm__ __volatile__(
        "push %%ebp; push %%ebx; push %%esi; push %%edi; "
        "mov 16(%%ecx),%%esi; mov 20(%%ecx),%%edi; mov 24(%%ecx),%%ebp; "
        "mov 4(%%ecx),%%ebx; mov 12(%%ecx),%%edx; mov (%%ecx),%%eax; "
        "mov 8(%%ecx),%%ecx; int $0x7c; "
        "pop %%edi; pop %%esi; pop %%ebx; pop %%ebp"
        :"=a"(result),"+c"(p)::"edx","cc","memory");
    return result;
}
#endif
#define sc_call ScRuntimePlatformCall

static int same(const char *a, const char *b)
{ while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static void *allocate(void *context, ScU32 bytes)
{ (void)context; return (void *)sc_call(0x100, (int)bytes, 0, 0, 0, 0, 0); }
static void release(void *context, void *memory)
{ (void)context; if (memory) sc_call(0x101, (int)memory, 0, 0, 0, 0, 0); }
static void *map_image(void *context, ScU32 bytes)
{
    (void)context;
    ScU32 base = (ScU32)sc_call(0x280, (int)bytes, 0, 0, 0, 0, 0);
    return base >= 0x50000000u && base < 0x54000000u ? (void *)base : 0;
}
static void unmap_image(void *context, void *base)
{ (void)context; sc_call(0x282, (int)base, 0, 0, 0, 0, 0); }
static int protect_image(void *context, void *base, ScU32 bytes, ScU32 access)
{ (void)context; return sc_call(0x281, (int)base, (int)bytes, (int)access, 0, 0, 0); }
static int system_library(const char *name)
{
    return same(name, "SC32API.SCL") || same(name, "SCCLIB.SCL") ||
        same(name, "SCLIB.SCL") || same(name, "SCUI.SCL") ||
        same(name, "SCSHELL.SCL") || same(name, "SCGFX.SCL") ||
        same(name, "SCRES.SCL") || same(name, "SCLDR.SCL");
}
static int read_file(void *context, const char *name, void **bytes, ScU32 *size)
{
    const char *root = context;
    if (!root || system_library(name)) root = "/SYS/LIB";
    char path[64]; ScU32 n = 0, i = 0, info[2];
    while (root[i]) { if (n == 62) return SC_SCL_RANGE; path[n++] = root[i++]; }
    if (!n || path[n - 1] != '/') path[n++] = '/';
    i = 0;
    while (name[i]) { if (n == 63) return SC_SCL_RANGE; path[n++] = name[i++]; }
    path[n] = 0;
    if (sc_call(0x37, (int)path, (int)info, 0, 0, 0, 0) < 0) return SC_SCL_NOT_FOUND;
    if (info[0] != 1 || info[1] < 128 || info[1] > 16u * 1024u * 1024u) return SC_SCL_RANGE;
    void *file = allocate(context, info[1]);
    if (!file) return SC_SCL_MEMORY;
    int got = sc_call(0x30, (int)path, (int)file, (int)info[1], 0, 0, 0);
    if (got < 0 || (ScU32)got != info[1]) {
        release(context, file);
        return got == -5 ? SC_SCL_NOT_FOUND : SC_SCL_IO;
    }
    *bytes = file; *size = info[1]; return 0;
}
static int invoke(void *context, void *entry, int finalizing)
{
    (void)context;
    if (finalizing) { ((void (*)(void))entry)(); return 0; }
    return ((int (*)(void))entry)();
}
int ScRuntimeDefaultPlatform(ScRuntimePlatform *platform, const char *root)
{
    if (!platform) return SC_SCL_FORMAT;
    if (root) {
        if (*root != '/') return SC_SCL_FORMAT;
        ScU32 i = 1, start = 1;
        for (;; ++i) {
            ScU32 byte = (unsigned char)root[i];
            if (!byte || byte == '/') {
                /* 只拒绝路径导航组件；点文件、连续点和UTF-8目录是
                 * SandFS合法名字，不能让引导库另行缩窄其字符集。 */
                ScU32 length = i - start;
                if ((length == 1 && root[start] == '.') ||
                    (length == 2 && root[start] == '.' && root[start + 1] == '.'))
                    return SC_SCL_FORMAT;
                if (!byte) break;
                start = i + 1;
            }
            if (i > 62 || byte == '\\' || byte == ':' || byte < 33)
                return SC_SCL_FORMAT;
        }
    }
    platform->size = sizeof(*platform); platform->context = (void *)root;
    platform->allocate = allocate; platform->release = release;
    platform->read_file = read_file; platform->release_file = release;
    platform->map_image = map_image; platform->unmap_image = unmap_image;
    platform->protect_image = protect_image; platform->invoke = invoke;
    return 0;
}
