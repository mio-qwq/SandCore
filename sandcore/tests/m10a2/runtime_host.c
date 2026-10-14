#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "SCLDR.H"
#include "SCLFMT.H"

/* 本夹具只链接真实装载器，不执行 i386 机器码。低地址 mmap 使 64 位
 * 宿主上的重定位仍使用真实 32 位地址；mprotect 验证段保护的调用顺序。
 * 宿主记录表的容量只限定此四库夹具，不能解释为运行时模块数量限制。 */
typedef struct {
    void *base;
    size_t bytes;
    int kind, live, protections;
    char name[40];
} Record;
typedef struct {
    const char *directory;
    Record records[128];
    unsigned record_count, allocations, reads, maps, protects;
    unsigned fail_allocation, fail_read, fail_map, fail_protect;
    unsigned live_heap, live_files, live_maps, tracking_errors;
    size_t live_bytes, peak_bytes;
    unsigned checks, failures;
    char preparing[40], events[2048];
    const char *fail_init;
    ScRuntime *runtime;
    int check_busy;
} Context;

enum { HEAP = 1, FILE_BYTES = 2, IMAGE = 3 };
static unsigned case_count, failed_cases, total_checks;

static void check(Context *c, int condition, const char *expression, int line)
{
    ++c->checks;
    if (!condition) {
        ++c->failures;
        fprintf(stderr, "runtime_host.c:%d: %s\n", line, expression);
    }
}
#define EXPECT(c, expression) check((c), !!(expression), #expression, __LINE__)
#define REQUIRE(c, expression) do { \
    int satisfied = !!(expression); check((c), satisfied, #expression, __LINE__); \
    if (!satisfied) goto done; \
} while (0)

static void report(Context *c, const char *name)
{
    ++case_count;
    total_checks += c->checks;
    if (c->failures) ++failed_cases;
    printf("{\"case\":\"%s\",\"passed\":%s,\"checks\":%u,"
           "\"failures\":%u,\"peak_tracked_bytes\":%zu}\n",
           name, c->failures ? "false" : "true", c->checks, c->failures, c->peak_bytes);
}

static Record *track(Context *c, void *base, size_t bytes, int kind)
{
    if (!base || c->record_count == sizeof(c->records) / sizeof(c->records[0])) {
        fprintf(stderr, "host fixture tracking exhausted\n"); exit(2);
    }
    Record *r = &c->records[c->record_count++];
    *r = (Record){.base = base, .bytes = bytes, .kind = kind, .live = 1};
    if (kind == IMAGE) {
        snprintf(r->name, sizeof(r->name), "%s", c->preparing);
        ++c->live_maps;
    } else if (kind == FILE_BYTES) ++c->live_files;
    else ++c->live_heap;
    c->live_bytes += bytes;
    if (c->live_bytes > c->peak_bytes) c->peak_bytes = c->live_bytes;
    return r;
}

static Record *find_record(Context *c, void *base, int kind)
{
    for (unsigned i = 0; i < c->record_count; ++i)
        if (c->records[i].live && c->records[i].base == base && c->records[i].kind == kind)
            return &c->records[i];
    ++c->tracking_errors;
    return NULL;
}

static void untrack(Context *c, Record *r)
{
    r->live = 0;
    c->live_bytes -= r->bytes;
    if (r->kind == IMAGE) --c->live_maps;
    else if (r->kind == FILE_BYTES) --c->live_files;
    else --c->live_heap;
}

static void *allocate(void *opaque, ScU32 bytes)
{
    Context *c = opaque;
    if (++c->allocations == c->fail_allocation) return NULL;
    void *base = malloc(bytes);
    if (!base) return NULL;
    track(c, base, bytes, HEAP);
    return base;
}

static void release(void *opaque, void *base)
{
    Context *c = opaque;
    Record *r = find_record(c, base, HEAP);
    if (!r) return;
    untrack(c, r); free(base);
}

static int read_file(void *opaque, const char *name, void **bytes, ScU32 *size)
{
    Context *c = opaque;
    *bytes = NULL; *size = 0;
    if (++c->reads == c->fail_read) return SC_SCL_MEMORY;
    char path[1024];
    if (snprintf(path, sizeof(path), "%s/%s", c->directory, name) >= (int)sizeof(path))
        return SC_SCL_RANGE;
    FILE *file = fopen(path, "rb");
    if (!file) return SC_SCL_NOT_FOUND;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return SC_SCL_IO; }
    long length = ftell(file);
    if (length < 0 || (unsigned long)length > UINT32_MAX || fseek(file, 0, SEEK_SET)) {
        fclose(file); return SC_SCL_IO;
    }
    void *data = malloc((size_t)length);
    if (!data) { fclose(file); return SC_SCL_MEMORY; }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data); fclose(file); return SC_SCL_IO;
    }
    fclose(file);
    track(c, data, (size_t)length, FILE_BYTES);
    snprintf(c->preparing, sizeof(c->preparing), "%s", name);
    *bytes = data; *size = (ScU32)length;
    return SC_SCL_OK;
}

static void release_file(void *opaque, void *base)
{
    Context *c = opaque;
    Record *r = find_record(c, base, FILE_BYTES);
    if (!r) return;
    untrack(c, r); free(base);
}

static void *map_image(void *opaque, ScU32 bytes)
{
    Context *c = opaque;
    if (++c->maps == c->fail_map) return NULL;
    void *base = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (base == MAP_FAILED) return NULL;
    if ((uintptr_t)base > UINT32_MAX - bytes) { munmap(base, bytes); return NULL; }
    track(c, base, bytes, IMAGE);
    return base;
}

static void unmap_image(void *opaque, void *base)
{
    Context *c = opaque;
    Record *r = find_record(c, base, IMAGE);
    if (!r) return;
    if (munmap(base, r->bytes)) ++c->tracking_errors;
    untrack(c, r);
}

static Record *containing_image(Context *c, const void *base, size_t bytes)
{
    uintptr_t address = (uintptr_t)base;
    for (unsigned i = 0; i < c->record_count; ++i) {
        Record *r = &c->records[i];
        if (r->live && r->kind == IMAGE && address >= (uintptr_t)r->base &&
            address - (uintptr_t)r->base <= r->bytes &&
            bytes <= r->bytes - (address - (uintptr_t)r->base)) return r;
    }
    return NULL;
}

static int protect_image(void *opaque, void *base, ScU32 bytes, ScU32 access)
{
    Context *c = opaque;
    ++c->protects;
    Record *r = containing_image(c, base, bytes);
    if (!r || (uintptr_t)base % 4096 || bytes % 4096 || (access != 1 && access != 3)) {
        ++c->tracking_errors; return SC_SCL_IO;
    }
    if (c->protects == c->fail_protect) return SC_SCL_IO;
    /* 第一调用保护整个映像，包括空洞；第二调用只恢复数据页写权限。 */
    EXPECT(c, (r->protections == 0 && base == r->base && bytes == r->bytes && access == 1) ||
              (r->protections == 1 && base == (unsigned char *)r->base + 4096 &&
               bytes == 4096 && access == 3));
    ++r->protections;
    return mprotect(base, bytes, PROT_READ | (access & 2 ? PROT_WRITE : 0)) ? SC_SCL_IO : 0;
}

static void expect_busy(Context *c)
{
    ScRuntimeStats stats = {.size = sizeof(stats)};
    ScModule module = 0x76543210u;
    void *address = (void *)(uintptr_t)0x1234;
    EXPECT(c, ScRuntimeQuery(c->runtime, &stats) == SC_SCL_BUSY);
    EXPECT(c, ScRuntimeOpen(c->runtime, "BASE.SCL", 1, 0, 0, &module) == SC_SCL_BUSY);
    EXPECT(c, module == 0x76543210u);
    EXPECT(c, ScRuntimeClose(c->runtime, 1) == SC_SCL_BUSY);
    EXPECT(c, ScRuntimeSymbol(c->runtime, 1, "base_value", 2, &address) == SC_SCL_BUSY);
    EXPECT(c, address == (void *)(uintptr_t)0x1234);
    EXPECT(c, ScRuntimeDestroy(c->runtime) == SC_SCL_BUSY);
}

static int invoke(void *opaque, void *entry, int finalizing)
{
    Context *c = opaque;
    Record *r = containing_image(c, entry, 1);
    if (!r) { ++c->tracking_errors; return 1; }
    EXPECT(c, entry == (unsigned char *)r->base + (finalizing ? 0x80 : 0x40));
    EXPECT(c, r->protections == 2);
    size_t offset = strlen(c->events);
    int written = snprintf(c->events + offset, sizeof(c->events) - offset,
                           "%s:%s;", finalizing ? "fini" : "init", r->name);
    if (written < 0 || (size_t)written >= sizeof(c->events) - offset) exit(2);
    if (c->check_busy) expect_busy(c);
    return !finalizing && c->fail_init && !strcmp(c->fail_init, r->name);
}

static ScRuntime *create(Context *c, const char *directory)
{
    c->directory = directory;
    ScRuntimePlatform platform = {
        .size = sizeof(platform), .context = c, .allocate = allocate, .release = release,
        .read_file = read_file, .release_file = release_file, .map_image = map_image,
        .unmap_image = unmap_image, .protect_image = protect_image, .invoke = invoke
    };
    c->runtime = ScRuntimeCreate(&platform);
    return c->runtime;
}

static ScRuntimeStats query(Context *c)
{
    ScRuntimeStats stats = {.size = sizeof(stats)};
    EXPECT(c, ScRuntimeQuery(c->runtime, &stats) == SC_SCL_OK);
    EXPECT(c, stats.version == 1);
    return stats;
}

static Record *named_image(Context *c, const char *name)
{
    for (unsigned i = 0; i < c->record_count; ++i)
        if (c->records[i].kind == IMAGE && c->records[i].live && !strcmp(c->records[i].name, name))
            return &c->records[i];
    return NULL;
}

static uint32_t at(Context *c, const char *name, unsigned offset)
{
    Record *r = named_image(c, name);
    EXPECT(c, r && offset + 4 <= r->bytes);
    return r ? scl_read32((unsigned char *)r->base + offset) : 0;
}

static uint32_t base_of(Context *c, const char *name)
{
    Record *r = named_image(c, name);
    EXPECT(c, r != NULL);
    return r ? (uint32_t)(uintptr_t)r->base : 0;
}

static void check_relocations(Context *c)
{
    uint32_t root = base_of(c, "ROOT.SCL"), mid = base_of(c, "MID.SCL");
    uint32_t side = base_of(c, "SIDE.SCL"), base = base_of(c, "BASE.SCL");
    /* 预期值按夹具的固定语义手工计算，不复用被测代码或 Python 重定位模型。 */
    EXPECT(c, at(c, "ROOT.SCL", 0x10) == root + 0x1100u + 5u);
    EXPECT(c, at(c, "ROOT.SCL", 0x14) == 0x20u - 4u - 0x14u);
    EXPECT(c, at(c, "ROOT.SCL", 0x18) == mid + 0x20u + 7u);
    EXPECT(c, at(c, "ROOT.SCL", 0x1c) == mid + 0x20u - 4u - root - 0x1cu);
    EXPECT(c, at(c, "ROOT.SCL", 0x1000) == mid + 0x1100u + 12u);
    EXPECT(c, at(c, "ROOT.SCL", 0x1004) == side + 0x1100u - 8u - root - 0x1004u);
    EXPECT(c, at(c, "MID.SCL", 0x10) == base + 0x20u - 4u - mid - 0x10u);
    EXPECT(c, at(c, "MID.SCL", 0x1000) == base + 0x1100u);
    EXPECT(c, at(c, "SIDE.SCL", 0x1000) == base + 0x1100u);
    EXPECT(c, at(c, "BASE.SCL", 0x1100) == 0x11223344u);
    EXPECT(c, at(c, "MID.SCL", 0x1100) == 0x33445566u);
    EXPECT(c, at(c, "SIDE.SCL", 0x1100) == 0x778899aau);
    EXPECT(c, at(c, "ROOT.SCL", 0x1100) == 0xaabbccddu);
    for (unsigned i = 0; i < c->record_count; ++i) {
        Record *r = &c->records[i];
        if (!r->live || r->kind != IMAGE) continue;
        int zero = 1;
        for (unsigned n = 0x1104; n < 0x2000; ++n)
            if (((unsigned char *)r->base)[n]) zero = 0;
        EXPECT(c, zero);
    }
}

static void expect_clean(Context *c)
{
    EXPECT(c, c->live_heap == 0 && c->live_files == 0 && c->live_maps == 0);
    EXPECT(c, c->live_bytes == 0 && c->tracking_errors == 0);
}

static void destroy(Context *c)
{
    if (c->runtime) {
        EXPECT(c, ScRuntimeDestroy(c->runtime) == SC_SCL_OK);
        c->runtime = NULL;
    }
    expect_clean(c);
}

static const char *graph_init = "init:BASE.SCL;init:MID.SCL;init:SIDE.SCL;init:ROOT.SCL;";
static const char *graph_destroy = "init:BASE.SCL;init:MID.SCL;init:SIDE.SCL;init:ROOT.SCL;"
                                  "fini:ROOT.SCL;fini:SIDE.SCL;fini:MID.SCL;fini:BASE.SCL;";

static void test_graph(const char *directory)
{
    Context c = {.check_busy = 1}; ScModule root = 0, again = 0, dependency = 0;
    REQUIRE(&c, create(&c, directory) != NULL);
    int result = ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 2, 3, &root);
    REQUIRE(&c, result == SC_SCL_OK);
    EXPECT(&c, !strcmp(c.events, graph_init));
    ScRuntimeStats stats = query(&c);
    EXPECT(&c, stats.modules == 4 && stats.loads == 4 && stats.relocations == 9);
    EXPECT(&c, stats.image_bytes == 4 * 8192 && stats.pinned_modules == 0);
    EXPECT(&c, c.live_files == 0 && c.live_maps == 4 && c.protects == 8);
    check_relocations(&c);
    EXPECT(&c, ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 0, 0, &again) == SC_SCL_OK);
    EXPECT(&c, root == again && c.maps == 4 && c.reads == 4);
    EXPECT(&c, query(&c).reuses == 1);
    EXPECT(&c, ScRuntimeClose(c.runtime, root) == SC_SCL_RESIDENT);
    EXPECT(&c, !strcmp(c.events, graph_init));
    EXPECT(&c, ScRuntimeOpen(c.runtime, "BASE.SCL", 1, 0, 0, &dependency) == SC_SCL_OK);
    EXPECT(&c, ScRuntimeClose(c.runtime, dependency) == SC_SCL_RESIDENT);
    /* 已释放的显式句柄不拥有依赖边，第二次 Close 必须拒绝，不能偷减依赖引用。 */
    EXPECT(&c, ScRuntimeClose(c.runtime, dependency) == SC_SCL_HANDLE);
    EXPECT(&c, query(&c).modules == 4);
    EXPECT(&c, ScRuntimeClose(c.runtime, root) == SC_SCL_OK);
    EXPECT(&c, !strcmp(c.events, graph_destroy));
    EXPECT(&c, query(&c).modules == 0 && c.live_maps == 0);
    EXPECT(&c, ScRuntimeClose(c.runtime, root) == SC_SCL_HANDLE);
done:
    destroy(&c); report(&c, "dependency_graph_relocation_reuse_close");
}

static void test_preloaded_siblings(const char *directory)
{
    Context c = {0}; ScModule side = 0, mid = 0, root = 0;
    REQUIRE(&c, create(&c, directory) != NULL);
    int result = ScRuntimeOpen(c.runtime, "SIDE.SCL", 1, 0, 0, &side);
    REQUIRE(&c, result == SC_SCL_OK);
    result = ScRuntimeOpen(c.runtime, "MID.SCL", 1, 0, 0, &mid);
    REQUIRE(&c, result == SC_SCL_OK);
    result = ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 0, 0, &root);
    REQUIRE(&c, result == SC_SCL_OK);
    EXPECT(&c, !strcmp(c.events, "init:BASE.SCL;init:SIDE.SCL;init:MID.SCL;init:ROOT.SCL;"));
    EXPECT(&c, ScRuntimeClose(c.runtime, side) == SC_SCL_RESIDENT);
    EXPECT(&c, ScRuntimeClose(c.runtime, mid) == SC_SCL_RESIDENT);
    /* C 先于 B 初始化，A 的依赖表却先 B 后 C。逆依赖表遍历不等于
     * 实际初始化逆序；此处必须由全局初始化链得到 A、B、C。 */
    EXPECT(&c, ScRuntimeClose(c.runtime, root) == SC_SCL_OK);
    EXPECT(&c, !strcmp(c.events, "init:BASE.SCL;init:SIDE.SCL;init:MID.SCL;init:ROOT.SCL;"
                               "fini:ROOT.SCL;fini:MID.SCL;fini:SIDE.SCL;fini:BASE.SCL;"));
    EXPECT(&c, query(&c).modules == 0 && c.live_maps == 0);
done:
    destroy(&c); report(&c, "preloaded_siblings_global_reverse_fini");
}

static void test_version(const char *directory)
{
    Context c = {0}; ScModule root = 0, rejected = 0x76543210u;
    REQUIRE(&c, create(&c, directory) != NULL);
    EXPECT(&c, ScRuntimeOpen(c.runtime, "ROOT.SCL", 2, 0, 0, &rejected) == SC_SCL_VERSION);
    EXPECT(&c, ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 3, 0, &rejected) == SC_SCL_VERSION);
    EXPECT(&c, ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 0, 4, &rejected) == SC_SCL_VERSION);
    EXPECT(&c, rejected == 0x76543210u && c.maps == 0 && c.events[0] == 0);
    int result = ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 2, 3, &root);
    REQUIRE(&c, result == SC_SCL_OK);
    unsigned reads = c.reads;
    EXPECT(&c, ScRuntimeOpen(c.runtime, "ROOT.SCL", 2, 0, 0, &rejected) == SC_SCL_VERSION);
    EXPECT(&c, ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 3, 0, &rejected) == SC_SCL_VERSION);
    EXPECT(&c, ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 0, 4, &rejected) == SC_SCL_VERSION);
    EXPECT(&c, rejected == 0x76543210u && c.reads == reads && query(&c).reuses == 0);
    EXPECT(&c, ScRuntimeClose(c.runtime, root) == SC_SCL_OK);
    EXPECT(&c, query(&c).modules == 0);
done:
    destroy(&c); report(&c, "new_and_resident_version_rejection");
}

static void test_load_errors(const char *directory)
{
    static const struct { const char *name; int error; const char *events; } cases[] = {
        {"ABSENT.SCL", SC_SCL_NOT_FOUND, ""},
        {"MISSINGLIB.SCL", SC_SCL_NOT_FOUND, ""},
        {"BADSYMBOL.SCL", SC_SCL_SYMBOL, "init:BASE.SCL;fini:BASE.SCL;"},
        {"WRONGKIND.SCL", SC_SCL_SYMBOL, "init:BASE.SCL;fini:BASE.SCL;"},
        {"TOOWIDE.SCL", SC_SCL_SYMBOL, "init:BASE.SCL;fini:BASE.SCL;"},
        {"BADVERSION.SCL", SC_SCL_VERSION, ""},
        {"CYCLEA.SCL", SC_SCL_CYCLE, ""},
        {"FORMAT.SCL", SC_SCL_FORMAT, ""},
        {"RANGE.SCL", SC_SCL_RANGE, ""},
        {"MISNAMED.SCL", SC_SCL_VERSION, ""}
    };
    Context c = {0};
    REQUIRE(&c, create(&c, directory) != NULL);
    size_t baseline = c.live_bytes;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        ScModule module = 0x76543210u;
        c.events[0] = 0;
        EXPECT(&c, ScRuntimeOpen(c.runtime, cases[i].name, 1, 0, 0, &module) == cases[i].error);
        EXPECT(&c, module == 0x76543210u && !strcmp(c.events, cases[i].events));
        ScRuntimeStats stats = query(&c);
        EXPECT(&c, stats.modules == 0 && stats.loads == 0 && stats.relocations == 0);
        EXPECT(&c, c.live_heap == 1 && c.live_files == 0 && c.live_maps == 0);
        EXPECT(&c, c.live_bytes == baseline && c.tracking_errors == 0);
    }
done:
    destroy(&c); report(&c, "missing_symbol_kind_size_version_cycle_format_rollback");
}

static void test_init_failure(const char *directory)
{
    Context c = {.fail_init = "ROOT.SCL"}; ScModule module = 0x76543210u, base = 0;
    REQUIRE(&c, create(&c, directory) != NULL);
    size_t baseline = c.live_bytes;
    EXPECT(&c, ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 0, 0, &module) == SC_SCL_INIT);
    EXPECT(&c, module == 0x76543210u);
    EXPECT(&c, !strcmp(c.events, "init:BASE.SCL;init:MID.SCL;init:SIDE.SCL;init:ROOT.SCL;"
                               "fini:SIDE.SCL;fini:MID.SCL;fini:BASE.SCL;"));
    EXPECT(&c, query(&c).modules == 0 && c.live_bytes == baseline && c.live_maps == 0);
    /* 已存在的模块不是本次事务资源；失败既不能析构它，也不能残留依赖边。 */
    c.events[0] = 0;
    int result = ScRuntimeOpen(c.runtime, "BASE.SCL", 1, 0, 0, &base);
    REQUIRE(&c, result == SC_SCL_OK);
    baseline = c.live_bytes;
    EXPECT(&c, ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 0, 0, &module) == SC_SCL_INIT);
    EXPECT(&c, !strcmp(c.events, "init:BASE.SCL;init:MID.SCL;init:SIDE.SCL;init:ROOT.SCL;"
                               "fini:SIDE.SCL;fini:MID.SCL;"));
    EXPECT(&c, query(&c).modules == 1 && c.live_bytes == baseline && c.live_maps == 1);
    EXPECT(&c, at(&c, "BASE.SCL", 0x1100) == 0x11223344u);
    EXPECT(&c, ScRuntimeClose(c.runtime, base) == SC_SCL_OK);
    EXPECT(&c, query(&c).modules == 0);
done:
    destroy(&c); report(&c, "init_failure_reverse_fini_existing_dependency_preserved");
}

static void test_failpoints(const char *directory)
{
    static const struct {
        const char *name; unsigned allocation, read, map, protect; int error; const char *events;
    } cases[] = {
        {"root_workspace_oom", 2, 0, 0, 0, SC_SCL_MEMORY, ""},
        {"root_metadata_oom", 3, 0, 0, 0, SC_SCL_MEMORY, ""},
        {"late_workspace_oom", 8, 0, 0, 0, SC_SCL_MEMORY,
         "init:BASE.SCL;init:MID.SCL;fini:MID.SCL;fini:BASE.SCL;"},
        {"late_metadata_oom", 9, 0, 0, 0, SC_SCL_MEMORY,
         "init:BASE.SCL;init:MID.SCL;fini:MID.SCL;fini:BASE.SCL;"},
        {"late_file_buffer_oom", 0, 4, 0, 0, SC_SCL_MEMORY,
         "init:BASE.SCL;init:MID.SCL;fini:MID.SCL;fini:BASE.SCL;"},
        {"root_image_oom", 0, 0, 1, 0, SC_SCL_MEMORY, ""},
        {"late_image_oom", 0, 0, 4, 0, SC_SCL_MEMORY,
         "init:BASE.SCL;init:MID.SCL;fini:MID.SCL;fini:BASE.SCL;"},
        {"base_whole_protect_failure", 0, 0, 0, 1, SC_SCL_IO, ""},
        {"base_data_protect_failure", 0, 0, 0, 2, SC_SCL_IO, ""},
        {"root_whole_protect_failure", 0, 0, 0, 7, SC_SCL_IO,
         "init:BASE.SCL;init:MID.SCL;init:SIDE.SCL;fini:SIDE.SCL;fini:MID.SCL;fini:BASE.SCL;"},
        {"root_data_protect_failure", 0, 0, 0, 8, SC_SCL_IO,
         "init:BASE.SCL;init:MID.SCL;init:SIDE.SCL;fini:SIDE.SCL;fini:MID.SCL;fini:BASE.SCL;"}
    };
    Context create_failure = {.fail_allocation = 1};
    EXPECT(&create_failure, create(&create_failure, directory) == NULL);
    expect_clean(&create_failure); report(&create_failure, "runtime_create_oom");
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Context c = {.fail_allocation = cases[i].allocation, .fail_read = cases[i].read,
                     .fail_map = cases[i].map, .fail_protect = cases[i].protect};
        ScModule module = 0x76543210u;
        REQUIRE(&c, create(&c, directory) != NULL);
        size_t baseline = c.live_bytes;
        EXPECT(&c, ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 0, 0, &module) == cases[i].error);
        EXPECT(&c, module == 0x76543210u && !strcmp(c.events, cases[i].events));
        EXPECT(&c, query(&c).modules == 0 && c.live_bytes == baseline && c.live_maps == 0);
        EXPECT(&c, c.live_heap == 1 && c.live_files == 0 && c.tracking_errors == 0);
        /* 同一运行时在故障后重新装载成功，证明 busy/链表/引用回滚后仍可用。 */
        c.fail_allocation = c.fail_read = c.fail_map = c.fail_protect = 0;
        c.events[0] = 0;
        int result = ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 0, 0, &module);
        REQUIRE(&c, result == SC_SCL_OK);
        EXPECT(&c, !strcmp(c.events, graph_init));
        EXPECT(&c, query(&c).modules == 4 && query(&c).loads == 4);
        EXPECT(&c, ScRuntimeClose(c.runtime, module) == SC_SCL_OK);
        EXPECT(&c, query(&c).modules == 0);
done:
        destroy(&c); report(&c, cases[i].name);
    }
}

static void test_pinning(const char *directory)
{
    Context c = {0}; ScModule root = 0, reopened = 0;
    void *function = NULL, *value = NULL, *invalid = (void *)(uintptr_t)0x1234;
    REQUIRE(&c, create(&c, directory) != NULL);
    int result = ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 0, 0, &root);
    REQUIRE(&c, result == SC_SCL_OK);
    EXPECT(&c, ScRuntimeSymbol(c.runtime, root, "root_function", 2, &invalid) == SC_SCL_SYMBOL);
    EXPECT(&c, ScRuntimeSymbol(c.runtime, root, "absent", 1, &invalid) == SC_SCL_SYMBOL);
    EXPECT(&c, invalid == (void *)(uintptr_t)0x1234 && query(&c).pinned_modules == 0);
    EXPECT(&c, ScRuntimeSymbol(c.runtime, root, "root_function", 1, &function) == SC_SCL_OK);
    EXPECT(&c, ScRuntimeSymbol(c.runtime, root, "root_value", 2, &value) == SC_SCL_OK);
    EXPECT(&c, (uintptr_t)function == base_of(&c, "ROOT.SCL") + 0x20u);
    EXPECT(&c, (uintptr_t)value == base_of(&c, "ROOT.SCL") + 0x1100u);
    EXPECT(&c, query(&c).pinned_modules == 1);
    EXPECT(&c, ScRuntimeClose(c.runtime, root) == SC_SCL_RESIDENT);
    EXPECT(&c, query(&c).modules == 4 && !strcmp(c.events, graph_init));
    EXPECT(&c, scl_read32(value) == 0xaabbccddu);
    EXPECT(&c, ScRuntimeSymbol(c.runtime, root, "root_value", 2, &invalid) == SC_SCL_HANDLE);
    EXPECT(&c, ScRuntimeClose(c.runtime, root) == SC_SCL_HANDLE);
    EXPECT(&c, ScRuntimeOpen(c.runtime, "ROOT.SCL", 1, 0, 0, &reopened) == SC_SCL_OK);
    EXPECT(&c, root == reopened && c.maps == 4);
    EXPECT(&c, ScRuntimeClose(c.runtime, reopened) == SC_SCL_RESIDENT);
    destroy(&c);
    EXPECT(&c, !strcmp(c.events, graph_destroy));
done:
    if (c.runtime) destroy(&c);
    report(&c, "escaped_function_data_addresses_pinned_until_destroy");
}

static void test_isolation(const char *directory)
{
    Context a = {0}, b = {0}; ScModule left = 0, right = 0;
    void *left_value = NULL, *right_value = NULL;
    REQUIRE(&a, create(&a, directory) != NULL);
    REQUIRE(&a, create(&b, directory) != NULL);
    int left_result = ScRuntimeOpen(a.runtime, "ROOT.SCL", 1, 0, 0, &left);
    int right_result = ScRuntimeOpen(b.runtime, "ROOT.SCL", 1, 0, 0, &right);
    REQUIRE(&a, left_result == 0 && right_result == 0);
    check_relocations(&a); check_relocations(&b);
    EXPECT(&a, base_of(&a, "ROOT.SCL") != base_of(&b, "ROOT.SCL"));
    EXPECT(&a, base_of(&a, "BASE.SCL") != base_of(&b, "BASE.SCL"));
    int left_symbol = ScRuntimeSymbol(a.runtime, left, "root_value", 2, &left_value);
    int right_symbol = ScRuntimeSymbol(b.runtime, right, "root_value", 2, &right_value);
    REQUIRE(&a, left_symbol == 0 && right_symbol == 0 && left_value != right_value);
    *(uint32_t *)left_value = 0xdeadbeefu;
    EXPECT(&a, scl_read32(left_value) == 0xdeadbeefu && scl_read32(right_value) == 0xaabbccddu);
    EXPECT(&a, ScRuntimeClose(a.runtime, left) == SC_SCL_RESIDENT);
    destroy(&a);
    EXPECT(&a, b.live_maps == 4 && scl_read32(right_value) == 0xaabbccddu);
    EXPECT(&a, ScRuntimeClose(b.runtime, right) == SC_SCL_RESIDENT);
done:
    if (a.runtime) destroy(&a);
    destroy(&b);
    a.checks += b.checks; a.failures += b.failures;
    if (b.peak_bytes > a.peak_bytes) a.peak_bytes = b.peak_bytes;
    report(&a, "two_runtime_private_data_and_relocations");
}

static void test_arguments(const char *directory)
{
    Context c = {0}; ScModule module = 0x76543210u;
    REQUIRE(&c, create(&c, directory) != NULL);
    EXPECT(&c, ScRuntimeOpen(c.runtime, "../BASE.SCL", 1, 0, 0, &module) == SC_SCL_FORMAT);
    EXPECT(&c, ScRuntimeOpen(c.runtime, "base.SCL", 1, 0, 0, &module) == SC_SCL_FORMAT);
    EXPECT(&c, ScRuntimeOpen(c.runtime, "BASE.SCL", 0, 0, 0, &module) == SC_SCL_FORMAT);
    EXPECT(&c, ScRuntimeOpen(c.runtime, "BASE.SCL", 1, 0, 0, NULL) == SC_SCL_FORMAT);
    EXPECT(&c, ScRuntimeClose(c.runtime, 0) == SC_SCL_HANDLE);
    ScRuntimeStats invalid = {.size = sizeof(invalid) - 4};
    EXPECT(&c, ScRuntimeQuery(c.runtime, &invalid) == SC_SCL_FORMAT);
    EXPECT(&c, c.reads == 0 && c.maps == 0 && module == 0x76543210u);
done:
    destroy(&c); report(&c, "invalid_argument_no_io");
}

int main(int argc, char **argv)
{
    if (argc != 2 || sizeof(void *) != 8 || sizeof(ScU32) != 4) {
        fprintf(stderr, "requires a 64-bit Linux host and a fixture directory\n"); return 2;
    }
    test_graph(argv[1]); test_preloaded_siblings(argv[1]); test_version(argv[1]); test_load_errors(argv[1]);
    test_init_failure(argv[1]); test_failpoints(argv[1]); test_pinning(argv[1]);
    test_isolation(argv[1]); test_arguments(argv[1]);
    printf("{\"summary\":true,\"cases\":%u,\"failed_cases\":%u,\"checks\":%u,"
           "\"executes_i386\":false,\"guest_qemu\":false}\n", case_count, failed_cases, total_checks);
    return failed_cases ? 1 : 0;
}
