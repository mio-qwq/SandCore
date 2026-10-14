#include "SCLDR.H"
#include "SCLFMT.H"

#ifdef __UINTPTR_TYPE__
typedef __UINTPTR_TYPE__ ScPointer;
#else
typedef unsigned int ScPointer;
#endif

typedef struct ScLoaded ScLoaded;
struct ScLoaded {
    ScLoaded *next, *previous, *init_next, *init_previous, *parent, *release_next;
    unsigned char *metadata, *image;
    ScLoaded **dependencies;
    ScU32 *imports;
    ScU32 id, external, edges, pinned, ready, initialized, retiring, borrowed, cursor, metadata_size;
    void (*application_prepare)(void);
    void *application_stack_candidate;
};
struct ScRuntime {
    ScRuntimePlatform platform;
    ScLoaded *modules, *initialized;
    ScU32 next_id, busy, loads, reuses, relocations;
    ScLoaded *invoking;
    ScU32 invoking_kind;
};

#ifdef SCL_BOOT_LIBRARY
/* 聚合ROOT的终止控制不成为普通独立loader的外部依赖。实现放在
 * 私有termination源；同一状态保护也覆盖旧九个公开入口的误重入。 */
static int app_runtime_change_allowed(ScRuntime *runtime);
static int app_runtime_destroy_allowed(ScRuntime *runtime);
static void app_module_initialized(ScRuntime *runtime,ScLoaded *module);
static void app_module_releasing(ScRuntime *runtime,ScLoaded *module);
#endif

/* 所有偏移只在完整格式验证后读取。元数据独立保留，段正文搬入
 * 映像后释放文件副本，避免每个库永久占两份完整机器码。 */
static ScU32 word(const unsigned char *p, ScU32 offset)
{ return scl_read32(p + offset); }
static void putword(unsigned char *p, ScU32 value)
{ for (ScU32 i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (i * 8)); }
static void zero(void *memory, ScU32 bytes)
{ unsigned char *p = memory; for (ScU32 i = 0; i < bytes; ++i) p[i] = 0; }
static void copy(void *to, const void *from, ScU32 bytes)
{
    unsigned char *d = to; const unsigned char *s = from;
    for (ScU32 i = 0; i < bytes; ++i) d[i] = s[i];
}
static int compare(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
static const char *string_at(ScLoaded *m, ScU32 offset)
{ return (const char *)m->metadata + word(m->metadata, 88) + offset; }
static const char *module_name(ScLoaded *m)
{ return string_at(m, word(m->metadata, 44)); }
static const unsigned char *record(ScLoaded *m, ScU32 field, ScU32 stride, ScU32 index)
{ return m->metadata + word(m->metadata, field) + stride * index; }
static ScLoaded *find_name(ScRuntime *r, const char *name)
{
    for (ScLoaded *m = r->modules; m; m = m->next)
        if (!compare(module_name(m), name)) return m;
    return 0;
}
static ScLoaded *find_id(ScRuntime *r, ScModule id)
{
    for (ScLoaded *m = r->modules; m; m = m->next)
        if (m->id == id) return m;
    return 0;
}
static int name_valid(const char *name)
{
    if (!name) return 0;
    ScU32 i = 0;
    while ((name[i] >= 'A' && name[i] <= 'Z') ||
           (name[i] >= '0' && name[i] <= '9') || name[i] == '_') {
        if (++i > 31) return 0;
    }
    return i && !compare(name + i, ".SCL");
}
static int version(ScLoaded *m, ScU32 major, ScU32 minor, ScU32 release)
{
    return word(m->metadata, 32) == major && word(m->metadata, 36) >= minor &&
           word(m->metadata, 40) >= release ? 0 : SC_SCL_VERSION;
}
static const unsigned char *find_export(ScLoaded *m, const char *name)
{
    ScU32 first = 0, count = word(m->metadata, 60);
    while (count) {
        ScU32 step = count / 2, index = first + step;
        const unsigned char *entry = record(m, 56, 20, index);
        int order = compare(string_at(m, word(entry, 0)), name);
        if (!order) return entry;
        if (order < 0) { first = index + 1; count -= step + 1; }
        else count = step;
    }
    return 0;
}
static void unlink_module(ScRuntime *r, ScLoaded *m)
{
    if (m->previous) m->previous->next = m->next;
    else r->modules = m->next;
    if (m->next) m->next->previous = m->previous;
}
static void unlink_initialized(ScRuntime *r, ScLoaded *m)
{
    if (!m->initialized) return;
    if (m->init_previous) m->init_previous->init_next = m->init_next;
    else r->initialized = m->init_next;
    if (m->init_next) m->init_next->init_previous = m->init_previous;
    m->initialized = 0;
}
static void finalize(ScRuntime *r, ScLoaded *m)
{
#ifdef SCL_BOOT_LIBRARY
    app_module_releasing(r,m);
#endif
    ScU32 entry = word(m->metadata, 100);
    if (m->initialized && entry != 0xffffffffu){
        r->invoking=m;r->invoking_kind=2;
        r->platform.invoke(r->platform.context, m->image + entry, 1);
        r->invoking=0;r->invoking_kind=0;
    }
    unlink_initialized(r, m);
}
static void release_storage(ScRuntime *r, ScLoaded *m)
{
#ifdef SCL_BOOT_LIBRARY
    app_module_releasing(r,m);
#endif
    void *context = r->platform.context;
    /* 根代码和元数据位于同一个已验证MODULE块，且当前清理函数本身
     * 就在根中执行。借用实例的页由进程EXIT回收，不能在RET前拆栈下代码。 */
    if (!m->borrowed && m->image) r->platform.unmap_image(context, m->image);
    if (!m->borrowed && m->metadata) r->platform.release(context, m->metadata);
    r->platform.release(context, m);
}
static int prepare(ScRuntime *r, const char *name, ScU32 major, ScU32 minor,
                   ScU32 release, ScLoaded **result)
{
    void *file = 0, *context = r->platform.context;
    ScU32 bytes = 0;
    int error = r->platform.read_file(context, name, &file, &bytes);
    if (error) return error;
    if (!file) return SC_SCL_IO;
    error = scl_validate(file, bytes);
    if (error) { r->platform.release_file(context, file); return error; }
    const unsigned char *source = file;
    const char *declared = (const char *)source + word(source, 88) + word(source, 44);
    if (compare(name, declared) || word(source, 32) != major ||
        word(source, 36) < minor || word(source, 40) < release) {
        r->platform.release_file(context, file); return SC_SCL_VERSION;
    }
    ScU32 deps = word(source, 84), imports = word(source, 68);
    /* 文件上限已验证；仍显式检查主机指针宽度下的工作区乘加。
     * 没有固定模块槽位或递归深度限制，失败由真实分配结果决定。 */
    ScU32 space = (ScU32)sizeof(ScLoaded);
    if (deps > (0xffffffffu - space) / sizeof(ScLoaded *)) {
        r->platform.release_file(context, file); return SC_SCL_RANGE;
    }
    space += deps * (ScU32)sizeof(ScLoaded *);
    if (imports > (0xffffffffu - space) / sizeof(ScU32)) {
        r->platform.release_file(context, file); return SC_SCL_RANGE;
    }
    space += imports * (ScU32)sizeof(ScU32);
    if (!r->next_id) { r->platform.release_file(context, file); return SC_SCL_RANGE; }
    ScLoaded *m = r->platform.allocate(context, space);
    if (!m) { r->platform.release_file(context, file); return SC_SCL_MEMORY; }
    zero(m, space);
    m->dependencies = (ScLoaded **)(m + 1);
    m->imports = (ScU32 *)(m->dependencies + deps);
    m->metadata_size = word(source, 88) + word(source, 92);
    m->metadata = r->platform.allocate(context, m->metadata_size);
    if (!m->metadata) error = SC_SCL_MEMORY;
    else {
        copy(m->metadata, source, m->metadata_size);
        ScU32 image_size = word(source, 28);
        m->image = r->platform.map_image(context, image_size);
        if (!m->image) error = SC_SCL_MEMORY;
        else if ((ScPointer)m->image > 0xffffffffu - image_size) error = SC_SCL_RANGE;
        else {
            zero(m->image, image_size);
            for (ScU32 i = 0; i < word(source, 52); ++i) {
                const unsigned char *s = source + word(source, 48) + i * 24;
                copy(m->image + word(s, 0), source + word(s, 8), word(s, 12));
            }
        }
    }
    r->platform.release_file(context, file);
    if (error) { release_storage(r, m); return error; }
    m->id = r->next_id++;
    m->next = r->modules;
    if (m->next) m->next->previous = m;
    r->modules = m;
    *result = m;
    return 0;
}
static int link_module(ScRuntime *r, ScLoaded *m)
{
    for (ScU32 i = 0; i < word(m->metadata, 68); ++i) {
        const unsigned char *entry = record(m, 64, 16, i);
        ScLoaded *dependency = m->dependencies[word(entry, 4)];
        const unsigned char *symbol = find_export(dependency, string_at(m, word(entry, 0)));
        if (!symbol || word(symbol, 12) != word(entry, 8) ||
            word(symbol, 8) < word(entry, 12)) return SC_SCL_SYMBOL;
        m->imports[i] = (ScU32)(ScPointer)dependency->image + word(symbol, 4);
    }
    for (ScU32 i = 0; i < word(m->metadata, 76); ++i) {
        const unsigned char *entry = record(m, 72, 20, i);
        ScU32 target = word(entry, 8) ? m->imports[word(entry, 12)] :
            (ScU32)(ScPointer)m->image + word(entry, 12);
        ScU32 value = target + word(entry, 16);
        if (word(entry, 4) == 2) value -= (ScU32)(ScPointer)m->image + word(entry, 0);
        putword(m->image + word(entry, 0), value);
    }
    /* 先把整个保留映像变成只读（包括段间的零填充），再开放显式
     * data/bss 段。非PAE分页没有NX，不能把这些位称为完整W^X。 */
    if (r->platform.protect_image(r->platform.context, m->image, word(m->metadata, 28), 1))
        return SC_SCL_IO;
    for (ScU32 i = 0; i < word(m->metadata, 52); ++i) {
        const unsigned char *s = record(m, 48, 24, i);
        if ((word(s, 16) & 2) && r->platform.protect_image(r->platform.context,
            m->image + word(s, 0), word(s, 4), 3)) return SC_SCL_IO;
    }
    ScU32 entry = word(m->metadata, 96);
    if (entry != 0xffffffffu){
        r->invoking=m;r->invoking_kind=1;
        int result=r->platform.invoke(r->platform.context,m->image+entry,0);
        r->invoking=0;r->invoking_kind=0;
        if(result)return SC_SCL_INIT;
    }
    m->initialized = 1;
    m->init_next = r->initialized;
    if (m->init_next) m->init_next->init_previous = m;
    r->initialized = m;
    m->ready = 1;
#ifdef SCL_BOOT_LIBRARY
    app_module_initialized(r,m);
#endif
    return 0;
}
static void rollback(ScRuntime *r, ScLoaded *checkpoint, ScLoaded *init_checkpoint)
{
    /* 初始化链与建立链不同：先按初始化的逆序撤销，再移除所有新
     * 结点。不能按地址/创建顺序清理，否则依赖库可能先被释放。 */
    while (r->initialized != init_checkpoint) finalize(r, r->initialized);
    for (ScLoaded *m = r->modules; m != checkpoint; m = m->next)
        for (ScU32 i = 0; i < m->cursor; ++i)
            if (m->dependencies[i]) --m->dependencies[i]->edges;
    while (r->modules != checkpoint) {
        ScLoaded *m = r->modules;
        unlink_module(r, m); release_storage(r, m);
    }
}

ScRuntime *ScRuntimeCreate(const ScRuntimePlatform *platform)
{
    if (!platform || platform->size != sizeof(*platform) || !platform->allocate ||
        !platform->release || !platform->read_file || !platform->release_file ||
        !platform->map_image || !platform->unmap_image || !platform->protect_image ||
        !platform->invoke) return 0;
    ScRuntime *r = platform->allocate(platform->context, sizeof(ScRuntime));
    if (!r) return 0;
    zero(r, sizeof(*r)); r->platform = *platform; r->next_id = 1;
    return r;
}
int ScRuntimeOpen(ScRuntime *r, const char *name, ScU32 major,
                  ScU32 minor, ScU32 release, ScModule *result)
{
    if (!r || !result || !name_valid(name) || !major) return SC_SCL_FORMAT;
#ifdef SCL_BOOT_LIBRARY
    if(!app_runtime_change_allowed(r))return SC_SCL_BUSY;
#endif
    if (r->busy) return SC_SCL_BUSY;
    r->busy = 1;
    ScLoaded *root = find_name(r, name);
    int error = 0;
    if (root) {
        error = version(root, major, minor, release);
        if (!error && root->external == 0xffffffffu) error = SC_SCL_RANGE;
        if (!error) { ++root->external; *result = root->id; ++r->reuses; }
        r->busy = 0; return error;
    }
    ScLoaded *checkpoint = r->modules, *init_checkpoint = r->initialized;
    error = prepare(r, name, major, minor, release, &root);
    ScLoaded *current = root;
    /* 用结点内游标及父链执行DFS；恶意长依赖链不会耗尽128KiB
     * 用户栈。LOADING结点只沿当前祖先链存在，重复命中即依赖环。 */
    while (!error && current) {
        if (current->cursor < word(current->metadata, 84)) {
            ScU32 index = current->cursor;
            const unsigned char *dep = record(current, 80, 20, index);
            const char *dep_name = string_at(current, word(dep, 0));
            ScLoaded *child = find_name(r, dep_name);
            if (child && !child->ready) { error = SC_SCL_CYCLE; break; }
            if (child) error = version(child, word(dep, 4), word(dep, 8), word(dep, 12));
            else {
                error = prepare(r, dep_name, word(dep, 4), word(dep, 8), word(dep, 12), &child);
                if (!error) child->parent = current;
            }
            if (error) break;
            if (child->edges == 0xffffffffu) { error = SC_SCL_RANGE; break; }
            ++child->edges;
            current->dependencies[index] = child;
            ++current->cursor;
            if (!child->ready) current = child;
        } else {
            error = link_module(r, current);
            if (!error) current = current->parent;
        }
    }
    if (error) rollback(r, checkpoint, init_checkpoint);
    else {
        root->external = 1; *result = root->id;
        for (ScLoaded *m = r->modules; m != checkpoint; m = m->next) {
            m->parent = 0; ++r->loads; r->relocations += word(m->metadata, 76);
        }
    }
    r->busy = 0; return error;
}
int ScRuntimeClose(ScRuntime *r, ScModule module)
{
    if (!r) return SC_SCL_HANDLE;
#ifdef SCL_BOOT_LIBRARY
    if(!app_runtime_change_allowed(r))return SC_SCL_BUSY;
#endif
    if (r->busy) return SC_SCL_BUSY;
    ScLoaded *m = find_id(r, module);
    if (!m || !m->external) return SC_SCL_HANDLE;
    --m->external;
    if (m->external || m->edges || m->pinned) return SC_SCL_RESIDENT;
    r->busy = 1;
    ScLoaded *queue = m, *retired = 0;
    while (queue) {
        m = queue; queue = m->release_next;
        m->retiring = 1;
        m->release_next = retired; retired = m;
        /* 只有引用数真正降到零才入队，同一依赖被多个兄弟引用时
         * 不会重复入队。释放深图不递归，也没有全表反复寻找叶子。 */
        for (ScU32 i = 0; i < word(m->metadata, 84); ++i) {
            ScLoaded *dep = m->dependencies[i];
            --dep->edges;
            if (!dep->edges && !dep->external && !dep->pinned) {
                dep->release_next = queue; queue = dep;
            }
        }
    }
    /* 先求出失去最后引用的完整闭包，再沿真实初始化链逆序执行。
     * 依赖可能先被另一个Open初始化；只按当前根的表顺序释放，
     * 会把这种共享DAG的fini顺序搞反。所有映像保留到回调结束。 */
    for (ScLoaded *item = r->initialized, *next; item; item = next) {
        next = item->init_next;
        if (item->retiring) finalize(r, item);
    }
    while (retired) {
        m = retired; retired = m->release_next;
        unlink_module(r, m); release_storage(r, m);
    }
    r->busy = 0; return SC_SCL_OK;
}
int ScRuntimeSymbol(ScRuntime *r, ScModule module, const char *name,
                    ScU32 kind, void **address)
{ return ScRuntimeResolve(r, module, name, kind, 1, address); }
int ScRuntimeResolve(ScRuntime *r, ScModule module, const char *name,
                     ScU32 kind, ScU32 minimum, void **address)
{
    if (!r || !name || !address || !minimum || (kind != 1 && kind != 2)) return SC_SCL_FORMAT;
    if (r->busy) return SC_SCL_BUSY;
    ScLoaded *m = find_id(r, module);
    if (!m || !m->external) return SC_SCL_HANDLE;
    const unsigned char *entry = find_export(m, name);
    if (!entry || word(entry, 12) != kind || word(entry, 8) < minimum) return SC_SCL_SYMBOL;
    m->pinned = 1;
    *address = m->image + word(entry, 4);
    return 0;
}
int ScRuntimeAdoptBootstrap(ScRuntime *r, void *image, const void *metadata, ScU32 size)
{
    if (!r || !image || !metadata || size < SCL_HEADER_SIZE) return SC_SCL_FORMAT;
    if (r->busy) return SC_SCL_BUSY;
    const unsigned char *h = metadata;
    /* 0x283先验完整磁盘文件。借用入口仍核验全部元数据边界，不能
     * 只因名字叫SCLDR就信任导出偏移；这里不读取不存在的payload。 */
    int error = scl_validate_metadata(metadata, size);
    if (error) return error;
    if (word(h, 32) != 1 || word(h, 68) || word(h, 84) ||
        word(h, 96) != 0xffffffffu || word(h, 100) != 0xffffffffu)
        return SC_SCL_FORMAT;
    const char *name = (const char *)h + word(h, 88) + word(h, 44);
    if (!name_valid(name) || compare(name, "SCLDR.SCL") || find_name(r, name) || !r->next_id)
        return SC_SCL_FORMAT;
    ScLoaded *m = r->platform.allocate(r->platform.context, sizeof(*m));
    if (!m) return SC_SCL_MEMORY;
    zero(m, sizeof(*m));
    m->metadata = (unsigned char *)metadata; m->metadata_size = size; m->image = image;
    m->id = r->next_id++; m->borrowed = m->pinned = m->ready = m->initialized = 1;
    m->next = r->modules; if (m->next) m->next->previous = m; r->modules = m;
    m->init_next = r->initialized;
    if (m->init_next) m->init_next->init_previous = m;
    r->initialized = m; ++r->loads;
    return 0;
}
int ScRuntimeQuery(ScRuntime *r, ScRuntimeStats *stats)
{
    if (!r || !stats || stats->size != sizeof(*stats)) return SC_SCL_FORMAT;
    if (r->busy) return SC_SCL_BUSY;
    ScRuntimeStats out; zero(&out, sizeof(out)); out.size = sizeof(out); out.version = 1;
    for (ScLoaded *m = r->modules; m; m = m->next) {
        ++out.modules; out.image_bytes += word(m->metadata, 28);
        out.metadata_bytes += m->metadata_size; out.pinned_modules += m->pinned;
    }
    out.loads = r->loads; out.reuses = r->reuses; out.relocations = r->relocations;
    *stats = out; return 0;
}
int ScRuntimeDestroy(ScRuntime *r)
{
    if (!r) return SC_SCL_HANDLE;
#ifdef SCL_BOOT_LIBRARY
    if(!app_runtime_destroy_allowed(r))return SC_SCL_BUSY;
#endif
    if (r->busy) return SC_SCL_BUSY;
    r->busy = 1;
    while (r->initialized) finalize(r, r->initialized);
    while (r->modules) {
        ScLoaded *m = r->modules;
        unlink_module(r, m); release_storage(r, m);
    }
    ScRuntimePlatform platform = r->platform;
    platform.release(platform.context, r);
    return 0;
}
