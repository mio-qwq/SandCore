/* Copyright (c) 2026 mio. 单 SCX 解包向导；载荷、进度、回滚均有真实文件语义。 */
#include "sb.h"
#include "miniz_tinfl.h"
#include <ctype.h>
typedef struct {
    char name[32];
    uint32_t size, packed, crc, offset;
} Entry;
typedef struct {
    ScWindow window;
    uint32_t *pixels;
    SbCanvas canvas;
    SbBuffer archive;
    Entry entries[128];
    unsigned count, total, completed, staged, bytes, phase, stage, next_tick, next_paint;
    unsigned cursor;
    int closed, cancel, shortcut, package_ready, installed, dirty, draw_error;
    int focus, pressed, hover, select_path;
    char package[160], destination[65], temporary[65], backup[65], error[384], detail[256];
} Setup;
static Setup setup;
enum { CONTROL_BACK = 1, CONTROL_NEXT, CONTROL_CANCEL, CONTROL_SHORTCUT, CONTROL_PATH };
static int control_enabled(int control) {
    switch (control) {
    case CONTROL_BACK:
        return (setup.stage > 0 && setup.stage < 3) ||
               (setup.stage == 5 && setup.package_ready && !setup.installed);
    case CONTROL_NEXT:
        return setup.stage != 3;
    case CONTROL_CANCEL:
        return 1;
    case CONTROL_SHORTCUT:
    case CONTROL_PATH:
        return setup.stage == 2;
    }
    return 0;
}
static uint32_t crc32(const void *v, unsigned n) {
    uint32_t crc = ~0u;
    const unsigned char *p = v;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320u & -(int)(crc & 1));
    }
    return ~crc;
}
static void label(int x, int y, const char *s, int px, int bold, uint32_t c) {
    sb_text(&setup.canvas, x, y, s, (SbFont){px, bold, 0, 0}, c);
}
static void button(int control, int x, int y, int w, const char *s) {
    int enabled = control_enabled(control);
    int down = enabled && setup.pressed == control && setup.hover == control;
    sb_fill(&setup.canvas, x, y, w, 28, 0xc0c0c0);
    sb_bevel(&setup.canvas, x, y, w, 28, down);
    int tw = sb_text_width(s, (SbFont){14, 0, 0, 0});
    label(x + (w - tw) / 2 + down, y + 7 + down, s, 14, 0, enabled ? 0 : 0x808080);
    if (enabled && setup.focus == control) {
        for (int i = 4; i < w - 4; i += 2) {
            sb_fill(&setup.canvas, x + i, y + 4, 1, 1, 0x404040);
            sb_fill(&setup.canvas, x + i, y + 23, 1, 1, 0x404040);
        }
        for (int i = 4; i < 24; i += 2) {
            sb_fill(&setup.canvas, x + 4, y + i, 1, 1, 0x404040);
            sb_fill(&setup.canvas, x + w - 5, y + i, 1, 1, 0x404040);
        }
    }
}
static void fail(const char *text) {
    strncpy(setup.error, text, sizeof(setup.error) - 1);
    setup.stage = 5;
    setup.focus = CONTROL_NEXT;
    setup.dirty = 1;
}
static void join(char *out, const char *dir, const char *leaf) {
    snprintf(out, 96, "%s/%s", dir, leaf);
}
static int valid_destination(void) {
    const char *p = setup.destination;
    unsigned n = strlen(p);
    if (n < 3 || n > 36 || *p != '/' || p[n - 1] == '/' || strstr(p, "..") || strstr(p, "//"))
        return 0;
    for (; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '/' && *p != '_' && *p != '-' && *p != '.')
            return 0;
    return 1;
}
static int load_package(void) {
    sb_buffer_reset(&setup.archive);
    setup.package_ready = 0;
    setup.count = setup.total = 0;
    if (sb_file_read(setup.package, &setup.archive) < 0) {
        fail("Cannot open BROWSER.PAK beside SETUP.SCX. Extract both ZIP entries to the same "
             "folder.");
        return -1;
    }
    unsigned char *p = setup.archive.data;
    unsigned length = setup.archive.length;
    if (length < 20 || memcmp(p, "SBPACK1\0", 8)) {
        fail("Invalid SandBrowser package header.");
        return -1;
    }
    uint32_t version;
    memcpy(&version, p + 8, 4);
    memcpy(&setup.count, p + 12, 4);
    memcpy(&setup.total, p + 16, 4);
    if (version != 1 || !setup.count || setup.count > 128 || setup.total > 64u * 1024u * 1024u) {
        fail("Unsupported package version or size.");
        return -1;
    }
    unsigned at = 20, total = 0;
    for (unsigned i = 0; i < setup.count; i++) {
        if (at > length || length - at < 44) {
            fail("Truncated package directory.");
            return -1;
        }
        Entry *e = setup.entries + i;
        memcpy(e, p + at, 44);
        at += 44;
        if (!memchr(e->name, 0, 32) || !e->name[0] || strstr(e->name, "..") ||
            e->size > SB_RESOURCE_MAX || !e->packed || e->packed > length - at) {
            fail("Invalid package entry.");
            return -1;
        }
        for (char *q = e->name; *q; q++)
            if (!(*q >= 'A' && *q <= 'Z') && !isdigit((unsigned char)*q) && *q != '_' &&
                *q != '.' && *q != '-') {
                fail("Unsafe package filename.");
                return -1;
            }
        if (strlen(e->name) > 20) {
            fail("Package filename exceeds the platform path contract.");
            return -1;
        }
        for (unsigned j = 0; j < i; j++)
            if (!strcmp(e->name, setup.entries[j].name)) {
                fail("Duplicate package filename.");
                return -1;
            }
        e->offset = at;
        at += e->packed;
        if (e->size > 64u * 1024u * 1024u - total) {
            fail("Package total overflow.");
            return -1;
        }
        total += e->size;
    }
    if (at != length || total != setup.total) {
        fail("Package size does not match its directory.");
        return -1;
    }
    setup.package_ready = 1;
    return 0;
}
static int cleanup(void) {
    int failures = 0;
    if (!setup.temporary[0])
        return 0;
    for (unsigned i = 0; i < setup.staged; i++) {
        char path[96];
        join(path, setup.temporary, setup.entries[i].name);
        int status = ScPathRemove(path);
        if (status < 0 && status != SC32_E_NOT_FOUND)
            failures++;
    }
    const char *profile[] = {"FAVS.TXT", "HISTORY.TXT", "OPTIONS.TXT"};
    for (unsigned i = 0; i < 3; i++) {
        char path[96];
        join(path, setup.temporary, profile[i]);
        int status = ScPathRemove(path);
        if (status < 0 && status != SC32_E_NOT_FOUND)
            failures++;
    }
    int status = ScPathRemove(setup.temporary);
    if (status < 0 && status != SC32_E_NOT_FOUND)
        failures++;
    if (!failures)
        setup.temporary[0] = 0;
    return failures ? -1 : 0;
}
static void fail_install(const char *text) {
    int status = cleanup();
    fail(text);
    if (status < 0)
        strncat(setup.error, " Some staging files could not be removed. Inspect the .NEW folder.",
                sizeof(setup.error) - strlen(setup.error) - 1);
}
static void start_install(void) {
    if (!setup.package_ready) {
        fail("The package has not been loaded. Click Retry to open BROWSER.PAK again.");
        return;
    }
    if (!valid_destination()) {
        fail("Use an absolute installation folder with 3 to 36 ASCII characters. Its parent folder "
             "must exist.");
        return;
    }
    /* valid_destination 已把正文限制在 36B，后缀仍在 65B 路径合同内。 */
    strcpy(setup.temporary, setup.destination);
    strcat(setup.temporary, ".NEW");
    strcpy(setup.backup, setup.destination);
    strcat(setup.backup, ".OLD");
    ScPathStatInfo st;
    sb_record(&st, sizeof(st));
    if (ScPathStat(setup.temporary, &st) >= 0 || ScPathStat(setup.backup, &st) >= 0) {
        fail("The .NEW or .OLD folder already exists. Choose another destination or inspect the "
             "previous installation backup first.");
        return;
    }
    sb_record(&st, sizeof(st));
    if (ScPathStat(setup.destination, &st) >= 0 && st.kind != SC32_PATH_DIRECTORY) {
        fail("The installation destination is a file.");
        return;
    }
    if (ScPathCreate(setup.temporary, SC32_PATH_DIRECTORY, 3) < 0) {
        fail("Cannot create the staging directory. Check its parent folder and write permission.");
        return;
    }
    setup.stage = 3;
    setup.completed = setup.staged = setup.bytes = 0;
    setup.cancel = 0;
    setup.focus = CONTROL_CANCEL;
    strcpy(setup.detail, "Preparing files...");
}
static void step_install(void) {
    if (setup.cancel) {
        fail_install("Installation cancelled. The previous installation has been preserved.");
        return;
    }
    if (setup.completed < setup.count) {
        Entry *e = setup.entries + setup.completed;
        unsigned char *data = malloc(e->size + 1);
        if (!data) {
            fail_install("Not enough memory to unpack this file.");
            return;
        }
        size_t got = tinfl_decompress_mem_to_mem(data, e->size, setup.archive.data + e->offset,
                                                 e->packed, TINFL_FLAG_PARSE_ZLIB_HEADER);
        if (got != e->size || crc32(data, e->size) != e->crc) {
            free(data);
            fail_install("The package is damaged: decompression or CRC verification failed.");
            return;
        }
        char path[96];
        join(path, setup.temporary, e->name);
        snprintf(setup.detail, sizeof(setup.detail), "Installing %s", e->name);
        setup.staged = setup.completed + 1;
        int status = sb_file_write(path, data, e->size);
        free(data);
        ScPathStatInfo written;
        sb_record(&written, sizeof(written));
        if (status < 0 || ScPathStat(path, &written) < 0 ||
            written.kind != SC32_PATH_FILE || written.bytes != e->size) {
            fail_install("A file could not be written and verified. The previous installation "
                         "is intact.");
            return;
        }
        setup.bytes += e->size;
        setup.completed++;
        return;
    }
    ScPathStatInfo st;
    sb_record(&st, sizeof(st));
    int upgrade = ScPathStat(setup.destination, &st) >= 0;
    if (upgrade) {
        const char *profile[] = {"FAVS.TXT", "HISTORY.TXT", "OPTIONS.TXT"};
        for (unsigned i = 0; i < 3; i++) {
            SbBuffer b = {0};
            char from[96], to[96];
            join(from, setup.destination, profile[i]);
            join(to, setup.temporary, profile[i]);
            ScPathStatInfo info;
            sb_record(&info, sizeof(info));
            if (ScPathStat(from, &info) >= 0) {
                if (sb_file_read(from, &b) < 0 || sb_file_write(to, b.data, b.length) < 0) {
                    sb_buffer_reset(&b);
                    fail_install("Cannot preserve the existing browser profile. The old "
                                 "installation was not changed.");
                    return;
                }
            }
            sb_buffer_reset(&b);
        }
        if (ScPathRename(setup.destination, setup.backup) < 0) {
            fail_install("Cannot move the existing installation to .OLD. Close any running "
                         "browser and retry.");
            return;
        }
    }
    if (ScPathRename(setup.temporary, setup.destination) < 0) {
        int restored = !upgrade || ScPathRename(setup.backup, setup.destination) >= 0;
        fail_install(restored ? "Cannot publish the new installation. The previous installation "
                                "has been preserved."
                              : "Cannot publish or restore the previous installation. Its files "
                                "remain in .OLD; recover that folder before retrying.");
        return;
    }
    setup.temporary[0] = 0;
    setup.installed = 1;
    strcpy(setup.detail, "Installation complete.");
    if (setup.shortcut) {
        char lnk[256], command[96];
        join(command, setup.destination, "BROWSER.SCX");
        int n = snprintf(lnk, sizeof(lnk), "SandBrowser\n%s\n\n", command);
        ScPathStatInfo info;
        sb_record(&info, sizeof(info));
        if (ScPathStat("/DESK/SBROWSE.LNK", &info) < 0) {
            int s = sb_file_write("/DESK/SBROWSE.LNK", lnk, n);
            if (s < 0)
                strcpy(setup.detail, "Installed. Desktop shortcut could not be created.");
        } else
            strcpy(setup.detail, "Installed. Existing desktop shortcut was preserved.");
    }
    if (upgrade && !strcmp(setup.detail, "Installation complete."))
        strcpy(setup.detail, "Installed. Profile preserved; previous files remain in .OLD.");
    setup.stage = 4;
    setup.focus = CONTROL_NEXT;
}
static void animation(void) {
    SbCanvas *c = &setup.canvas;
    unsigned t = setup.phase;
    int cx = 93, cy = 190, r = 63;
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) {
                int shade = 80 + (x + r) * 75 / (2 * r) - (y + r) * 25 / (2 * r);
                sb_fill(c, cx + x, cy + y, 1, 1, ((unsigned)shade << 8) | (shade + 70));
            }
    for (int k = -2; k <= 2; k++) {
        int yy = cy + k * 20;
        int half = k ? k == 2 || k == -2 ? 47 : 59 : r;
        sb_line(c, cx - half, yy, cx + half, yy, 0x79badf);
    }
    for (int k = -2; k <= 2; k++) {
        int xx = cx + k * 20 + (int)(t % 40) - 20;
        for (int y = -55; y <= 55; y += 2) {
            int off = (xx - cx) * (60 - abs(y) / 2) / 60;
            if (off * off + y * y < r * r)
                sb_fill(c, cx + off, cy + y, 1, 2, 0x88cce8);
        }
    }
    int nodes[][2] = {{30, 100}, {146, 100}, {34, 286}, {151, 283}, {90, 57}};
    for (int i = 0; i < 5; i++) {
        sb_line(c, cx, cy, nodes[i][0], nodes[i][1], 0x4b6d9e);
        sb_fill(c, nodes[i][0] - 5, nodes[i][1] - 5, 11, 9, 0xc0c0c0);
        sb_bevel(c, nodes[i][0] - 5, nodes[i][1] - 5, 11, 9, 0);
        unsigned at = (t + i * 17) % 90;
        int x = cx + (nodes[i][0] - cx) * (int)at / 90, y = cy + (nodes[i][1] - cy) * (int)at / 90;
        sb_fill(c, x - 1, y - 1, 3, 3, 0xffee94);
    }
    label(25, 345, "SANDBROWSER", 16, 1, 0xffffff);
    label(25, 370, "WORLD WIDE WEB", 12, 0, 0x9ecef0);
    label(25, 396, "mio / 2026", 14, 0, 0xbad5eb);
}
static void paint(void) {
    setup.canvas = (SbCanvas){setup.pixels, 720, 490, 720, 0, 0, 720, 490};
    sb_fill(&setup.canvas, 0, 0, 720, 490, 0xc0c0c0);
    for (int y = 8; y < 437; y++)
        sb_fill(&setup.canvas, 8, y, 177, 1, 0x001032 + ((y / 8) << 8));
    animation();
    label(207, 28, "SandBrowser Setup", 24, 1, 0);
    label(207, 68, "welcome to the World Wide Web !", 18, 1, 0x000080);
    sb_fill(&setup.canvas, 205, 101, 491, 1, 0x808080);
    sb_fill(&setup.canvas, 205, 102, 491, 1, 0xffffff);
    if (setup.stage == 0) {
        label(208, 128, "Welcome to a new window on the Web.", 16, 1, 0);
        label(208, 165, "A classic browser for SandCore, created by mio.", 14, 0, 0);
        label(208, 197, "Browse the Web, organize Favorites and return", 14, 0, 0);
        label(208, 219, "to pages from your History.", 14, 0, 0);
        label(208, 263, "Setup installs the browser, offline help and", 14, 0, 0);
        label(208, 285, "component licenses in the folder you choose.", 14, 0, 0);
        label(208, 329, "Click Next to review the installation.", 14, 0, 0);
    } else if (setup.stage == 1) {
        label(208, 128, "Components and notices", 16, 1, 0);
        label(208, 166, "HTML/CSS: litehtml + Gumbo", 14, 0, 0);
        label(208, 190, "JavaScript: Duktape     HTTPS: BearSSL", 14, 0, 0);
        label(208, 214, "Decompression: miniz    Math: musl", 14, 0, 0);
        label(208, 252, "Third-party notices and licenses will be installed", 14, 0, 0);
        label(208, 274, "as NOTICE.TXT and available in Help > Licenses.", 14, 0, 0);
        label(208, 318, "Secure pages require a compatible processor", 14, 0, 0);
        label(208, 342, "and a correctly set system clock.", 14, 0, 0);
        char size[96];
        snprintf(size, sizeof(size), "%u files / %u KiB     Version " SB_VERSION,
                 setup.count, (setup.total + 1023) / 1024);
        label(208, 382, size, 14, 0, 0x404040);
    } else if (setup.stage == 2) {
        label(208, 128, "Choose destination", 16, 1, 0);
        label(208, 165, "Install folder (parent must already exist):", 14, 0, 0);
        sb_fill(&setup.canvas, 208, 194, 485, 29, 0xffffff);
        sb_bevel(&setup.canvas, 208, 194, 485, 29, 1);
        SbCanvas saved = setup.canvas;
        setup.canvas.x0 = 212;
        setup.canvas.x1 = 689;
        int selected = setup.focus == CONTROL_PATH && setup.select_path;
        if (selected)
            sb_fill(&setup.canvas, 216, 199,
                    sb_text_width(setup.destination, (SbFont){14, 0, 0, 0}), 19, 0x000080);
        label(216, 201, setup.destination, 14, 0, selected ? 0xffffff : 0);
        if (setup.focus == CONTROL_PATH && !selected && (setup.phase & 4u)) {
            char before[65];
            memcpy(before, setup.destination, setup.cursor);
            before[setup.cursor] = 0;
            int x = 216 + sb_text_width(before, (SbFont){14, 0, 0, 0});
            sb_fill(&setup.canvas, x, 200, 1, 18, 0);
        }
        setup.canvas = saved;
        label(208, 243, "Tab: next control     Ctrl+A: select the path", 14, 0, 0);
        button(CONTROL_SHORTCUT, 209, 284, 20, setup.shortcut ? "x" : "");
        label(240, 291, "Create a desktop shortcut when possible", 14, 0, 0);
        label(208, 339, "Upgrades preserve Favorites, History and Options.", 14, 0, 0);
        label(208, 362, "Existing files are retained in a .OLD folder.", 14, 0, 0);
    } else if (setup.stage == 3 || setup.stage == 4) {
        label(208, 128, setup.stage == 3 ? "Installing SandBrowser" : "Ready to explore", 16, 1, 0);
        label(208, 174, setup.detail, 14, 0, 0);
        sb_fill(&setup.canvas, 208, 218, 486, 25, 0xffffff);
        sb_bevel(&setup.canvas, 208, 218, 486, 25, 1);
        unsigned percent = setup.total ? (uint64_t)setup.bytes * 100 / setup.total : 0;
        for (unsigned i = 0; i < percent * 47 / 100; i++)
            sb_fill(&setup.canvas, 212 + i * 10, 222, 7, 17, 0x000080);
        char line[160];
        snprintf(line, sizeof(line), "%u / %u files    %u / %u KiB", setup.completed, setup.count,
                 setup.bytes / 1024, setup.total / 1024);
        label(208, 267, line, 14, 0, 0);
        if (setup.stage == 4) {
            label(208, 313, "Click Launch to open the installed browser.", 14, 0, 0);
            label(208, 340, "Launch path:", 14, 0, 0);
            label(208, 363, setup.destination, 14, 0, 0);
        }
    } else {
        label(208, 128, "Setup could not finish", 16, 1, 0x900000);
        char line[66];
        const char *p = setup.error;
        int y = 170;
        while (*p && y < 402) {
            unsigned n = strlen(p);
            if (n > 62)
                n = 62;
            memcpy(line, p, n);
            line[n] = 0;
            label(208, y, line, 14, 0, 0);
            p += n;
            y += 22;
        }
    }
    sb_fill(&setup.canvas, 8, 447, 704, 1, 0x808080);
    sb_fill(&setup.canvas, 8, 448, 704, 1, 0xffffff);
    button(CONTROL_BACK, 420, 459, 86, "< Back");
    button(CONTROL_NEXT, 514, 459, 96,
           setup.stage == 2   ? "Install"
           : setup.stage == 4 ? "Launch"
           : setup.stage == 5 ? "Retry"
                              : "Next >");
    button(CONTROL_CANCEL, 618, 459, 90,
           setup.stage == 4 || setup.stage == 5 ? "Close" : "Cancel");
    ScStatus status = sb_window_present(setup.window, setup.pixels, 720, 490);
    setup.next_paint = sb_ticks() + (status == SC32_OK ? 1 : 20);
    if (status == SC32_OK) {
        setup.dirty = 0;
        if (setup.draw_error)
            ScWindowSetTitle(setup.window, "SandBrowser Setup - mio");
        setup.draw_error = 0;
    } else {
        setup.dirty = 1;
        if (status < 0 && status != SC32_E_AGAIN && status != SC32_E_BUSY &&
            status != SC32_E_STALE && status != setup.draw_error) {
            char title[128];
            snprintf(title, sizeof(title), "SandBrowser Setup - drawing error %d", status);
            ScWindowSetTitle(setup.window, title);
            sb_log("SandBrowser Setup: frame submission failed (%d).\n", status);
            setup.draw_error = status;
        }
    }
}
static void next(void) {
    if (setup.stage < 2)
        setup.stage++;
    else if (setup.stage == 2)
        start_install();
    else if (setup.stage == 5) {
        if (setup.installed) {
            setup.stage = 4;
            next();
        } else if (setup.package_ready)
            setup.stage = 2;
        else if (load_package() == 0)
            setup.stage = 0;
    } else if (setup.stage == 4) {
        char command[96];
        join(command, setup.destination, "BROWSER.SCX");
        ScJob job = 0;
        if (ScProcessSpawn(command, 0, SC32_SPAWN_NO_STREAM_INHERIT, &job) < 0)
            fail("Installed successfully, but the browser could not be launched. Open BROWSER.SCX "
                 "from the installation folder.");
        else {
            ScJobClose(job);
            setup.closed = 1;
        }
    }
}
static void activate(int control) {
    if (!control_enabled(control))
        return;
    if (control == CONTROL_NEXT)
        next();
    else if (control == CONTROL_BACK)
        setup.stage = setup.stage == 5 ? 2 : setup.stage - 1;
    else if (control == CONTROL_SHORTCUT)
        setup.shortcut = !setup.shortcut;
    else if (control == CONTROL_CANCEL) {
        if (setup.stage == 3)
            setup.cancel = 1;
        else
            setup.closed = 1;
    }
    if (!control_enabled(setup.focus))
        setup.focus = CONTROL_NEXT;
    setup.dirty = 1;
}
static int hit_control(int x, int y) {
    if (y >= 459 && y < 487) {
        if (x >= 420 && x < 506)
            return CONTROL_BACK;
        if (x >= 514 && x < 610)
            return CONTROL_NEXT;
        if (x >= 618 && x < 708)
            return CONTROL_CANCEL;
    }
    if (setup.stage == 2) {
        if (x >= 208 && x < 693 && y >= 194 && y < 223)
            return CONTROL_PATH;
        if (x >= 209 && x < 693 && y >= 284 && y < 312)
            return CONTROL_SHORTCUT;
    }
    return 0;
}
static void tab_focus(int backwards) {
    const int controls[] = {CONTROL_PATH, CONTROL_SHORTCUT, CONTROL_BACK,
                            CONTROL_NEXT, CONTROL_CANCEL};
    int at = 0;
    for (int i = 0; i < 5; i++)
        if (controls[i] == setup.focus)
            at = i;
    for (int i = 0; i < 5; i++) {
        at = (at + (backwards ? 4 : 1)) % 5;
        if (control_enabled(controls[at])) {
            setup.focus = controls[at];
            break;
        }
    }
    setup.dirty = 1;
}
static void path_key(unsigned key, unsigned mods) {
    unsigned n = strlen(setup.destination);
    if ((mods & SC32_MOD_CONTROL) && (key == 'a' || key == 'A'))
        setup.select_path = 1;
    else if (key == SC32_KEY_HOME) {
        setup.cursor = 0;
        setup.select_path = 0;
    } else if (key == SC32_KEY_END) {
        setup.cursor = n;
        setup.select_path = 0;
    } else if (key == SC32_KEY_LEFT || key == SC32_KEY_RIGHT) {
        if (setup.select_path)
            setup.cursor = key == SC32_KEY_LEFT ? 0 : n;
        else if (key == SC32_KEY_LEFT && setup.cursor)
            setup.cursor--;
        else if (key == SC32_KEY_RIGHT && setup.cursor < n)
            setup.cursor++;
        setup.select_path = 0;
    } else if (key == 8 || key == SC32_KEY_DELETE) {
        if (setup.select_path) {
            setup.destination[0] = 0;
            setup.cursor = 0;
            setup.select_path = 0;
        } else if (key == 8 && setup.cursor) {
            memmove(setup.destination + setup.cursor - 1, setup.destination + setup.cursor,
                    n - setup.cursor + 1);
            setup.cursor--;
        } else if (key == SC32_KEY_DELETE && setup.cursor < n)
            memmove(setup.destination + setup.cursor, setup.destination + setup.cursor + 1,
                    n - setup.cursor);
    }
    setup.dirty = 1;
}
static ScStatus procedure(ScWindow w, const ScMessage *m, void *context) {
    (void)context;
    if (m->id == SC32_MSG_CLOSE) {
        if (setup.stage == 3) {
            setup.cancel = 1;
            ScWindowRejectClose(w);
        } else
            setup.closed = 1;
        return 0;
    }
    if (m->id == SC32_MSG_PAINT) {
        setup.dirty = 1;
        return 0;
    }
    if (m->id == SC32_MSG_CAPTURELOST || m->id == SC32_MSG_QUEUELOSS ||
        m->id == SC32_MSG_VISIBILITY) {
        setup.pressed = setup.hover = 0;
        setup.dirty = 1;
        return 0;
    }
    if (m->id == SC32_MSG_DESTROY) {
        setup.closed = 1;
        setup.window = 0;
        return 0;
    }
    if (m->id == SC32_MSG_KEYDOWN) {
        unsigned key = m->data[1], mods = m->data[3];
        if (key == 9)
            tab_focus(!!(mods & SC32_MOD_SHIFT));
        else if (key == 27)
            activate(CONTROL_CANCEL);
        else if (!(m->flags & SC32_MSG_REPEAT) && (key == '\n' || key == '\r'))
            activate(setup.focus == CONTROL_PATH ? CONTROL_NEXT : setup.focus);
        else if (!(m->flags & SC32_MSG_REPEAT) && (mods & SC32_MOD_ALT)) {
            if (key == 'n' || key == 'N')
                activate(CONTROL_NEXT);
            else if (key == 'b' || key == 'B')
                activate(CONTROL_BACK);
            else if (key == 'c' || key == 'C')
                activate(CONTROL_CANCEL);
        } else if (key == ' ' && setup.focus != CONTROL_PATH &&
                   !(m->flags & SC32_MSG_REPEAT))
            activate(setup.focus);
        else if (setup.stage == 2 && setup.focus == CONTROL_PATH)
            path_key(key, mods);
        return 0;
    }
    if (m->id == SC32_MSG_TEXT && setup.stage == 2 && setup.focus == CONTROL_PATH &&
        !(m->data[1] & (SC32_MOD_CONTROL | SC32_MOD_ALT))) {
        unsigned scalar = m->data[0], n = strlen(setup.destination);
        if (scalar >= 32 && scalar < 127) {
            if (setup.select_path) {
                setup.destination[0] = 0;
                setup.cursor = n = 0;
                setup.select_path = 0;
            }
            if (n < 36) {
                memmove(setup.destination + setup.cursor + 1, setup.destination + setup.cursor,
                        n - setup.cursor + 1);
                setup.destination[setup.cursor++] = scalar;
                setup.dirty = 1;
            }
        }
        return 0;
    }
    if (m->id == SC32_MSG_MOUSEMOVE) {
        int control = hit_control((int)m->data[0], (int)m->data[1]);
        if (setup.pressed && setup.hover != control)
            setup.dirty = 1;
        setup.hover = control;
        return 0;
    }
    if (m->id == SC32_MSG_BUTTONDOWN || m->id == SC32_MSG_BUTTONUP) {
        if (!(m->data[3] & 1u))
            return 0;
        int control = hit_control((int)m->data[0], (int)m->data[1]);
        if (m->id == SC32_MSG_BUTTONDOWN) {
            if (control && control_enabled(control)) {
                setup.focus = control;
                if (control == CONTROL_PATH) {
                    setup.cursor = strlen(setup.destination);
                    setup.select_path = 0;
                    char before[65];
                    for (unsigned i = 0; i <= strlen(setup.destination); i++) {
                        memcpy(before, setup.destination, i);
                        before[i] = 0;
                        if (216 + sb_text_width(before, (SbFont){14, 0, 0, 0}) >=
                            (int)m->data[0]) {
                            setup.cursor = i;
                            break;
                        }
                    }
                } else if (ScPointerCapture(w, 1) == SC32_OK)
                    setup.pressed = setup.hover = control;
            }
        } else {
            int pressed = setup.pressed;
            setup.pressed = 0;
            if (pressed) {
                ScPointerCapture(w, 0);
                if (pressed == control)
                    activate(control);
            }
        }
        setup.dirty = 1;
        return 0;
    }
    return ScDefaultWindowProc(w, m, context);
}
int main(void) {
    SbRuntimeInit();
    memset(&setup, 0, sizeof(setup));
    setup.shortcut = 1;
    setup.focus = CONTROL_NEXT;
    setup.dirty = 1;
    char root[128], home[128];
    uint32_t n = 0;
    sb_base_path(root, sizeof(root));
    snprintf(setup.package, sizeof(setup.package), "%s/BROWSER.PAK", root);
    if (ScProcessEnvironment("HOME", home, sizeof(home), &n) < 0)
        strcpy(home, "/HOME");
    size_t home_length = strlen(home);
    if (home_length + sizeof("/SBROWSE") <= sizeof(setup.destination)) {
        memcpy(setup.destination, home, home_length);
        memcpy(setup.destination + home_length, "/SBROWSE", sizeof("/SBROWSE"));
    }
    setup.cursor = strlen(setup.destination);
    setup.pixels = malloc(720 * 490 * 4);
    if (!setup.pixels) {
        sb_log("SandBrowser Setup: cannot allocate the setup surface.\n");
        return 2;
    }
    ScWindowDesc desc;
    sb_record(&desc, sizeof(desc));
    desc.title = "SandBrowser Setup - mio";
    desc.x = 100;
    desc.y = 75;
    desc.client_width = 720;
    desc.client_height = 490;
    desc.style = SC32_STYLE_CLOSE | SC32_STYLE_MINIMIZE;
    desc.procedure = procedure;
    ScStatus status = ScWindowCreate(&desc, &setup.window);
    if (status < 0) {
        sb_log("SandBrowser Setup: window creation failed (%d).\n", status);
        free(setup.pixels);
        SbRuntimeFini();
        return 3;
    }
    /* 先交付首帧，再读包；载荷缺失/损坏也能进入同一个可重试向导。 */
    paint();
    load_package();
    while (!setup.closed) {
        ScMessage m;
        sb_record(&m, sizeof(m));
        for (unsigned messages = 0; messages < 64 && !setup.closed &&
             ScMessageGet(&m, 0) == SC32_OK; messages++) {
            status = ScMessageDispatch(&m);
            if (status < 0 && status != SC32_E_STALE)
                sb_log("SandBrowser Setup: event dispatch failed (%d).\n", status);
            sb_record(&m, sizeof(m));
        }
        if (setup.closed)
            break;
        if (setup.stage == 3) {
            step_install();
            setup.dirty = 1;
        }
        if ((int32_t)(sb_ticks() - setup.next_tick) >= 0) {
            setup.next_tick = sb_ticks() + 5;
            setup.phase++;
            setup.dirty = 1;
        }
        if (setup.dirty && (int32_t)(sb_ticks() - setup.next_paint) >= 0)
            paint();
        sb_wait(0, 0, 1);
    }
    if (setup.stage == 3)
        cleanup();
    if (setup.window)
        ScWindowDestroy(setup.window);
    sb_buffer_reset(&setup.archive);
    free(setup.pixels);
    SbRuntimeFini();
    return 0;
}
