/* Copyright (c) 2026 mio. Host regression for the rejected-frame bug and setup input.
 * Uses production code with strict SC32 boundary doubles; does not emulate a guest. */
#include "sb.h"
#include <assert.h>
#define main unused_setup_entry
#include "../src/installer.c"
#undef main

static unsigned frame_width = 720, frame_height = 490;
static unsigned frame_state = SC32_WINDOW_VISIBLE;
static unsigned submits, ends, cancels, invalidates, captures, creates, spawns, rejects;
static int claimed, submit_error, resize_on_begin;

void sb_record(void *p, unsigned n) {
    memset(p, 0, n);
    ((uint32_t *)p)[0] = n;
    ((uint32_t *)p)[1] = 1;
}
ScStatus ScWindowQuery(ScWindow w, ScWindowInfo *out) {
    assert(w == 9);
    out->generation = 9;
    out->geometry_epoch = 14;
    out->client_width = frame_width;
    out->client_height = frame_height;
    out->state = frame_state;
    return SC32_OK;
}
ScStatus ScWindowInvalidate(ScWindow w, ScI32 x, ScI32 y, ScU32 width, ScU32 height) {
    assert(w == 9 && !x && !y && width == frame_width && height == frame_height);
    invalidates++;
    return SC32_OK;
}
ScStatus ScPaintBegin(ScWindow w, ScPaintInfo *out) {
    assert(w == 9 && !claimed);
    claimed = 1;
    out->window = w;
    out->generation = 9;
    out->geometry_epoch = 14;
    out->paint_epoch = 23;
    out->client_width = frame_width + resize_on_begin;
    out->client_height = frame_height;
    return SC32_OK;
}
ScStatus ScWindowSubmit(const ScSubmitDesc *d) {
    assert(claimed && d->window == 9 && d->generation == 9 && d->geometry_epoch == 14);
    /* The real SC32 GUI rejects zero rectangles before touching any pixels. */
    if (!d->rectangle_count || !d->rectangles)
        return SC32_E_RANGE;
    assert(d->rectangle_count == 1 && d->pixels);
    const ScSubmitRect *r = d->rectangles;
    assert(!r->source_x && !r->source_y && !r->destination_x && !r->destination_y);
    assert(r->width == frame_width && r->height == frame_height);
    assert(d->width == frame_width && d->height == frame_height);
    assert(d->stride == frame_width * 4 && d->bytes == d->stride * frame_height);
    submits++;
    return submit_error;
}
ScStatus ScPaintEnd(const ScPaintInfo *p) {
    assert(claimed && p->window == 9 && p->paint_epoch == 23 && !submit_error);
    claimed = 0;
    ends++;
    return SC32_OK;
}
ScStatus ScPaintCancel(const ScPaintInfo *p) {
    assert(claimed && p->paint_epoch == 23);
    claimed = 0;
    cancels++;
    return SC32_OK;
}
ScStatus ScPointerCapture(ScWindow w, ScU32 enabled) {
    (void)w;
    captures = enabled;
    return SC32_OK;
}
ScStatus ScWindowRejectClose(ScWindow w) {
    (void)w;
    rejects++;
    return SC32_OK;
}
ScStatus ScDefaultWindowProc(ScWindow w, const ScMessage *m, void *p) {
    (void)w; (void)m; (void)p;
    return SC32_OK;
}
int sb_text_width(const char *text, SbFont font) {
    (void)font;
    return strlen(text) * 8;
}
void sb_buffer_reset(SbBuffer *b) {
    free(b->data);
    memset(b, 0, sizeof(*b));
}
int sb_file_read(const char *p, SbBuffer *b) {
    (void)p; (void)b;
    return SC32_E_NOT_FOUND;
}
ScStatus ScPathStat(const char *p, ScPathStatInfo *out) {
    (void)p; (void)out;
    return SC32_E_NOT_FOUND;
}
ScStatus ScPathCreate(const char *p, ScU32 kind, ScU32 mode) {
    assert(!strcmp(p, "/HOME/SBROWSE.NEW") && kind == SC32_PATH_DIRECTORY && mode == 3);
    creates++;
    return SC32_OK;
}
ScStatus ScProcessSpawn(const char *p, const ScI32 a[3], ScU32 flags, ScJob *out) {
    (void)p; (void)a; (void)flags;
    spawns++;
    *out = 1;
    return SC32_OK;
}
ScStatus ScJobClose(ScJob j) { (void)j; return SC32_OK; }

static void message(unsigned id, unsigned a, unsigned b, unsigned c, unsigned d) {
    ScMessage m;
    sb_record(&m, sizeof(m));
    m.id = id;
    m.data[0] = a;
    m.data[1] = b;
    m.data[2] = c;
    m.data[3] = d;
    assert(procedure(9, &m, 0) == SC32_OK);
}
static void click(int x, int y, unsigned button) {
    message(SC32_MSG_BUTTONDOWN, x, y, button, button);
    message(SC32_MSG_BUTTONUP, x, y, 0, button);
}
int main(void) {
    uint32_t *pixels = calloc(frame_width * frame_height, 4);
    assert(pixels);
    assert(sb_window_present(9, pixels, frame_width, frame_height) == SC32_OK);
    assert(submits == 1 && ends == 1 && !cancels && !claimed);
    submit_error = SC32_E_MEMORY;
    assert(sb_window_present(9, pixels, frame_width, frame_height) == SC32_E_MEMORY);
    assert(submits == 2 && ends == 1 && cancels == 1 && !claimed);
    submit_error = 0;
    resize_on_begin = 1;
    assert(sb_window_present(9, pixels, frame_width, frame_height) == SC32_E_STALE);
    assert(submits == 2 && cancels == 2 && !claimed);
    resize_on_begin = 0;
    frame_state = SC32_WINDOW_MINIMIZED;
    unsigned before = invalidates;
    assert(sb_window_present(9, pixels, frame_width, frame_height) == SC32_PENDING);
    assert(invalidates == before && !claimed);
    frame_state = SC32_WINDOW_VISIBLE;
    assert(sb_window_present(9, pixels, frame_width, frame_height) == SC32_OK);
    assert(submits == 3 && ends == 2 && !claimed);
    free(pixels);

    memset(&setup, 0, sizeof(setup));
    setup.window = 9;
    setup.focus = CONTROL_NEXT;
    setup.package_ready = 1;
    strcpy(setup.destination, "/HOME/SBROWSE");
    setup.cursor = strlen(setup.destination);
    click(540, 470, 2); /* Right click is not activation. */
    message(SC32_MSG_BUTTONUP, 540, 470, 0, 1);
    assert(!setup.stage);
    message(SC32_MSG_BUTTONDOWN, 540, 470, 1, 1);
    message(SC32_MSG_MOUSEMOVE, 400, 450, 1, 0);
    message(SC32_MSG_BUTTONUP, 400, 450, 0, 1);
    assert(!setup.stage && !captures);
    click(435, 470, 1); /* Disabled Back is inert. */
    assert(!setup.stage);
    click(540, 470, 1);
    click(540, 470, 1);
    assert(setup.stage == 2);
    click(300, 205, 1);
    message(SC32_MSG_KEYDOWN, 0, 'a', 0, SC32_MOD_CONTROL);
    const char *path = "/HOME/SBROWSE";
    for (const char *p = path; *p; p++)
        message(SC32_MSG_TEXT, *p, 0, 0, 0);
    assert(!strcmp(setup.destination, path) && setup.cursor == strlen(path));
    message(SC32_MSG_KEYDOWN, 0, SC32_KEY_HOME, 0, 0);
    message(SC32_MSG_KEYDOWN, 0, SC32_KEY_DELETE, 0, 0);
    message(SC32_MSG_TEXT, '/', 0, 0, 0);
    assert(!strcmp(setup.destination, path));
    click(540, 470, 1);
    assert(setup.stage == 3 && creates == 1);
    click(540, 470, 1); /* Disabled Next cannot execute during installation. */
    assert(setup.stage == 3 && creates == 1 && !spawns);
    message(SC32_MSG_CLOSE, 0, 0, 0, 0);
    assert(setup.cancel && !setup.closed && rejects == 1);
    setup.stage = 5;
    setup.focus = CONTROL_NEXT;
    message(SC32_MSG_KEYDOWN, 0, 13, 0, 0);
    assert(setup.stage == 2 && !setup.closed);
    puts("PASS: frame submission, failed-frame cancellation, resize/minimize/restore, setup input and retry");
    return 0;
}
