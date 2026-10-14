#include "SCLDR.H"

/* 唯一的引导桥依赖 start.asm；这里属于 SCLDR 私有后端。应用只
 * 包含 SCLDR.H，后续 SC32API 使用同一后端而不复制寄存器约定。 */
extern int sc_call(int number, int a, int b, int c, int d, int e, int f);

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
        ScU32 i = 1;
        for (; root[i]; ++i) {
            if (i > 26 || root[i] == '\\' || root[i] == ':' || root[i] < 32)
                return SC_SCL_FORMAT;
            if (root[i] == '.' && (root[i - 1] == '/' || root[i - 1] == '.'))
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
