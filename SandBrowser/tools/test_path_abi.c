/* Copyright (c) 2026 mio. Run the SDK's actual 32-bit path/status adapters.
 * Only the syscall boundary is substituted; no guest files are accessed. */
#include "../../sandcore/user/sc32/SC32PRIVATE.H"
void sc32_image_finalize(void);
void sc32_auth_finalize(void);
void sc32_gui_finalize(void);
#include "../../sandcore/user/sc32/sc32_support.c"
#include "../../sandcore/user/sc32/sc32_path.c"

static int stat_raw = -1, rename_raw = -1;
static ScI32 sc32_call(ScI32 number, ScI32 a, ScI32 b, ScI32 c, ScI32 d, ScI32 e, ScI32 f) {
    (void)c; (void)d; (void)e; (void)f;
    if (number == SC32_WIRE_GATE && a == SC32_WIRE_PROBE) return 0;
    if (number == 0x73) {
        const char *path = (const char *)a;
        char *relative = (char *)b;
        unsigned length = 0;
        if (*path == '/') path++;
        while (path[length] && length < 63) { relative[length] = path[length]; length++; }
        relative[length] = 0;
        return length;
    }
    if (number == 0x37) {
        if (!stat_raw) { ((ScU32 *)b)[0] = SC32_PATH_DIRECTORY; ((ScU32 *)b)[1] = 0; }
        return stat_raw;
    }
    if (number == 0x36) return rename_raw;
    return -1;
}
static int check(void) {
    ScPathStatInfo record = {sizeof(record), 1, 0x1234, 0x5678};
    /* kernel/fs.c fs_stat returns -1 for a missing path; sc32_legacy preserves
     * that as FAILED, rather than inventing a NOT_FOUND distinction. */
    if (ScPathStat("/HOME/SBROWSE.OLD", &record) != SC32_E_FAILED) return 1;
    if (sc32_last_error.domain != SC32_DOMAIN_LEGACY || sc32_last_error.raw != -1) return 2;
    if (record.kind != 0x1234 || record.bytes != 0x5678) return 3;
    stat_raw = -5;
    if (ScPathStat("/HOME/SBROWSE.OLD", &record) != SC32_E_ACCESS) return 4;
    stat_raw = 0;
    if (ScPathStat("/HOME/SBROWSE.OLD", &record) || record.kind != SC32_PATH_DIRECTORY) return 5;
    /* An occupied rename destination remains a failure; a stat candidate is
     * never permission to overwrite it. */
    if (ScPathRename("/HOME/SBROWSE", "/HOME/SBROWSE.OLD") != SC32_E_FAILED) return 6;
    return 0;
}
__attribute__((noreturn)) void _start(void) {
    int result = check();
    __asm__ volatile("int $0x80" :: "a"(1), "b"(result) : "memory");
    __builtin_unreachable();
}
