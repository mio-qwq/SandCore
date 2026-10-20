/* Copyright (c) 2026 mio. IE5 / Windows 98 风格原生浏览器外壳。 */
#include "sb.h"
#include <ctype.h>
#include <strings.h>
#define GRAY 0xc0c0c0
#define BLUE 0x000080
typedef struct {
    char text[SB_URL];
    unsigned cursor, anchor;
} Edit;
typedef struct {
    char url[SB_URL], title[192];
} Place;
typedef struct {
    ScJob job;
    ScProcessRef process;
    ScIpcChannel channel;
    int hello;
} Worker;
typedef struct {
    ScWindow window;
    ScWindowInfo info;
    ScIpcEndpoint endpoint;
    Worker page, net;
    ScSharedMemory memory;
    ScSharedGrant grant;
    ScSharedMapping mapping;
    SbSurface *shared;
    unsigned shared_bytes;
    SbPacket *packet;
    uint32_t *pixels, *pagepixels;
    SbCanvas canvas;
    int width, height, vx, vy, vw, vh, framew, frameh, docw, doch, sx, sy, closed, dirty, focus,
        menu, menu_hot, dialog, dialog_tab, sidebar, bar, links, statusbar, script, busy, secure,
        hover, drag, source_requested, option_script;
    uint32_t epoch, progress, total, last_job_check, startup, next_paint;
    int draw_error;
    int zoom, sent_zoom, view_pending;
    Edit address, input, homepage;
    char root[128], title[256], status[512], message[2048], loaded_url[SB_URL];
    Place history[128], favorites[64];
    unsigned history_count, history_index, favorite_count, side_scroll, side_selected;
} Browser;
static Browser app;
typedef struct {
    const char *name, *key;
    int command;
} MenuItem;
enum {
    C_OPEN = 1,
    C_SAVE,
    C_SOURCE,
    C_EXIT,
    C_BACK,
    C_FORWARD,
    C_STOP,
    C_REFRESH,
    C_HOME,
    C_FIND,
    C_COPY,
    C_PASTE,
    C_SELECTALL,
    C_TOOLBAR,
    C_LINKS,
    C_STATUS,
    C_FAVORITES,
    C_HISTORY,
    C_ADDFAV,
    C_DELETEFAV,
    C_OPTIONS,
    C_ABOUT,
    C_HELP,
    C_LICENSE,
    C_PROPERTIES,
    C_CLEARHISTORY,
    C_SEARCH,
    C_NEW,
    C_ZOOM_IN,
    C_ZOOM_OUT,
    C_ZOOM_RESET
};
static const MenuItem menus[][12] = {{{"New window", "Ctrl+N", C_NEW},
                                      {"Open...", "Ctrl+O", C_OPEN},
                                      {"Save source as...", "Ctrl+S", C_SAVE},
                                      {"Page properties", "", C_PROPERTIES},
                                      {"-", "", 0},
                                      {"Close", "Alt+F4", C_EXIT},
                                      {0}},
                                     {{"Copy address", "Ctrl+C", C_COPY},
                                      {"Paste address", "Ctrl+V", C_PASTE},
                                      {"Select address", "Ctrl+L", C_SELECTALL},
                                      {"-", "", 0},
                                      {"Find on this page...", "Ctrl+F", C_FIND},
                                      {0}},
                                     {{"Standard buttons", "", C_TOOLBAR},
                                      {"Links bar", "", C_LINKS},
                                      {"Status bar", "", C_STATUS},
                                      {"-", "", 0},
                                      {"Page source", "", C_SOURCE},
                                      {"Refresh", "F5", C_REFRESH},
                                      {"Stop", "Esc", C_STOP},
                                      {"-", "", 0},
                                      {"Zoom in", "Ctrl++", C_ZOOM_IN},
                                      {"Zoom out", "Ctrl+-", C_ZOOM_OUT},
                                      {"Restore 100%", "Ctrl+0", C_ZOOM_RESET},
                                      {0}},
                                     {{"Favorites explorer bar", "Ctrl+I", C_FAVORITES},
                                      {"Add to Favorites...", "Ctrl+D", C_ADDFAV},
                                      {"Remove selected favorite", "", C_DELETEFAV},
                                      {0}},
                                     {{"Back", "Alt+Left", C_BACK},
                                      {"Forward", "Alt+Right", C_FORWARD},
                                      {"Home", "Alt+Home", C_HOME},
                                      {"History explorer bar", "Ctrl+H", C_HISTORY},
                                      {"-", "", 0},
                                      {"Internet Options...", "", C_OPTIONS},
                                      {0}},
                                     {{"Contents and keyboard guide", "F1", C_HELP},
                                      {"Third-party licenses", "", C_LICENSE},
                                      {"-", "", 0},
                                      {"About SandBrowser", "", C_ABOUT},
                                      {0}}};
static int menu_x[] = {8, 64, 120, 178, 258, 316};
static const char *menu_names[] = {"File", "Edit", "View", "Favorites", "Go", "Help"};
static void strcopy(char *d, unsigned cap, const char *s) {
    if (cap) {
        unsigned n = strlen(s);
        if (n >= cap)
            n = cap - 1;
        memcpy(d, s, n);
        d[n] = 0;
    }
}
static int send_to(ScIpcChannel c, unsigned kind, unsigned id, unsigned a, unsigned b, unsigned x,
                   unsigned d, const void *p, unsigned n) {
    if (!c)
        return SC32_E_BROKEN_PIPE;
    return sb_send(c, kind, id, app.epoch, a, b, x, d, p, n);
}
static void changed(void) {
    app.dirty = 1;
    if (app.window)
        ScWindowInvalidate(app.window, 0, 0, app.width, app.height);
}
static void layout(void) {
    app.vx = app.sidebar ? 206 : 3;
    app.vy = 28 + (app.bar ? 58 : 0) + 34 + (app.links ? 26 : 0);
    app.vw = app.width - app.vx - 20;
    app.vh = app.height - app.vy - (app.statusbar ? 26 : 4) - 18;
    if (app.vw < 64)
        app.vw = 64;
    if (app.vh < 64)
        app.vh = 64;
}
static void flush_view(void) {
    if (!app.page.hello)
        return;
    if (app.sent_zoom != app.zoom) {
        if (send_to(app.page.channel, SB_ZOOM, 0, app.zoom, 0, 0, 0, 0, 0) != SC32_OK)
            return;
        app.sent_zoom = app.zoom;
        app.view_pending = 1;
    }
    if (app.view_pending &&
        send_to(app.page.channel, SB_VIEW, 0, app.vw, app.vh, app.sx, app.sy, 0, 0) == SC32_OK)
        app.view_pending = 0;
}
static void view(void) {
    layout();
    app.view_pending = 1;
    flush_view();
    changed();
}
static void set_zoom(int percent) {
    percent = percent < 50 ? 50 : percent > 200 ? 200 : percent;
    if (percent == app.zoom)
        return;
    app.sx = (int)((int64_t)app.sx * percent / app.zoom);
    app.sy = (int)((int64_t)app.sy * percent / app.zoom);
    app.zoom = percent;
    view();
}
static void path_for(char *out, const char *leaf) {
    snprintf(out, 160, "%s/%s", app.root, leaf);
}
static void profile_field(SbBuffer *buffer, const char *text) {
    char safe[SB_URL];
    unsigned i = 0;
    for (; text[i] && i + 1 < sizeof(safe); i++)
        safe[i] = (unsigned char)text[i] < 32 ? ' ' : text[i];
    safe[i] = 0;
    sb_buffer_append(buffer, safe, i);
}
static void persist(void) {
    char path[160];
    SbBuffer b = {0};
    for (unsigned i = 0; i < app.favorite_count; i++) {
        profile_field(&b, app.favorites[i].url);
        sb_buffer_append(&b, "\t", 1);
        profile_field(&b, app.favorites[i].title);
        sb_buffer_append(&b, "\n", 1);
    }
    path_for(path, "FAVS.TXT");
    int failed = sb_file_write(path, b.data, b.length) < 0;
    sb_buffer_reset(&b);
    for (unsigned i = 0; i < app.history_count; i++) {
        profile_field(&b, app.history[i].url);
        sb_buffer_append(&b, "\t", 1);
        profile_field(&b, app.history[i].title);
        sb_buffer_append(&b, "\n", 1);
    }
    path_for(path, "HISTORY.TXT");
    failed |= sb_file_write(path, b.data, b.length) < 0;
    sb_buffer_reset(&b);
    char config[SB_URL + 128];
    int n = snprintf(config, sizeof(config), "%d %d %d %d\n%s\nzoom=%d\n", app.script, app.bar, app.links,
                     app.statusbar, app.homepage.text, app.zoom);
    path_for(path, "OPTIONS.TXT");
    failed |= sb_file_write(path, config, n) < 0;
    if (failed)
        strcopy(app.status, sizeof(app.status),
                "Could not save the browser profile. Check free space and write permission.");
}
static void read_places(const char *leaf, Place *p, unsigned *count, unsigned cap) {
    SbBuffer b = {0};
    char path[160];
    path_for(path, leaf);
    if (sb_file_read(path, &b) < 0)
        return;
    char *line = (char *)b.data;
    *count = 0;
    while (line && *line && *count < cap) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab++ = 0;
            if (strlen(line) < SB_URL) {
                strcopy(p[*count].url, SB_URL, line);
                strcopy(p[*count].title, 192, tab);
                (*count)++;
            }
        }
        line = next;
    }
    sb_buffer_reset(&b);
}
static void preferences(void) {
    app.script = app.bar = app.links = app.statusbar = 1;
    app.zoom = 100;
    strcpy(app.homepage.text, "about:welcome");
    read_places("FAVS.TXT", app.favorites, &app.favorite_count, 64);
    read_places("HISTORY.TXT", app.history, &app.history_count, 128);
    app.history_index = app.history_count;
    SbBuffer b = {0};
    char path[160];
    path_for(path, "OPTIONS.TXT");
    if (sb_file_read(path, &b) >= 0 && b.data) {
        char *p = (char *)b.data;
        int *values[] = {&app.script, &app.bar, &app.links, &app.statusbar};
        for (unsigned i = 0; i < 4; i++) {
            if ((*p != '0' && *p != '1') || p[1] != (i == 3 ? '\n' : ' '))
                break;
            *values[i] = *p - '0';
            p += 2;
        }
        if (p > (char *)b.data && strchr(p, '\n')) {
            char *next = strchr(p, '\n');
            *next++ = 0;
            if (strlen(p) < SB_URL && *p)
                strcpy(app.homepage.text, p);
            if (!strncmp(next, "zoom=", 5)) {
                int z = atoi(next + 5);
                if (z >= 50 && z <= 200)
                    app.zoom = z;
            }
        }
    }
    sb_buffer_reset(&b);
}
static void notice(const char *s) {
    app.dialog = 6;
    app.menu = -1;
    strcopy(app.message, sizeof(app.message), s);
    changed();
}
static void open_dialog(int kind, const char *text) {
    app.dialog = kind;
    app.dialog_tab = 0;
    if (kind == 5)
        app.option_script = app.script;
    app.menu = -1;
    strcopy(app.input.text, sizeof(app.input.text), text ? text : "");
    app.input.anchor = 0;
    app.input.cursor = strlen(app.input.text);
    changed();
}
static void navigate(const char *s, int remember, const void *post, unsigned postbytes) {
    char target[SB_URL];
    while (*s == ' ' || *s == '\t')
        s++;
    if (!*s)
        return;
    if (strlen(s) >= sizeof(target)) {
        notice("The address is too long.");
        return;
    }
    if (!strstr(s, ":") && s[0] != '/')
        snprintf(target, sizeof(target), "https://%s", s);
    else
        strcpy(target, s);
    if (strlen(target) >= SB_URL - 1) {
        notice("The address is too long.");
        return;
    }
    if (strncmp(target, "http://", 7) && strncmp(target, "https://", 8) &&
        strncmp(target, "about:", 6) && strncmp(target, "file:", 5) && target[0] != '/') {
        notice("This address scheme is not supported. Use http, https, file, or about.");
        return;
    }
    unsigned old = app.epoch;
    app.epoch++;
    sb_send(app.net.channel, SB_CANCEL, 0, old, 0, 0, 0, 0, 0, 0);
    sb_send(app.page.channel, SB_CANCEL, 0, old, 0, 0, 0, 0, 0, 0);
    app.busy = 1;
    app.secure = 0;
    app.progress = app.total = 0;
    app.sx = app.sy = 0;
    strcopy(app.address.text, sizeof(app.address.text), target);
    app.address.cursor = app.address.anchor = strlen(app.address.text);
    strcopy(app.status, sizeof(app.status), "Connecting...");
    app.focus = 0;
    if (remember) {
        if (app.history_index < app.history_count)
            app.history_count = app.history_index + 1;
        if (app.history_count >= 128) {
            memmove(app.history, app.history + 1, 127 * sizeof(Place));
            app.history_count = 127;
        }
        app.history_index = app.history_count;
        strcopy(app.history[app.history_count].url, SB_URL, target);
        strcopy(app.history[app.history_count].title, 192, target);
        app.history_count++;
    }
    if (app.page.channel) {
        SbBuffer b = {0};
        sb_buffer_append(&b, target, strlen(target) + 1);
        sb_buffer_append(&b, post, postbytes);
        if (b.length <= SB_CHUNK)
            send_to(app.page.channel, SB_NAVIGATE, 0, 0, 0, 0, 0, b.data, b.length);
        else
            notice("The submitted form exceeds the 48 KiB transport limit.");
        sb_buffer_reset(&b);
        view();
    }
    changed();
}
static void clipboard_copy(const char *s, unsigned n) {
    ScClipboardDesc d;
    ScClipboardCandidate candidate;
    ScClipboardInfo out;
    sb_record(&d, sizeof(d));
    d.budget_bytes = n + 1;
    if (ScClipboardBegin(&d, &candidate) < 0)
        return;
    if (ScClipboardOffer(candidate, SC32_CLIPBOARD_TEXT, s, n) >= 0) {
        sb_record(&out, sizeof(out));
        if (ScClipboardCommit(candidate, 0, &out) >= 0)
            return;
    }
    ScClipboardAbort(candidate);
}
static void erase_selection(Edit *e) {
    unsigned a = e->cursor < e->anchor ? e->cursor : e->anchor,
             b = e->cursor > e->anchor ? e->cursor : e->anchor;
    memmove(e->text + a, e->text + b, strlen(e->text + b) + 1);
    e->cursor = e->anchor = a;
}
static void insert(Edit *e, const char *s, unsigned n) {
    erase_selection(e);
    unsigned length = strlen(e->text);
    if (n >= sizeof(e->text) - length)
        return;
    memmove(e->text + e->cursor + n, e->text + e->cursor, length - e->cursor + 1);
    memcpy(e->text + e->cursor, s, n);
    e->cursor += n;
    e->anchor = e->cursor;
}
static void clipboard_paste(Edit *e) {
    ScClipboardInfo info;
    sb_record(&info, sizeof(info));
    if (ScClipboardOpen(&info) < 0)
        return;
    ScBrokerIoResult r;
    sb_record(&r, sizeof(r));
    char text[SB_URL];
    if (ScClipboardRead(info.handle, SC32_CLIPBOARD_TEXT, 0, text, sizeof(text) - 1, &r) >= 0) {
        unsigned n = r.transferred;
        text[n] = 0;
        for (unsigned i = 0; i < n; i++)
            if (text[i] == '\r' || text[i] == '\n' || text[i] == '\t')
                text[i] = ' ';
        insert(e, text, n);
    }
    ScClipboardClose(info.handle);
}
static void command(int c) {
    app.menu = -1;
    switch (c) {
    case C_NEW: {
        char cmd[1024];
        int n = snprintf(cmd, sizeof(cmd), "%s/BROWSER.SCX %s", app.root, app.address.text);
        ScJob job = 0;
        if (n >= (int)sizeof(cmd) || ScProcessSpawn(cmd, 0, SC32_SPAWN_NO_STREAM_INHERIT, &job) < 0)
            notice("Could not open another browser window.");
        else
            ScJobClose(job);
        break;
    }
    case C_OPEN:
        open_dialog(2, "https://");
        break;
    case C_SAVE: {
        char path[160];
        path_for(path, "SAVED.HTM");
        open_dialog(4, path);
        break;
    }
    case C_FIND:
        open_dialog(3, "");
        break;
    case C_SOURCE:
        app.source_requested = 1;
        send_to(app.page.channel, SB_SOURCE, 0, 0, 0, 0, 0, 0, 0);
        break;
    case C_EXIT:
        app.closed = 1;
        break;
    case C_BACK:
        if (app.history_index > 0 && app.history_count) {
            app.history_index--;
            navigate(app.history[app.history_index].url, 0, 0, 0);
        }
        break;
    case C_FORWARD:
        if (app.history_index + 1 < app.history_count) {
            app.history_index++;
            navigate(app.history[app.history_index].url, 0, 0, 0);
        }
        break;
    case C_STOP:
        send_to(app.net.channel, SB_CANCEL, 0, 0, 0, 0, 0, 0, 0);
        send_to(app.page.channel, SB_CANCEL, 0, 0, 0, 0, 0, 0, 0);
        app.busy = 0;
        strcpy(app.status, "Stopped");
        break;
    case C_REFRESH:
        navigate(app.address.text, 0, 0, 0);
        break;
    case C_HOME:
        navigate(app.homepage.text, 1, 0, 0);
        break;
    case C_COPY:
        clipboard_copy(app.address.text, strlen(app.address.text));
        break;
    case C_PASTE:
        app.focus = 1;
        app.address.anchor = 0;
        app.address.cursor = strlen(app.address.text);
        clipboard_paste(&app.address);
        break;
    case C_SELECTALL:
        app.focus = 1;
        app.address.anchor = 0;
        app.address.cursor = strlen(app.address.text);
        break;
    case C_TOOLBAR:
        app.bar = !app.bar;
        view();
        break;
    case C_LINKS:
        app.links = !app.links;
        view();
        break;
    case C_STATUS:
        app.statusbar = !app.statusbar;
        view();
        break;
    case C_FAVORITES:
        app.sidebar = app.sidebar == 1 ? 0 : 1;
        app.side_scroll = 0;
        view();
        break;
    case C_HISTORY:
        app.sidebar = app.sidebar == 2 ? 0 : 2;
        app.side_scroll = 0;
        view();
        break;
    case C_ADDFAV:
        open_dialog(7, app.title);
        break;
    case C_DELETEFAV:
        if (app.favorite_count && app.side_selected < app.favorite_count) {
            memmove(app.favorites + app.side_selected, app.favorites + app.side_selected + 1,
                    (app.favorite_count - app.side_selected - 1) * sizeof(Place));
            app.favorite_count--;
            persist();
        }
        break;
    case C_CLEARHISTORY:
        app.history_count = app.history_index = 0;
        persist();
        break;
    case C_OPTIONS:
        open_dialog(5, app.homepage.text);
        break;
    case C_ABOUT:
        open_dialog(1, "");
        break;
    case C_HELP:
        navigate("about:help", 1, 0, 0);
        break;
    case C_LICENSE:
        navigate("about:licenses", 1, 0, 0);
        break;
    case C_PROPERTIES:
        open_dialog(8, "");
        break;
    case C_SEARCH:
        open_dialog(11, "");
        break;
    case C_ZOOM_IN:
        set_zoom(app.zoom + 10);
        break;
    case C_ZOOM_OUT:
        set_zoom(app.zoom - 10);
        break;
    case C_ZOOM_RESET:
        set_zoom(100);
        break;
    }
    changed();
}
static void dialog_accept(void) {
    int d = app.dialog;
    app.dialog = 0;
    if (d == 11) {
        char query[8192], target[8192];
        sb_percent_encode(app.input.text, query, sizeof(query), 1);
        snprintf(target, sizeof(target), "https://duckduckgo.com/html/?q=%s", query);
        navigate(target, 1, 0, 0);
    }
    if (d == 2)
        navigate(app.input.text, 1, 0, 0);
    if (d == 3)
        send_to(app.page.channel, SB_FIND, 0, 0, 0, 0, 0, app.input.text,
                strlen(app.input.text) + 1);
    if (d == 4) {
        ScPathStatInfo st;
        sb_record(&st, sizeof(st));
        if (ScPathStat(app.input.text, &st) == 0) {
            app.dialog = 10;
            strcopy(app.message, sizeof(app.message),
                    "This file already exists. Replace it with this page's source?");
            changed();
            return;
        }
        send_to(app.page.channel, SB_SAVE, 0, 0, 0, 0, 0, app.input.text,
                strlen(app.input.text) + 1);
    }
    if (d == 10)
        send_to(app.page.channel, SB_SAVE, 0, 0, 0, 0, 0, app.input.text,
                strlen(app.input.text) + 1);
    if (d == 5) {
        app.script = app.option_script;
        strcopy(app.homepage.text, sizeof(app.homepage.text), app.input.text);
        send_to(app.page.channel, SB_SCRIPT_OPTION, 0, app.script, 0, 0, 0, 0, 0);
        persist();
    }
    if (d == 7) {
        if (app.favorite_count >= 64)
            notice("The Favorites list is full (64 entries).");
        else {
            Place *p = app.favorites + app.favorite_count++;
            strcopy(p->url, SB_URL, app.address.text);
            strcopy(p->title, 192, app.input.text);
            persist();
        }
    }
    changed();
}
static void label(int x, int y, const char *s, int bold, uint32_t c) {
    sb_text(&app.canvas, x, y, s, (SbFont){SB_UI_FONT, bold, 0, 0}, c);
}
static void wrapped(int x, int y, int max, const char *s, uint32_t c) {
    char line[512];
    int at = 0;
    while (*s) {
        if (*s == '\n') {
            line[at] = 0;
            label(x, y, line, 0, c);
            at = 0;
            y += 21;
            s++;
            continue;
        }
        line[at++] = *s++;
        line[at] = 0;
        if (at >= 500 || sb_text_width(line, (SbFont){SB_UI_FONT, 0, 0, 0}) > max) {
            int cut = at;
            while (cut > 0 && line[cut - 1] != ' ')
                cut--;
            if (!cut)
                cut = at;
            char saved = line[cut];
            line[cut] = 0;
            label(x, y, line, 0, c);
            line[cut] = saved;
            memmove(line, line + cut, at - cut);
            at -= cut;
            y += 21;
        }
    }
    line[at] = 0;
    label(x, y, line, 0, c);
}
static void button(int x, int y, int w, int h, const char *s, int down, int enabled) {
    sb_fill(&app.canvas, x, y, w, h, GRAY);
    sb_bevel(&app.canvas, x, y, w, h, down);
    int tw = sb_text_width(s, (SbFont){SB_UI_FONT, 0, 0, 0});
    label(x + (w - tw) / 2 + down, y + (h - SB_UI_FONT - 2) / 2 + down, s, 0, enabled ? 0 : 0x808080);
}
static void edit_draw(Edit *e, int x, int y, int w, int h, int focused) {
    sb_fill(&app.canvas, x, y, w, h, 0xffffff);
    sb_bevel(&app.canvas, x, y, w, h, 1);
    SbCanvas old = app.canvas;
    app.canvas.x0 = x + 4;
    app.canvas.x1 = x + w - 4;
    app.canvas.y0 = y + 3;
    app.canvas.y1 = y + h - 3;
    char prefix[SB_URL];
    unsigned cursor = e->cursor;
    if (cursor > strlen(e->text))
        cursor = strlen(e->text);
    memcpy(prefix, e->text, cursor);
    prefix[cursor] = 0;
    int cursorw = sb_text_width(prefix, (SbFont){SB_UI_FONT, 0, 0, 0}),
        offset = focused && cursorw > w - 14 ? cursorw - w + 14 : 0;
    unsigned a = e->cursor < e->anchor ? e->cursor : e->anchor,
             b = e->cursor > e->anchor ? e->cursor : e->anchor;
    label(x + 5 - offset, y + 5, e->text, 0, 0);
    if (focused && a != b) {
        memcpy(prefix, e->text, a);
        prefix[a] = 0;
        int ax = sb_text_width(prefix, (SbFont){SB_UI_FONT, 0, 0, 0});
        memcpy(prefix, e->text + a, b - a);
        prefix[b - a] = 0;
        int sw = sb_text_width(prefix, (SbFont){SB_UI_FONT, 0, 0, 0});
        sb_fill(&app.canvas, x + 5 + ax - offset, y + 4, sw, h - 8, BLUE);
        label(x + 5 + ax - offset, y + 5, prefix, 0, 0xffffff);
    }
    if (focused)
        sb_fill(&app.canvas, x + 5 + cursorw - offset, y + 4, 1, h - 8, 0);
    app.canvas = old;
}
static void icon(int x, int y, int kind, int enabled) {
    uint32_t dark = enabled ? 0x185820 : 0x888888, light = enabled ? 0x5da35c : 0xbbbbbb;
    SbCanvas *c = &app.canvas;
    if (kind == 0 || kind == 1) {
        int sign = kind == 0 ? -1 : 1;
        int xx = x + 12;
        for (int i = 0; i < 11; i++)
            sb_fill(c, xx + sign * (10 - i), y + 3 + i, sign < 0 ? i + 3 : 3, 1, dark);
        sb_fill(c, x + 7, y + 10, 16, 6, dark);
        sb_line(c, x + 6, y + 10, x + 22, y + 10, light);
    } else if (kind == 2) {
        sb_fill(c, x + 6, y + 3, 18, 22, 0xffffff);
        sb_bevel(c, x + 6, y + 3, 18, 22, 0);
        for (int i = 0; i < 4; i++) {
            sb_line(c, x + 9 + i, y + 8, x + 20 + i, y + 20, enabled ? 0xbb2828 : 0x888888);
            sb_line(c, x + 20 + i, y + 8, x + 9 + i, y + 20, enabled ? 0xbb2828 : 0x888888);
        }
    } else if (kind == 3) {
        sb_circle(c, x + 15, y + 14, 10, dark);
        sb_circle(c, x + 15, y + 14, 6, GRAY);
        sb_fill(c, x + 15, y + 2, 14, 9, GRAY);
        sb_line(c, x + 21, y + 4, x + 21, y + 13, dark);
        sb_line(c, x + 21, y + 13, x + 12, y + 13, dark);
    } else if (kind == 4) {
        sb_fill(c, x + 6, y + 12, 20, 15, 0xdfd2a0);
        for (int i = 0; i < 12; i++)
            sb_fill(c, x + 15 - i, y + 2 + i, 1 + 2 * i, 1, 0x985044);
        sb_fill(c, x + 13, y + 18, 6, 9, 0x6a4d36);
        sb_fill(c, x + 8, y + 16, 4, 5, 0xc8e0f0);
    } else if (kind == 5) {
        sb_circle(c, x + 11, y + 11, 9, 0x385f80);
        sb_circle(c, x + 11, y + 11, 6, 0xe2f4ff);
        for (int i = 0; i < 4; i++)
            sb_line(c, x + 17 + i, y + 18, x + 25 + i, y + 26, 0x705028);
    } else if (kind == 6) {
        sb_fill(c, x + 3, y + 9, 25, 17, 0xe0be4e);
        sb_fill(c, x + 5, y + 5, 10, 5, 0xeed475);
        sb_bevel(c, x + 3, y + 9, 25, 17, 0);
        sb_text(c, x + 10, y + 9, "*", (SbFont){18, 1, 0, 0}, 0x1d3598);
    } else {
        sb_fill(c, x + 6, y + 3, 19, 24, 0xfff6d6);
        sb_bevel(c, x + 6, y + 3, 19, 24, 0);
        for (int i = 0; i < 4; i++)
            sb_fill(c, x + 10, y + 8 + i * 4, 11, 1, 0x486c9c);
        sb_circle(c, x + 8, y + 22, 6, 0x40794a);
    }
}
static void globe(int x, int y, int phase) {
    SbCanvas *c = &app.canvas;
    sb_circle(c, x, y, 14, 0x102d77);
    sb_circle(c, x - 2, y - 2, 11, 0x326fbd);
    sb_line(c, x - 12, y, x + 12, y, 0xc7e8f8);
    sb_line(c, x - 10, y - 7, x + 10, y - 7, 0x90b8df);
    sb_line(c, x - 10, y + 7, x + 10, y + 7, 0x90b8df);
    for (int i = -1; i <= 1; i++)
        sb_line(c, x + i * 6 - 2, y - 10, x + i * 6 + 2, y + 10, 0x90b8df);
    int dot = phase % 22 - 11;
    sb_circle(c, x + dot, y - 5, 2, 0xffef8d);
}
static void draw_menu(void) {
    if (app.menu < 0)
        return;
    int x = menu_x[app.menu], y = 26, w = 274;
    unsigned rows = 0;
    while (rows < 12 && menus[app.menu][rows].name)
        rows++;
    sb_fill(&app.canvas, x + 3, y + 3, w, rows * 25 + 6, 0x777777);
    sb_fill(&app.canvas, x, y, w, rows * 25 + 6, GRAY);
    sb_bevel(&app.canvas, x, y, w, rows * 25 + 6, 0);
    for (unsigned i = 0; i < rows; i++) {
        const MenuItem *m = &menus[app.menu][i];
        int yy = y + 3 + i * 25;
        if (m->name[0] == '-') {
            sb_fill(&app.canvas, x + 4, yy + 10, w - 8, 1, 0x808080);
            sb_fill(&app.canvas, x + 4, yy + 11, w - 8, 1, 0xffffff);
            continue;
        }
        int selected = app.menu_hot == (int)i;
        if (selected)
            sb_fill(&app.canvas, x + 3, yy, w - 6, 25, BLUE);
        if ((m->command == C_TOOLBAR && app.bar) || (m->command == C_LINKS && app.links) ||
            (m->command == C_STATUS && app.statusbar))
            label(x + 7, yy + 3, "+", 1, selected ? 0xffffff : 0);
        label(x + 24, yy + 4, m->name, 0, selected ? 0xffffff : 0);
        int kw = sb_text_width(m->key, (SbFont){SB_UI_FONT, 0, 0, 0});
        label(x + w - 10 - kw, yy + 4, m->key, 0, selected ? 0xffffff : 0);
    }
}
static void draw_dialog(void) {
    if (!app.dialog)
        return;
    int w = 540,
        h = app.dialog == 5   ? 380
            : app.dialog == 1 ? 328
            : app.dialog == 8 ? 300
                              : 220,
        x = (app.width - w) / 2, y = (app.height - h) / 2;
    sb_fill(&app.canvas, x + 5, y + 5, w, h, 0x606060);
    sb_fill(&app.canvas, x, y, w, h, GRAY);
    sb_bevel(&app.canvas, x, y, w, h, 0);
    sb_fill(&app.canvas, x + 3, y + 3, w - 6, 24, BLUE);
    const char *title = app.dialog == 1    ? "About SandBrowser"
                        : app.dialog == 2  ? "Open"
                        : app.dialog == 3  ? "Find"
                        : app.dialog == 11 ? "Search the Web"
                        : app.dialog == 4  ? "Save page source"
                        : app.dialog == 5  ? "Internet Options"
                        : app.dialog == 7  ? "Add Favorite"
                        : app.dialog == 8  ? "Page Properties"
                        : app.dialog == 10 ? "Confirm file replacement"
                                           : "SandBrowser";
    label(x + 10, y + 7, title, 1, 0xffffff);
    button(x + w - 25, y + 5, 19, 18, "x", 0, 1);
    if (app.dialog == 1) {
        globe(x + 53, y + 72, sb_ticks() / 3);
        label(x + 90, y + 47, "SandBrowser", 1, 0);
        label(x + 90, y + 73, "Version " SB_VERSION " for SandCore", 0, 0);
        label(x + 90, y + 98, "Author: mio", 1, BLUE);
        label(x + 25, y + 135, "Built " __DATE__ " " __TIME__, 0, 0);
        wrapped(x + 25, y + 163, w - 50,
                "A classic World Wide Web browser.\nHTML/CSS: litehtml + Gumbo    JavaScript: "
                "Duktape\nHTTPS: BearSSL TLS 1.2    Inflate: miniz\nOriginal browser integration "
                "and interface by mio.",
                0);
        button(x + 25, y + h - 43, 160, 27, "Component licenses", 0, 1);
    } else if (app.dialog == 8) {
        label(x + 22, y + 49, app.title, 1, 0);
        wrapped(x + 22, y + 82, w - 44, app.loaded_url, 0);
        label(x + 22, y + 166,
              app.secure ? "Connection: verified HTTPS (TLS 1.2)"
                         : "Connection: local page or HTTP",
              0, 0);
        label(x + 22, y + 194, "Certificate chain, hostname and UTC validity are required.", 0, 0);
        label(x + 22, y + 218, "No certificate-error override is provided.", 0, 0);
    } else if (app.dialog == 5) {
        const char *tabs[] = {"General", "Security", "Advanced"};
        for (int i = 0; i < 3; i++)
            button(x + 18 + i * 110, y + 40, 111, 28, tabs[i], app.dialog_tab == i, 1);
        sb_bevel(&app.canvas, x + 15, y + 68, w - 30, h - 126, 1);
        if (app.dialog_tab == 0) {
            label(x + 30, y + 91, "Home page", 1, 0);
            label(x + 30, y + 118, "The page opened by the Home button and at startup:", 0, 0);
            edit_draw(&app.input, x + 30, y + 148, w - 60, 28, 1);
            button(x + 30, y + 193, 120, 27, "Use current", 0, 1);
            button(x + 158, y + 193, 120, 27, "Use welcome", 0, 1);
            label(x + 30, y + 243, "History", 1, 0);
            button(x + 30, y + 271, 150, 27, "Clear history", 0, 1);
        } else if (app.dialog_tab == 1) {
            button(x + 30, y + 97, 18, 18, app.option_script ? "x" : "", 0, 1);
            label(x + 58, y + 98, "Enable JavaScript (reload page after applying)", 0, 0);
            wrapped(
                x + 30, y + 146, w - 60,
                "HTTPS always validates certificates, hostnames and dates. Mixed HTTP resources on "
                "HTTPS pages are blocked.\nRDRAND or RDSEED supplies secure randomness. If the "
                "UTC clock is missing, time servers are queried automatically.\nCookies are "
                "session-only and restricted to the exact host.",
                0);
        } else
            wrapped(
                x + 30, y + 95, w - 60,
                "HTML and CSS use a full layout library with tables, floats, positioned elements "
                "and flex layout.\nThis version uses the system font and PNG/JPEG/WebP/BMP "
                "decoder. SVG, GIF, frames, downloads and printing are not yet "
                "implemented.\nScript heaps and network resources have explicit memory limits.",
                0);
    } else if (app.dialog == 6 || app.dialog == 10) {
        wrapped(x + 24, y + 55, w - 48, app.message, 0);
    } else {
        label(x + 24, y + 52,
              app.dialog == 2    ? "Type an Internet address or a local absolute path:"
              : app.dialog == 3  ? "Find what:"
              : app.dialog == 11 ? "Search DuckDuckGo for:"
              : app.dialog == 4  ? "Save HTML source to this absolute path:"
                                 : "Name:",
              0, 0);
        edit_draw(&app.input, x + 24, y + 85, w - 48, 29, 1);
        if (app.dialog == 4)
            label(x + 24, y + 132, "HTML source only. Linked resources are not saved.", 0, 0);
        if (app.dialog == 7)
            wrapped(x + 24, y + 132, w - 48, app.address.text, 0);
    }
    button(x + w - 196, y + h - 43, 84, 27, app.dialog == 10 ? "Replace" : "OK", 0, 1);
    button(x + w - 103, y + h - 43, 84, 27,
           app.dialog == 1 || app.dialog == 6 || app.dialog == 8 ? "Close" : "Cancel", 0, 1);
}
static void render(void) {
    if (!app.pixels)
        return;
    app.canvas =
        (SbCanvas){app.pixels, app.width, app.height, app.width, 0, 0, app.width, app.height, 0};
    sb_fill(&app.canvas, 0, 0, app.width, app.height, GRAY);
    for (int i = 0; i < 6; i++) {
        int w = i == 3 ? 78 : 54;
        if (app.menu == i)
            sb_bevel(&app.canvas, menu_x[i] - 3, 3, w, 23, 1);
        label(menu_x[i], 6, menu_names[i], 0, 0);
    }
    sb_fill(&app.canvas, 2, 27, app.width - 4, 1, 0x808080);
    int y = 28;
    if (app.bar) {
        sb_bevel(&app.canvas, 2, y, app.width - 4, 58, 0);
        sb_fill(&app.canvas, 7, y + 7, 1, 44, 0xffffff);
        sb_fill(&app.canvas, 10, y + 7, 1, 44, 0x808080);
        const char *labels[] = {"Back", "Forward", "Stop",      "Refresh",
                                "Home", "Search",  "Favorites", "History"};
        for (int i = 0; i < 8; i++) {
            int x = 18 + i * 68, enabled = (i == 0   ? app.history_index > 0
                                            : i == 1 ? app.history_index + 1 < app.history_count
                                            : i == 2 ? app.busy
                                                     : 1);
            if (app.hover == i)
                sb_bevel(&app.canvas, x, y + 3, 66, 51, 0);
            icon(x + 17, y + 5, i, enabled);
            int tw = sb_text_width(labels[i], (SbFont){SB_UI_FONT, 0, 0, 0});
            label(x + (66 - tw) / 2, y + 36, labels[i], 0, enabled ? 0 : 0x808080);
        }
        globe(app.width - 30, y + 28, app.busy ? sb_ticks() / 3 : 0);
        y += 58;
    }
    sb_bevel(&app.canvas, 2, y, app.width - 4, 34, 0);
    label(14, y + 10, "Address", 0, 0);
    edit_draw(&app.address, 77, y + 4, app.width - 153, 26, app.focus == 1 && !app.dialog);
    button(app.width - 67, y + 4, 60, 26, "Go", 0, 1);
    y += 34;
    if (app.links) {
        sb_bevel(&app.canvas, 2, y, app.width - 4, 26, 0);
        label(12, y + 5, "Links", 0, 0);
        label(70, y + 5, "Welcome to the Web", 0, 0x000080);
        label(248, y + 5, "Using SandBrowser", 0, 0x000080);
        label(418, y + 5, "mio / About", 0, 0x000080);
        y += 26;
    }
    if (app.sidebar) {
        sb_bevel(&app.canvas, 3, app.vy, 197, app.vh + 18, 1);
        sb_fill(&app.canvas, 6, app.vy + 3, 191, 25, 0xdadada);
        label(14, app.vy + 7, app.sidebar == 1 ? "Favorites" : "History", 1, 0);
        button(176, app.vy + 6, 17, 17, "x", 0, 1);
        sb_fill(&app.canvas, 7, app.vy + 30, 188, app.vh - 14, 0xffffff);
        Place *p = app.sidebar == 1 ? app.favorites : app.history;
        unsigned count = app.sidebar == 1 ? app.favorite_count : app.history_count;
        unsigned rows = (app.vh - 40) / 26;
        for (unsigned i = 0; i < rows && i + app.side_scroll < count; i++) {
            unsigned idx = i + app.side_scroll;
            int yy = app.vy + 34 + i * 26;
            SbCanvas old = app.canvas;
            app.canvas.x0 = 10;
            app.canvas.x1 = 192;
            if (app.side_selected == idx)
                sb_fill(&app.canvas, 10, yy, 182, 25, 0xe2e8f4);
            sb_fill(&app.canvas, 15, yy + 7, 12, 10, 0xdfbc55);
            label(34, yy + 4, p[idx].title[0] ? p[idx].title : p[idx].url, 0, 0);
            app.canvas = old;
        }
        if (!count)
            label(16, app.vy + 47, "This list is empty.", 0, 0x707070);
    }
    sb_fill(&app.canvas, app.vx, app.vy, app.vw, app.vh, 0xffffff);
    if (app.pagepixels && app.framew > 0) {
        SbCanvas old = app.canvas;
        app.canvas.x0 = app.vx;
        app.canvas.y0 = app.vy;
        app.canvas.x1 = app.vx + app.vw;
        app.canvas.y1 = app.vy + app.vh;
        sb_blit(&app.canvas, app.vx, app.vy, app.framew, app.frameh, app.pagepixels, app.framew,
                app.frameh, app.framew);
        app.canvas = old;
    } else
        label(app.vx + 28, app.vy + 30, "Starting the World Wide Web services...", 0, 0x505050);
    sb_bevel(&app.canvas, app.vx - 2, app.vy - 2, app.vw + 4, app.vh + 4, 1);
    int bx = app.vx + app.vw + 2, by = app.vy;
    button(bx, by, 16, 16, "^", 0, 1);
    button(bx, by + app.vh - 16, 16, 16, "v", 0, 1);
    int track = app.vh - 32, thumb = app.doch > app.vh ? track * app.vh / app.doch : track;
    if (thumb < 20)
        thumb = 20;
    if (thumb > track)
        thumb = track;
    int pos = app.doch > app.vh ? (track - thumb) * app.sy / (app.doch - app.vh) : 0;
    sb_fill(&app.canvas, bx, by + 16, 16, track, 0xd6d6d6);
    button(bx, by + 16 + pos, 16, thumb, "", 0, 1);
    button(app.vx, app.vy + app.vh + 2, 16, 16, "<", 0, 1);
    button(app.vx + app.vw - 16, app.vy + app.vh + 2, 16, 16, ">", 0, 1);
    sb_fill(&app.canvas, app.vx + 16, app.vy + app.vh + 2, app.vw - 32, 16, 0xd6d6d6);
    if (app.statusbar) {
        int yy = app.height - 24;
        sb_bevel(&app.canvas, 3, yy, app.width - 414, 21, 1);
        SbCanvas old = app.canvas;
        app.canvas.x0 = 9;
        app.canvas.x1 = app.width - 419;
        label(10, yy + 3, app.status, 0, 0);
        app.canvas = old;
        char zoom_label[16];
        snprintf(zoom_label, sizeof(zoom_label), "%d%%", app.zoom);
        button(app.width - 407, yy, 25, 21, "-", 0, app.zoom > 50);
        button(app.width - 379, yy, 72, 21, zoom_label, 0, 1);
        button(app.width - 304, yy, 25, 21, "+", 0, app.zoom < 200);
        sb_bevel(&app.canvas, app.width - 277, yy, 103, 21, 1);
        if (app.busy) {
            unsigned bars = app.total ? app.progress * 12 / app.total : 0;
            for (unsigned i = 0; i < bars && i < 12; i++)
                sb_fill(&app.canvas, app.width - 272 + i * 8, yy + 4, 6, 13, BLUE);
        }
        sb_bevel(&app.canvas, app.width - 170, yy, 166, 21, 1);
        label(app.width - 162, yy + 3,
              app.secure                 ? "[lock] Secure HTTPS"
              : app.loaded_url[0] == 'h' ? "Internet"
                                         : "Local page",
              0, 0);
    }
    draw_menu();
    draw_dialog();
    ScStatus status = sb_window_present(app.window, app.pixels, app.width, app.height);
    app.next_paint = sb_ticks() + (status == SC32_OK ? 5 : 20);
    if (status == SC32_OK) {
        app.dirty = 0;
        if (app.draw_error)
            ScWindowSetTitle(app.window, app.title);
        app.draw_error = 0;
    } else {
        app.dirty = 1;
        if (status < 0 && status != SC32_E_AGAIN && status != SC32_E_BUSY &&
            status != SC32_E_STALE && status != app.draw_error) {
            char title[128];
            snprintf(title, sizeof(title), "SandBrowser - drawing error %d", status);
            ScWindowSetTitle(app.window, title);
            sb_log("SandBrowser: frame submission failed (%d).\n", status);
            app.draw_error = status;
        }
    }
}
static void edit_key(Edit *e, unsigned key, unsigned mods, unsigned scalar) {
    unsigned len = strlen(e->text);
    if (mods & SC32_MOD_CONTROL) {
        if (key == 'a' || key == 'A') {
            e->anchor = 0;
            e->cursor = len;
        }
        if (key == 'c' || key == 'C' || key == 'x' || key == 'X') {
            unsigned a = e->cursor < e->anchor ? e->cursor : e->anchor,
                     b = e->cursor > e->anchor ? e->cursor : e->anchor;
            clipboard_copy(e->text + a, b - a);
            if (key == 'x' || key == 'X')
                erase_selection(e);
        }
        if (key == 'v' || key == 'V')
            clipboard_paste(e);
        return;
    }
    if (key == SC32_KEY_HOME)
        e->cursor = 0;
    else if (key == SC32_KEY_END)
        e->cursor = len;
    else if (key == SC32_KEY_LEFT && e->cursor) {
        do {
            e->cursor--;
        } while (e->cursor && ((unsigned char)e->text[e->cursor] & 0xc0) == 0x80);
    } else if (key == SC32_KEY_RIGHT && e->cursor < len) {
        do {
            e->cursor++;
        } while (e->cursor < len && ((unsigned char)e->text[e->cursor] & 0xc0) == 0x80);
    } else if (key == 8) {
        if (e->cursor == e->anchor && e->cursor) {
            e->anchor = e->cursor;
            do {
                e->cursor--;
            } while (e->cursor && ((unsigned char)e->text[e->cursor] & 0xc0) == 0x80);
        }
        erase_selection(e);
    } else if (key == SC32_KEY_DELETE) {
        if (e->cursor == e->anchor && e->anchor < len) {
            do {
                e->anchor++;
            } while (e->anchor < len && ((unsigned char)e->text[e->anchor] & 0xc0) == 0x80);
        }
        erase_selection(e);
    } else if (scalar >= 32 && scalar != 127) {
        char b[4];
        unsigned n = 0;
        if (scalar < 128)
            b[n++] = scalar;
        else if (scalar < 2048) {
            b[n++] = 0xc0 | (scalar >> 6);
            b[n++] = 0x80 | (scalar & 63);
        } else if (scalar < 65536) {
            b[n++] = 0xe0 | (scalar >> 12);
            b[n++] = 0x80 | ((scalar >> 6) & 63);
            b[n++] = 0x80 | (scalar & 63);
        } else {
            b[n++] = 0xf0 | (scalar >> 18);
            b[n++] = 0x80 | ((scalar >> 12) & 63);
            b[n++] = 0x80 | ((scalar >> 6) & 63);
            b[n++] = 0x80 | (scalar & 63);
        }
        insert(e, b, n);
    } else
        return;
    if (!(mods & SC32_MOD_SHIFT))
        e->anchor = e->cursor;
}
static void scroll_to(int x, int y) {
    app.sx = x < 0 ? 0 : x > app.docw - app.vw ? app.docw - app.vw : x;
    app.sy = y < 0 ? 0 : y > app.doch - app.vh ? app.doch - app.vh : y;
    if (app.sx < 0)
        app.sx = 0;
    if (app.sy < 0)
        app.sy = 0;
    view();
}
static ScStatus procedure(ScWindow w, const ScMessage *m, void *context) {
    (void)context;
    if (m->id == SC32_MSG_CLOSE) {
        app.closed = 1;
        return 0;
    }
    if (m->id == SC32_MSG_SIZE) {
        ScWindowInfo i;
        sb_record(&i, sizeof(i));
        if (ScWindowQuery(w, &i) >= 0) {
            int ww = i.client_width, hh = i.client_height;
            if (ww < 1 || hh < 1 || ww > 1920 || hh > 1080)
                return 0;
            uint32_t *p = realloc(app.pixels, ww * hh * 4);
            if (p) {
                app.pixels = p;
                app.width = ww;
                app.height = hh;
                view();
            }
        }
        return 0;
    }
    if (m->id == SC32_MSG_PAINT) {
        app.dirty = 1;
        return 0;
    }
    if (m->id == SC32_MSG_QUEUELOSS) {
        app.menu = -1;
        app.drag = 0;
        changed();
        return 0;
    }
    if (m->id == SC32_MSG_KEYDOWN || m->id == SC32_MSG_TEXT) {
        unsigned key = m->id == SC32_MSG_TEXT ? 0 : m->data[1],
                 mods = m->id == SC32_MSG_TEXT ? m->data[1] : m->data[3],
                 scalar = m->id == SC32_MSG_TEXT ? m->data[0] : 0;
        if (app.dialog) {
            if (key == 27) {
                app.dialog = 0;
                changed();
                return 0;
            }
            if (key == '\n' || key == '\r') {
                dialog_accept();
                return 0;
            }
            if (app.dialog == 2 || app.dialog == 3 || app.dialog == 4 || app.dialog == 7 ||
                app.dialog == 11 || (app.dialog == 5 && app.dialog_tab == 0))
                edit_key(&app.input, key, mods, scalar);
            changed();
            return 0;
        }
        if (key == 27) {
            command(C_STOP);
            return 0;
        }
        if (mods & SC32_MOD_CONTROL) {
            unsigned symbol = key ? key : scalar;
            int zoom_command = symbol == '+' || symbol == '=' ? C_ZOOM_IN
                               : symbol == '-' || symbol == '_' ? C_ZOOM_OUT
                               : symbol == '0' ? C_ZOOM_RESET : 0;
            if (zoom_command) {
                if (key)
                    command(zoom_command);
                return 0;
            }
            int c = key == 'n' || key == 'N'   ? C_NEW
                    : key == 'l' || key == 'L' ? C_SELECTALL
                    : key == 'o' || key == 'O' ? C_OPEN
                    : key == 's' || key == 'S' ? C_SAVE
                    : key == 'f' || key == 'F' ? C_FIND
                    : key == 'h' || key == 'H' ? C_HISTORY
                    : key == 'i' || key == 'I' ? C_FAVORITES
                    : key == 'd' || key == 'D' ? C_ADDFAV
                                               : 0;
            if (c) {
                command(c);
                return 0;
            }
        }
        if (mods & SC32_MOD_ALT) {
            if (key == SC32_KEY_LEFT) {
                command(C_BACK);
                return 0;
            }
            if (key == SC32_KEY_RIGHT) {
                command(C_FORWARD);
                return 0;
            }
            if (key == SC32_KEY_HOME) {
                command(C_HOME);
                return 0;
            }
            for (int i = 0; i < 6; i++)
                if (tolower(key) == tolower(menu_names[i][0])) {
                    app.menu = i;
                    app.menu_hot = 0;
                    changed();
                    return 0;
                }
        }
        if (key == SC32_KEY_F1 + 4) {
            command(C_REFRESH);
            return 0;
        }
        if (key == SC32_KEY_F1) {
            command(C_HELP);
            return 0;
        }
        if (app.menu >= 0) {
            if (key == SC32_KEY_DOWN) {
                if (menus[app.menu][app.menu_hot + 1].name)
                    app.menu_hot++;
            } else if (key == SC32_KEY_UP && app.menu_hot > 0)
                app.menu_hot--;
            else if (key == '\n')
                command(menus[app.menu][app.menu_hot].command);
            changed();
            return 0;
        }
        if (app.focus == 1) {
            if (key == '\n' || key == '\r')
                navigate(app.address.text, 1, 0, 0);
            else
                edit_key(&app.address, key, mods, scalar);
            changed();
            return 0;
        }
        if (key == SC32_KEY_DOWN || key == SC32_KEY_UP || key == SC32_KEY_PAGEUP ||
            key == SC32_KEY_PAGEDOWN || key == SC32_KEY_HOME || key == SC32_KEY_END) {
            int y = app.sy + (key == SC32_KEY_DOWN       ? 40
                              : key == SC32_KEY_UP       ? -40
                              : key == SC32_KEY_PAGEUP   ? -app.vh
                              : key == SC32_KEY_PAGEDOWN ? app.vh
                                                         : 0);
            if (key == SC32_KEY_HOME)
                y = 0;
            if (key == SC32_KEY_END)
                y = app.doch;
            scroll_to(app.sx, y);
            return 0;
        }
        char utf[4];
        Edit temp = {0};
        if (scalar >= 32) {
            edit_key(&temp, 0, 0, scalar);
            unsigned n = strlen(temp.text);
            memcpy(utf, temp.text, n);
            send_to(app.page.channel, SB_KEY, 0, 0, mods, 0, 0, utf, n);
        } else if (key == 8 || key == 9 || key == 10 || key == 13)
            send_to(app.page.channel, SB_KEY, 0, key, mods, 0, 0, 0, 0);
        return 0;
    }
    if (m->id == SC32_MSG_WHEEL) {
        int x = (int)m->data[0], y = (int)m->data[1], wheel = (int)m->data[2];
        if ((m->data[5] & SC32_MOD_CONTROL) && wheel && !app.dialog) {
            set_zoom(app.zoom + (wheel > 0 ? 10 : -10));
            return 0;
        }
        if (app.sidebar && x < 200 && y > app.vy) {
            unsigned count = app.sidebar == 1 ? app.favorite_count : app.history_count;
            int offset = (int)app.side_scroll - wheel * 3;
            if (offset < 0)
                offset = 0;
            if (offset > (int)count)
                offset = count;
            app.side_scroll = offset;
            changed();
        } else
            scroll_to(app.sx, app.sy - wheel * 48);
        return 0;
    }
    if (m->id == SC32_MSG_MOUSEMOVE || m->id == SC32_MSG_BUTTONDOWN || m->id == SC32_MSG_BUTTONUP) {
        if (m->id != SC32_MSG_MOUSEMOVE && !(m->data[3] & 1u))
            return 0;
        int x = (int)m->data[0], y = (int)m->data[1], down = m->id == SC32_MSG_BUTTONDOWN,
            up = m->id == SC32_MSG_BUTTONUP;
        if (app.dialog) {
            if (!down)
                return 0;
            int ww = 540,
                hh = app.dialog == 5   ? 380
                     : app.dialog == 1 ? 328
                     : app.dialog == 8 ? 300
                                       : 220,
                xx = (app.width - ww) / 2, yy = (app.height - hh) / 2;
            if (x >= xx + ww - 26 && y < yy + 27 && y > yy) {
                app.dialog = 0;
                changed();
                return 0;
            }
            if (y >= yy + hh - 43 && y <= yy + hh - 16) {
                if (x >= xx + ww - 196 && x < xx + ww - 112)
                    dialog_accept();
                else if (x >= xx + ww - 103) {
                    app.dialog = 0;
                    changed();
                } else if (app.dialog == 1 && x >= xx + 25 && x <= xx + 185) {
                    app.dialog = 0;
                    command(C_LICENSE);
                }
            }
            if (app.dialog == 5) {
                if (y >= yy + 40 && y < yy + 68) {
                    int tab = (x - xx - 18) / 110;
                    if (tab >= 0 && tab < 3)
                        app.dialog_tab = tab;
                }
                if (app.dialog_tab == 1 && x > xx + 30 && x < xx + 500 && y > yy + 90 &&
                    y < yy + 122)
                    app.option_script = !app.option_script;
                if (app.dialog_tab == 0) {
                    if (y > yy + 193 && y < yy + 220) {
                        if (x < xx + 150)
                            strcopy(app.input.text, sizeof(app.input.text), app.address.text);
                        else if (x < xx + 278)
                            strcpy(app.input.text, "about:welcome");
                        app.input.cursor = app.input.anchor = strlen(app.input.text);
                    }
                    if (x > xx + 30 && x < xx + 180 && y > yy + 271 && y < yy + 298)
                        command(C_CLEARHISTORY);
                }
            }
            changed();
            return 0;
        }
        if (app.menu >= 0) {
            int mx = menu_x[app.menu], row = (y - 29) / 25;
            if (y >= 29 && x >= mx && x < mx + 274 && row >= 0 && row < 12 &&
                menus[app.menu][row].name) {
                app.menu_hot = row;
                if (up && menus[app.menu][row].command)
                    command(menus[app.menu][row].command);
                changed();
                return 0;
            }
            if (down) {
                app.menu = -1;
                changed();
            }
        }
        if (app.statusbar && down && y >= app.height - 24 && y < app.height - 3) {
            if (x >= app.width - 407 && x < app.width - 382)
                command(C_ZOOM_OUT);
            else if (x >= app.width - 379 && x < app.width - 307)
                command(C_ZOOM_RESET);
            else if (x >= app.width - 304 && x < app.width - 279)
                command(C_ZOOM_IN);
            return 0;
        }
        if (y < 27 && down) {
            for (int i = 0; i < 6; i++)
                if (x >= menu_x[i] - 3 && x < menu_x[i] + (i == 3 ? 75 : 51)) {
                    app.menu = i;
                    app.menu_hot = 0;
                    changed();
                    return 0;
                }
        }
        if (app.bar && y >= 31 && y < 84) {
            int i = (x - 18) / 68;
            if (x >= 18 && i < 8) {
                app.hover = i;
                if (up) {
                    int commands[] = {C_BACK, C_FORWARD, C_STOP,      C_REFRESH,
                                      C_HOME, C_SEARCH,  C_FAVORITES, C_HISTORY};
                    command(commands[i]);
                }
                changed();
                return 0;
            }
        }
        int ay = 28 + (app.bar ? 58 : 0);
        if (y >= ay && y < ay + 34 && down) {
            if (x >= app.width - 67)
                navigate(app.address.text, 1, 0, 0);
            else if (x >= 77) {
                app.focus = 1;
                app.address.anchor = 0;
                app.address.cursor = strlen(app.address.text);
                changed();
            }
            return 0;
        }
        if (app.links && y >= ay + 34 && y < ay + 60 && up) {
            if (x >= 70 && x < 240)
                navigate("about:welcome", 1, 0, 0);
            else if (x >= 248 && x < 410)
                command(C_HELP);
            else if (x >= 418)
                command(C_ABOUT);
            return 0;
        }
        if (app.sidebar && x < 200 && y >= app.vy && down) {
            if (x > 174 && y < app.vy + 28) {
                app.sidebar = 0;
                view();
                return 0;
            }
            int row = (y - app.vy - 34) / 26;
            if (y >= app.vy + 34 && row >= 0) {
                unsigned idx = app.side_scroll + row,
                         count = app.sidebar == 1 ? app.favorite_count : app.history_count;
                if (idx < count) {
                    app.side_selected = idx;
                    Place *p = app.sidebar == 1 ? app.favorites : app.history;
                    navigate(p[idx].url, 1, 0, 0);
                }
            }
            changed();
            return 0;
        }
        if (x >= app.vx + app.vw && x < app.vx + app.vw + 20 && y >= app.vy &&
            y < app.vy + app.vh && down) {
            if (y < app.vy + 16)
                scroll_to(app.sx, app.sy - 40);
            else if (y > app.vy + app.vh - 16)
                scroll_to(app.sx, app.sy + 40);
            else
                scroll_to(app.sx, (y - app.vy - 16) * (app.doch - app.vh) / (app.vh - 32));
            return 0;
        }
        if (y >= app.vy + app.vh && y < app.vy + app.vh + 20 && down) {
            scroll_to(app.sx + (x < app.vx + app.vw / 2 ? -80 : 80), app.sy);
            return 0;
        }
        if (x >= app.vx && x < app.vx + app.vw && y >= app.vy && y < app.vy + app.vh) {
            if (down)
                app.focus = 0;
            send_to(app.page.channel, SB_POINTER, 0, x - app.vx, y - app.vy,
                    down ? 1
                    : up ? 2
                         : 0,
                    m->data[2], 0, 0);
            return 0;
        }
        if (app.hover != -1) {
            app.hover = -1;
            changed();
        }
    }
    return ScDefaultWindowProc(w, m, context);
}
static int spawn_worker(Worker *w, const char *file, const char *endpoint, ScProcessRef *self) {
    char command[320];
    int n = snprintf(command, sizeof(command), "%s/%s %s %u:%u", app.root, file, endpoint,
                     self->pid, self->generation);
    if (n >= sizeof(command))
        return -1;
    int s = ScProcessSpawn(command, 0, SC32_SPAWN_NO_STREAM_INHERIT, &w->job);
    if (s < 0)
        return s;
    ScJobInfo info;
    sb_record(&info, sizeof(info));
    s = ScJobQuery(w->job, &info);
    if (s < 0)
        return s;
    sb_record(&w->process, sizeof(w->process));
    w->process.pid = info.child_pid;
    w->process.generation = info.child_generation;
    return w->process.pid && w->process.generation ? 0 : -1;
}
static void accept_workers(void) {
    for (;;) {
        ScIpcChannel c = 0;
        ScBrokerIdentity peer;
        sb_record(&peer, sizeof(peer));
        int s = ScIpcAccept(app.endpoint, &c, &peer);
        if (s < 0)
            break;
        Worker *w =
            peer.pid == app.page.process.pid && peer.generation == app.page.process.generation
                ? &app.page
            : peer.pid == app.net.process.pid && peer.generation == app.net.process.generation
                ? &app.net
                : 0;
        if (!w || w->channel) {
            ScIpcClose(c);
            continue;
        }
        w->channel = c;
    }
}
static void receive_worker(Worker *w, int role) {
    if (!w->channel)
        return;
    ScIpcPacket meta;
    int n;
    while ((n = sb_receive(w->channel, app.packet, &meta)) >= 0) {
        SbWire h = app.packet->h;
        if (meta.sender.pid != w->process.pid || meta.sender.generation != w->process.generation) {
            notice("IPC peer identity changed; channel closed.");
            ScIpcClose(w->channel);
            w->channel = 0;
            return;
        }
        if (!w->hello) {
            if (h.kind != SB_HELLO || h.a != (unsigned)role) {
                ScIpcClose(w->channel);
                w->channel = 0;
                return;
            }
            w->hello = 1;
            if (role == SB_ROLE_PAGE) {
                send_to(w->channel, SB_SURFACE, app.grant, app.shared_bytes, 0, 0, 0, 0, 0);
                send_to(w->channel, SB_SCRIPT_OPTION, 0, app.script, 0, 0, 0, 0, 0);
                view();
            }
            continue;
        }
        if (h.epoch != app.epoch) {
            if (role == SB_ROLE_PAGE && h.kind == SB_FRAME)
                sb_send(w->channel, SB_FRAME_ACK, h.id, h.epoch, 0, 0, 0, 0, 0, 0);
            continue;
        }
        if (role == SB_ROLE_NET) {
            if (h.kind == SB_HEADERS)
                app.total = h.c;
            if (h.kind == SB_DATA)
                app.progress += n;
            int s;
            while ((s = sb_send(app.page.channel, h.kind, h.id, h.epoch, h.a, h.b, h.c, h.d,
                                app.packet->body, n)) == SC32_E_AGAIN) {
                ScProcessSleep(1);
            }
            if (s < 0)
                notice("The page process stopped during resource transfer.");
            changed();
            continue;
        }
        if (h.kind == SB_FETCH) {
            app.progress = app.total = 0;
            const char *z = (char *)memchr(app.packet->body, 0, n);
            if (z) {
                strcopy(app.status, sizeof(app.status), "Receiving resource...");
                int s = sb_send(app.net.channel, SB_FETCH, h.id, h.epoch, h.a, h.b, h.c, h.d,
                                app.packet->body, n);
                if (s < 0) {
                    const char *msg = "Network service unavailable";
                    send_to(app.page.channel, SB_ERROR, h.id, 0, 0, 0, 0, msg, strlen(msg) + 1);
                }
            }
        } else if (h.kind == SB_FRAME) {
            if (app.shared && h.a > 0 && h.b > 0 && h.a <= 1920 && h.b <= 1080 &&
                h.a * h.b * 4 <= SB_SURFACE_BYTES && app.shared->generation == h.id &&
                app.shared->width == h.a && app.shared->height == h.b &&
                app.shared->reserved[0] == (unsigned)app.zoom) {
                __asm__ volatile("" ::: "memory");
                memcpy(app.pagepixels, (unsigned char *)app.shared + 4096, h.a * h.b * 4);
                app.framew = h.a;
                app.frameh = h.b;
                app.docw = app.shared->document_width;
                app.doch = app.shared->document_height;
                app.sx = app.shared->scroll_x;
                app.sy = app.shared->scroll_y;
            }
            send_to(app.page.channel, SB_FRAME_ACK, h.id, 0, 0, 0, 0, 0, 0);
            changed();
        } else if (h.kind == SB_FORM_POST) {
            const char *z = (char *)memchr(app.packet->body, 0, n);
            if (z) {
                char url[SB_URL];
                strcopy(url, sizeof(url), (char *)app.packet->body);
                unsigned off = z - (char *)app.packet->body + 1;
                SbBuffer post = {0};
                sb_buffer_append(&post, app.packet->body + off, n - off);
                if (!strncmp(url, "http://", 7) || !strncmp(url, "https://", 8))
                    navigate(url, 1, post.data, post.length);
                else
                    notice("Form submission requires HTTP or HTTPS.");
                sb_buffer_reset(&post);
            }
        } else if (n && app.packet->body[n - 1] == 0) {
            const char *text = (char *)app.packet->body;
            if (h.kind == SB_TITLE) {
                strcopy(app.title, sizeof(app.title), text);
                char title[320];
                snprintf(title, sizeof(title), "%s - SandBrowser", text);
                ScWindowSetTitle(app.window, title);
                if (app.history_index < app.history_count)
                    strcopy(app.history[app.history_index].title, 192, text);
            } else if (h.kind == SB_STATUS) {
                if (*text)
                    strcopy(app.status, sizeof(app.status), text);
                ScPointerSetCursor(app.window, h.a);
            } else if (h.kind == SB_LINK) { /* resources cannot ask the owner to open local files */
                if (strncmp(text, "file:", 5) && text[0] != '/')
                    navigate(text, h.a ? 0 : 1, 0, 0);
                else if (!strncmp(app.loaded_url, "file:", 5) || app.loaded_url[0] == '/' ||
                         !strncmp(app.loaded_url, "about:", 6))
                    navigate(text, h.a ? 0 : 1, 0, 0);
            } else if (h.kind == SB_SOURCE && app.source_requested) {
                char path[160], expected[176];
                path_for(path, "SOURCE.HTM");
                snprintf(expected, sizeof(expected), "file:%s", path);
                app.source_requested = 0;
                if (!strcmp(text, expected))
                    navigate(expected, 1, 0, 0);
            } else if (h.kind == SB_HEADERS) {
                app.secure = !!h.a;
                strcopy(app.loaded_url, sizeof(app.loaded_url), text);
                strcopy(app.address.text, sizeof(app.address.text), text);
                app.address.cursor = app.address.anchor = strlen(app.address.text);
            } else if (h.kind == SB_DONE) {
                app.busy = 0;
                strcpy(app.status, "Done");
                persist();
            } else if (h.kind == SB_ALERT || h.kind == SB_ERROR)
                notice(text);
            changed();
        }
    }
    if (n == SC32_E_BROKEN_PIPE) {
        ScIpcClose(w->channel);
        w->channel = 0;
        app.busy = 0;
        notice(role == SB_ROLE_PAGE
                   ? "The page process has exited. Close and reopen SandBrowser to start a new "
                     "page process."
                   : "The network process has exited. Close and reopen SandBrowser to reconnect.");
    }
}
int SbBrowserMain(void) {
    memset(&app, 0, sizeof(app));
    app.width = 1000;
    app.height = 680;
    app.menu = app.menu_hot = app.hover = -1;
    strcpy(app.title, "SandBrowser");
    strcpy(app.status, "Starting services...");
    sb_base_path(app.root, sizeof(app.root));
    if (!*app.root)
        return 1;
    ScProcessSetEnvironment("__SB_ROOT", app.root, 0);
    preferences();
    app.packet = malloc(sizeof(*app.packet));
    app.pixels = malloc(app.width * app.height * 4);
    app.pagepixels = malloc(SB_SURFACE_BYTES);
    if (!app.packet || !app.pixels || !app.pagepixels)
        return 2;
    layout();
    ScWindowDesc window;
    sb_record(&window, sizeof(window));
    window.title = "SandBrowser - mio";
    window.x = 60;
    window.y = 45;
    window.client_width = app.width;
    window.client_height = app.height;
    window.style = SC32_STYLE_STANDARD;
    window.procedure = procedure;
    if (ScWindowCreate(&window, &app.window) < 0)
        return 3;
    ScCallbackPin((const void *)procedure);
    changed();
    render();
    ScSharedDesc sd;
    sb_record(&sd, sizeof(sd));
    app.shared_bytes = (4096 + SB_SURFACE_BYTES + 4095) & ~4095u;
    sd.bytes = app.shared_bytes;
    sd.access = SC32_SHARED_READ | SC32_SHARED_WRITE;
    if (ScMemoryCreateShared(&sd, &app.memory) < 0) {
        notice("Cannot allocate shared page surface.");
        goto loop;
    }
    ScSharedMappingInfo map;
    sb_record(&map, sizeof(map));
    if (ScMemoryMapShared(app.memory, 0, app.shared_bytes, sd.access, &map) < 0) {
        notice("Cannot map shared page surface.");
        goto loop;
    }
    app.mapping = map.handle;
    app.shared = (SbSurface *)(uintptr_t)map.address;
    ScProcessRef self;
    sb_record(&self, sizeof(self));
    ScIdentityInfo identity;
    sb_record(&identity, sizeof(identity));
    if (ScProcessSelf(&self) < 0 || ScIdentityQuery(&identity) < 0)
        goto loop;
    char endpoint[64];
    snprintf(endpoint, sizeof(endpoint), "SB.%u.%u.%u", self.pid, self.generation, sb_ticks());
    ScIpcEndpointDesc ep;
    sb_record(&ep, sizeof(ep));
    ep.name = endpoint;
    ep.session = identity.session;
    ep.queue_budget_bytes = 262144;
    ep.max_frame_bytes = sizeof(SbPacket);
    ep.pending_budget_bytes = 262144;
    ep.acl_flags = SC32_IPC_ACL_SESSION | SC32_IPC_ACL_UID | SC32_IPC_ACL_GID | SC32_IPC_ACL_REALM;
    ep.allowed_uid = identity.uid;
    ep.allowed_gid = identity.gid;
    ep.allowed_realm = identity.realm;
    ep.allowed_session = identity.session;
    if (ScIpcCreate(&ep, &app.endpoint) < 0 || ScIpcListen(app.endpoint) < 0) {
        notice("IPC endpoint creation failed.");
        goto loop;
    }
    if (spawn_worker(&app.page, "WEBPAGE.SCX", endpoint, &self) < 0 ||
        spawn_worker(&app.net, "WEBNET.SCX", endpoint, &self) < 0) {
        notice(
            "Cannot launch WEBPAGE.SCX / WEBNET.SCX. Keep all installed program files together.");
        goto loop;
    }
    ScSharedGrantInfo grant;
    sb_record(&grant, sizeof(grant));
    if (ScMemoryGrantShared(app.memory, &app.page.process, 0, app.shared_bytes, sd.access, 0,
                            &grant) < 0) {
        notice("Cannot grant the page process access to its surface.");
        goto loop;
    }
    app.grant = grant.grant;
    app.startup = sb_ticks();
loop:
    while (!app.closed) {
        ScMessage m;
        sb_record(&m, sizeof(m));
        for (unsigned messages = 0; messages < 64 && ScMessageGet(&m, 0) == SC32_OK;
             messages++) {
            ScMessageDispatch(&m);
            sb_record(&m, sizeof(m));
        }
        accept_workers();
        receive_worker(&app.page, SB_ROLE_PAGE);
        receive_worker(&app.net, SB_ROLE_NET);
        flush_view();
        if (app.page.hello && app.net.hello && !app.epoch) {
            char args[SB_URL];
            uint32_t count = 0;
            if (ScProcessArguments(args, sizeof(args), &count) >= 0 && count)
                navigate(args, 1, 0, 0);
            else
                navigate(app.homepage.text, 1, 0, 0);
        }
        if ((int32_t)(sb_ticks() - app.next_paint) >= 0 && (app.dirty || app.busy))
            render();
        if (app.startup && !app.epoch && sb_ticks() - app.startup > 1000) {
            app.startup = 0;
            notice("The browser services did not finish starting. The installed SCL libraries must "
                   "match this SandCore SDK.");
        }
        if (sb_ticks() - app.last_job_check > 100) {
            app.last_job_check = sb_ticks();
            Worker *workers[] = {&app.page, &app.net};
            for (int i = 0; i < 2; i++)
                if (workers[i]->job) {
                    ScJobInfo job;
                    sb_record(&job, sizeof(job));
                    if (ScJobQuery(workers[i]->job, &job) >= 0 && job.state == SC32_JOB_FINISHED &&
                        !workers[i]->channel) {
                        ScJobClose(workers[i]->job);
                        workers[i]->job = 0;
                        app.busy = 0;
                        notice(i == 0 ? "WEBPAGE.SCX could not start or has exited. Check its SCL "
                                        "dependencies."
                                      : "WEBNET.SCX could not start or has exited. Check its SCL "
                                        "dependencies.");
                    }
                }
        }
        sb_wait(app.endpoint, SC32_BROKER_READY_ACCEPT, 1);
    }
    persist();
    send_to(app.page.channel, SB_SHUTDOWN, 0, 0, 0, 0, 0, 0, 0);
    send_to(app.net.channel, SB_SHUTDOWN, 0, 0, 0, 0, 0, 0, 0);
    Worker *workers[] = {&app.page, &app.net};
    for (int i = 0; i < 2; i++) {
        if (workers[i]->channel)
            ScIpcClose(workers[i]->channel);
        if (workers[i]->job) {
            ScJobInfo info;
            sb_record(&info, sizeof(info));
            if (ScJobWait(workers[i]->job, 20, &info) == SC32_TIMEOUT)
                ScJobCancel(workers[i]->job);
            ScJobClose(workers[i]->job);
        }
    }
    if (app.endpoint)
        ScIpcClose(app.endpoint);
    if (app.grant)
        ScMemoryRevokeShared(app.grant);
    if (app.mapping)
        ScMemoryUnmapShared(app.mapping);
    if (app.memory)
        ScMemoryCloseShared(app.memory);
    ScWindowDestroy(app.window);
    free(app.packet);
    free(app.pixels);
    free(app.pagepixels);
    return 0;
}
