/* Copyright (c) 2026 mio. 独立 HTML/CSS/JS 页面进程；不拥有窗口和 socket。 */
#include "sb.h"
#include "litehtml.h"
#include "litehtml/html_tag.h"
#include "litehtml/el_text.h"
#include "duktape.h"
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <ctype.h>
#include <strings.h>
using namespace litehtml;
using std::string;
extern "C" unsigned char *sb_gif(const unsigned char *, int, int *, int *);
static element::ptr find_id(const element::ptr &e, const string &id) {
    if (!e)
        return nullptr;
    if (id == e->get_attr("id", ""))
        return e;
    for (auto &child : e->children()) {
        auto found = find_id(child, id);
        if (found)
            return found;
    }
    return nullptr;
}
static void selector_order(const element::ptr &e, const std::vector<element::ptr> &matches,
                           std::vector<element::ptr> &out) {
    if (std::find(matches.begin(), matches.end(), e) != matches.end())
        out.push_back(e);
    for (auto &child : e->children())
        selector_order(child, matches, out);
}
static std::vector<element::ptr> selected(const element::ptr &e, const string &s) {
    if (s.find(',') == string::npos) {
        auto list = e->select_all(s);
        return {list.begin(), list.end()};
    }
    std::vector<element::ptr> matches, out;
    size_t begin = 0;
    int brackets = 0;
    char quote = 0;
    for (size_t i = 0; i <= s.size(); i++) {
        char c = i < s.size() ? s[i] : ',';
        if (quote) {
            if (c == quote && (i == 0 || s[i - 1] != '\\'))
                quote = 0;
        } else if (c == '\'' || c == '\"')
            quote = c;
        else if (c == '[' || c == '(')
            brackets++;
        else if (c == ']' || c == ')')
            brackets--;
        else if (c == ',' && !brackets) {
            auto list = e->select_all(s.substr(begin, i - begin));
            matches.insert(matches.end(), list.begin(), list.end());
            begin = i + 1;
        }
    }
    selector_order(e, matches, out);
    return out;
}
static uint32_t color(web_color c) {
    return (uint32_t(c.red) << 16) | (uint32_t(c.green) << 8) | c.blue;
}
static string escape(const string &s) {
    string out;
    for (char c : s) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '\"')
            out += "&quot;";
        else
            out += c;
    }
    return out;
}
static string serialize(const element::ptr &e, bool children_only = false) {
    if (e->is_text()) {
        string text;
        e->get_text(text);
        return escape(text);
    }
    string text, tag = e->get_tagName();
    if (!children_only) {
        text = "<" + tag;
        for (auto &a : e->dump_get_attrs())
            text += " " + std::get<0>(a) + "=\"" + escape(std::get<1>(a)) + "\"";
        text += ">";
    }
    for (auto &c : e->children())
        text += serialize(c);
    if (!children_only && tag != "input" && tag != "img" && tag != "br" && tag != "hr" &&
        tag != "meta" && tag != "link" && tag != "area" && tag != "base")
        text += "</" + tag + ">";
    return text;
}
static const char *form_css =
    "input,textarea,select,button{display:inline-block;font-family:Tahoma;font-size:14px;color:"
    "black;vertical-align:middle;margin:2px;padding:3px;border:2px inset "
    "#c0c0c0;background:white}input{width:160px;height:20px}input[type=checkbox],input[type=radio]{"
    "width:15px;height:15px}input[type=hidden]{display:none}input[type=submit],input[type=button],"
    "input[type=reset],button{width:auto;min-width:60px;height:24px;background:#c0c0c0;border-"
    "style:outset}textarea{width:240px;height:80px}select{width:160px;height:22px}script,style{"
    "display:none}";
struct Resource {
    string url, type, error;
    SbBuffer bytes = {};
    int state = 0, secure = 0;
    uint32_t status = 0;
    std::vector<uint32_t> pixels;
    int width = 0, height = 0;
    ~Resource() {
        sb_buffer_reset(&bytes);
    }
};
class Page;
static Page *active;
struct Control : html_tag {
    explicit Control(const document::ptr &d) : html_tag(d) {}
    bool is_replaced() const override {
        return true;
    }
    void parse_attributes() override {
        html_tag::parse_attributes();
        string text;
        if (!strcmp(get_tagName(), "textarea"))
            get_text(text);
        set_attr("_sbdefault", get_attr("value", text.c_str()));
        set_attr("_sbchecked_default", get_attr("checked") ? "1" : "0");
        if (!strcmp(get_tagName(), "select")) {
            auto opts = selected(shared_from_this(), "option");
            unsigned index = 0;
            for (unsigned i = 0; i < opts.size(); i++)
                if (opts[i]->get_attr("selected"))
                    index = i;
            char value[16];
            snprintf(value, sizeof(value), "%u", index);
            set_attr("_sbindex", value);
            set_attr("_sbindex_default", value);
        }
    }
    void get_content_size(litehtml::size &s, int) override {
        string tag = get_tagName();
        s.width = tag == "textarea" ? 240 : 160;
        s.height = tag == "textarea" ? 80 : 22;
    }
};
struct Timer {
    unsigned id, due, interval;
    void *fn;
};
class Page : public document_container {
  public:
    ScIpcChannel channel = 0;
    ScSharedMemory memory = 0;
    ScSharedMapping mapping = 0;
    SbSurface *surface = nullptr;
    SbCanvas canvas = {};
    SbPacket *packet = nullptr;
    uint32_t epoch = 0, sequence = 1, frame_sequence = 0;
    int width = 800, height = 500, scrollx = 0, scrolly = 0, quit = 0, cancelled = 0,
        frame_pending = 0, dirty = 1, js_enabled = 1;
    string base, url, source, pending_url, pending_post;
    uint32_t pending_epoch = 0;
    document::ptr doc;
    std::map<string, std::shared_ptr<Resource>> cache;
    unsigned resource_bytes = 0;
    duk_context *js = nullptr;
    size_t js_bytes = 0;
    unsigned deadline = 0;
    std::vector<element::ptr> nodes;
    std::vector<Timer> timers;
    unsigned timer_id = 0;
    bool changed = false;
    string focus_id;
    element::ptr focus;
    int zoom = 100;
    ~Page() {
        if (js)
            duk_destroy_heap(js);
        doc.reset();
        cache.clear();
        if (mapping)
            ScMemoryUnmapShared(mapping);
        if (memory)
            ScMemoryCloseShared(memory);
        free(packet);
    }
    void say(unsigned k, const string &s, unsigned a = 0) {
        int status;
        while ((status = sb_send(channel, k, 0, epoch, a, 0, 0, 0, s.c_str(), s.size() + 1)) ==
               SC32_E_AGAIN) {
            sb_wait(channel, SC32_BROKER_READY_WRITE, 1);
        }
        if (status < 0)
            quit = 1;
    }
    bool safe_url(const string &s) {
        if (s.compare(0, 6, "about:") == 0)
            return true;
        if (s.compare(0, 5, "file:") == 0 || (!s.empty() && s[0] == '/'))
            return url.compare(0, 5, "file:") == 0 || url.compare(0, 6, "about:") == 0 ||
                   (!url.empty() && url[0] == '/');
        SbUrl parsed;
        char e[256];
        if (sb_url_parse(s.c_str(), &parsed, e, sizeof(e)))
            return false;
        if (url.compare(0, 8, "https://") == 0 && !parsed.secure)
            return false;
        return true;
    }
    string resolve_url(const char *s, const char *b = nullptr) {
        char out[SB_URL];
        string from = b && *b ? b : base;
        if (from.compare(0, 6, "about:") == 0) {
            char root[128];
            sb_base_path(root, sizeof(root));
            from = string("file:") + root + "/WELCOME.HTM";
        }
        return sb_url_resolve(from.c_str(), s ? s : "", out, sizeof(out)) ? string() : string(out);
    }
    void navigate_packet(const SbWire &h, const unsigned char *b, unsigned n) {
        const char *z = (const char *)memchr(b, 0, n);
        if (!z || z - (const char *)b >= SB_URL)
            return;
        pending_url = (const char *)b;
        pending_post.assign(z + 1, (const char *)b + n);
        pending_epoch = h.epoch;
        cancelled = 1;
    }
    void ancillary(const SbWire &h, const unsigned char *b, unsigned n) {
        if (h.kind == SB_SHUTDOWN) {
            quit = cancelled = 1;
            return;
        }
        if (h.kind == SB_NAVIGATE) {
            navigate_packet(h, b, n);
            return;
        }
        if (h.kind == SB_CANCEL && h.epoch == epoch) {
            cancelled = 1;
            return;
        }
        if (h.kind == SB_FRAME_ACK && h.id == frame_sequence)
            frame_pending = 0;
        if (h.kind == SB_VIEW) {
            width = std::max(64, std::min(1920, (int)h.a));
            height = std::max(64, std::min(1080, (int)h.b));
            scrollx = h.c;
            scrolly = h.d;
            if (doc) {
                doc->media_changed();
                doc->render(width);
            }
            dirty = 1;
        }
        if (h.kind == SB_SCRIPT_OPTION)
            js_enabled = !!h.a;
    }
    std::shared_ptr<Resource> resource(const string &key, bool main = false,
                                       const string &post = string()) {
        auto it = cache.find(key);
        if (!main && it != cache.end())
            return it->second;
        auto r = std::make_shared<Resource>();
        r->url = key;
        r->state = -1;
        if (cache.size() >= 96 && !main) {
            r->error = "Resource count limit reached";
            return r;
        }
        cache[key] = r;
        if (!main && !safe_url(key)) {
            r->error = "Blocked mixed content or unsupported resource URL";
            return r;
        }
        if (key.compare(0, 6, "about:") == 0) {
            string filename = key == "about:licenses" ? "LICENSES.HTM"
                              : key == "about:help"   ? "HELP.HTM"
                              : key == "about:about"  ? "ABOUT.HTM"
                                                      : "WELCOME.HTM";
            char root[128];
            sb_base_path(root, sizeof(root));
            char path[160];
            snprintf(path, sizeof(path), "%s/%s", root, filename.c_str());
            if (sb_file_read(path, &r->bytes) < 0)
                r->error = "Cannot load installed start page";
            else
                r->state = 1;
            r->type = "text/html; charset=utf-8";
            return r;
        }
        if (key.compare(0, 5, "file:") == 0 || (!key.empty() && key[0] == '/')) {
            const char *p = key.c_str();
            if (!strncmp(p, "file://", 7))
                p += 7;
            else if (!strncmp(p, "file:", 5))
                p += 5;
            if (sb_file_read(p, &r->bytes) < 0)
                r->error = "Local file could not be opened";
            else
                r->state = 1;
            r->type = "text/html; charset=utf-8";
            return r;
        }
        if (!safe_url(key)) {
            r->error = "Unsupported URL";
            return r;
        }
        unsigned id = ++sequence;
        SbBuffer req = {};
        sb_buffer_append(&req, key.c_str(), key.size() + 1);
        sb_buffer_append(&req, post.data(), post.size());
        if (req.length > SB_CHUNK) {
            r->error = "Form exceeds IPC request limit";
            sb_buffer_reset(&req);
            return r;
        }
        int status;
        while ((status = sb_send(channel, SB_FETCH, id, epoch, !post.empty(), 0, 0, 0, req.data,
                                 req.length)) == SC32_E_AGAIN)
            sb_wait(channel, SC32_BROKER_READY_WRITE, 1);
        sb_buffer_reset(&req);
        if (status < 0) {
            quit = 1;
            return r;
        }
        unsigned expected = 0;
        while (!quit && !cancelled) {
            ScIpcPacket meta;
            int n = sb_receive(channel, packet, &meta);
            if (n == SC32_E_AGAIN) {
                sb_wait(channel, SC32_BROKER_READY_READ, 2);
                continue;
            }
            if (n < 0) {
                quit = 1;
                break;
            }
            SbWire h = packet->h;
            if (h.epoch != epoch || h.id != id) {
                ancillary(h, packet->body, n);
                continue;
            }
            if (h.kind == SB_HEADERS) {
                r->status = h.a;
                r->secure = h.b;
                expected = h.c;
                const char *p = (char *)packet->body;
                const char *z = (const char *)memchr(p, 0, n);
                if (z && memchr(z + 1, 0, n - (z + 1 - p))) {
                    r->url = p;
                    r->type = z + 1;
                }
                if (expected > SB_RESOURCE_MAX)
                    break;
            } else if (h.kind == SB_DATA) {
                if (h.a != r->bytes.length || resource_bytes + (unsigned)n > 24u * 1024u * 1024u ||
                    sb_buffer_append(&r->bytes, packet->body, n)) {
                    r->error = "Resource memory limit reached";
                    break;
                }
                resource_bytes += n;
            } else if (h.kind == SB_DONE) {
                if (r->bytes.length == expected) {
                    r->state = 1;
                    if (!r->bytes.data)
                        sb_buffer_append(&r->bytes, "", 0);
                } else
                    r->error = "Incomplete resource transfer";
                break;
            } else if (h.kind == SB_ERROR) {
                if (n && packet->body[n - 1] == 0)
                    r->error = (char *)packet->body;
                break;
            } else
                ancillary(h, packet->body, n);
        }
        if (r->state != 1 && r->error.empty())
            r->error = cancelled ? "Navigation cancelled" : "Resource request failed";
        return r;
    }
    uint_ptr create_font(const char *, int pixels, int weight, font_style italic,
                         unsigned decoration, font_metrics *fm) override {
        auto *f = new SbFont{std::max(6, std::min(64, pixels * zoom / 100)), weight >= 600,
                             italic == font_style_italic, (decoration & 1) != 0};
        fm->height = f->pixels + 3;
        fm->ascent = f->pixels - f->pixels / 5;
        fm->descent = f->pixels / 5 + 3;
        fm->x_height = f->pixels / 2;
        return (uint_ptr)f;
    }
    void delete_font(uint_ptr f) override {
        delete (SbFont *)f;
    }
    int text_width(const char *t, uint_ptr f) override {
        return f ? sb_text_width(t, *(SbFont *)f) : 0;
    }
    void draw_text(uint_ptr, const char *t, uint_ptr f, web_color c, const position &p) override {
        if (f)
            sb_text(&canvas, p.x, p.y, t, *(SbFont *)f, color(c));
    }
    int pt_to_px(int pt) const override {
        return pt * 4 / 3;
    }
    int get_default_font_size() const override {
        return 16;
    }
    const char *get_default_font_name() const override {
        return "Tahoma";
    }
    void draw_list_marker(uint_ptr, const list_marker &m) override {
        int x = m.pos.x, y = m.pos.y, w = m.pos.width, h = m.pos.height;
        if (!m.image.empty()) {
            draw_image(m.image.c_str(), m.baseurl, x, y, w, h);
            return;
        }
        if (m.marker_type == list_style_type_square)
            sb_fill(&canvas, x, y, w, h, color(m.color));
        else if (m.marker_type == list_style_type_circle) {
            sb_circle(&canvas, x + w / 2, y + h / 2, w / 2, color(m.color));
            sb_circle(&canvas, x + w / 2, y + h / 2, std::max(0, w / 2 - 1), 0xffffff);
        } else
            sb_circle(&canvas, x + w / 2, y + h / 2, std::max(1, w / 2), color(m.color));
    }
    void load_image(const char *s, const char *b, bool) override {
        string key = resolve_url(s, b);
        if (key.empty())
            return;
        auto r = resource(key);
        if (r->state != 1 || r->width || r->bytes.length == 0)
            return;
        if (r->bytes.length >= 6 && !memcmp(r->bytes.data, "GIF8", 4)) {
            int w = 0, h = 0;
            unsigned char *rgba = sb_gif(r->bytes.data, r->bytes.length, &w, &h);
            if (rgba && w > 0 && h > 0 && w <= 1920 && h <= 1080 &&
                resource_bytes + (unsigned)w * h * 4 < 32u * 1024u * 1024u) {
                r->pixels.resize(w * h);
                for (int i = 0; i < w * h; i++)
                    r->pixels[i] = ((uint32_t)rgba[i * 4 + 3] << 24) |
                                   ((uint32_t)rgba[i * 4] << 16) |
                                   ((uint32_t)rgba[i * 4 + 1] << 8) | rgba[i * 4 + 2];
                r->width = w;
                r->height = h;
                resource_bytes += w * h * 4;
            } else
                r->width = -1;
            free(rgba);
            return;
        }
        ScImageMemoryDesc d;
        sb_record(&d, sizeof(d));
        d.data = r->bytes.data;
        d.bytes = r->bytes.length;
        ScImageRequest request = 0;
        int status = ScImageDecodeMemoryBegin(&d, &request);
        if (status < 0) {
            r->width = -1;
            return;
        }
        ScImageInfo info;
        unsigned began = sb_ticks();
        do {
            sb_record(&info, sizeof(info));
            status = ScImageDecodeStep(request, 65536, &info);
            if (status < 0 || quit || cancelled || sb_ticks() - began > 1000)
                break;
            if (info.flags & SC32_IMAGE_READY)
                break;
            uint32_t events;
            ScSystemWait(SC32_EVENT_IMAGE, 1, &events);
        } while (1);
        if (status >= 0 && (info.flags & SC32_IMAGE_READY) && info.width && info.height &&
            info.bytes <= 8u * 1024u * 1024u && resource_bytes + info.bytes < 32u * 1024u * 1024u) {
            r->pixels.resize(info.width * info.height);
            sb_record(&info, sizeof(info));
            if (ScImageDecodeCopy(request, r->pixels.data(), r->pixels.size() * 4, &info) >= 0) {
                r->width = info.width;
                r->height = info.height;
                resource_bytes += info.bytes;
            } else
                r->pixels.clear();
        }
        ScImageDecodeCancel(request);
        if (!r->width)
            r->width = -1;
    }
    void get_image_size(const char *s, const char *b, litehtml::size &sz) override {
        auto it = cache.find(resolve_url(s, b));
        sz.width = sz.height = 0;
        if (it != cache.end() && it->second->width > 0) {
            sz.width = it->second->width;
            sz.height = it->second->height;
        }
    }
    void draw_image(const char *s, const char *b, int x, int y, int w, int h) {
        auto it = cache.find(resolve_url(s, b));
        if (it == cache.end() || it->second->width <= 0)
            return;
        auto &r = *it->second;
        sb_blit(&canvas, x, y, w, h, r.pixels.data(), r.width, r.height, r.width);
    }
    void draw_background(uint_ptr, const std::vector<background_paint> &bg) override {
        for (auto i = bg.rbegin(); i != bg.rend(); ++i) {
            auto &b = *i;
            if (b.color.alpha)
                sb_fill(&canvas, b.clip_box.x, b.clip_box.y, b.clip_box.width, b.clip_box.height,
                        color(b.color));
            if (b.image.empty() || b.image_size.width <= 0 || b.image_size.height <= 0)
                continue;
            SbCanvas old = canvas;
            canvas.x0 = std::max(canvas.x0, b.clip_box.x);
            canvas.y0 = std::max(canvas.y0, b.clip_box.y);
            canvas.x1 = std::min(canvas.x1, b.clip_box.right());
            canvas.y1 = std::min(canvas.y1, b.clip_box.bottom());
            int w = b.image_size.width, h = b.image_size.height;
            int x = b.position_x, y = b.position_y;
            bool rx =
                     b.repeat == background_repeat_repeat || b.repeat == background_repeat_repeat_x,
                 ry =
                     b.repeat == background_repeat_repeat || b.repeat == background_repeat_repeat_y;
            if (rx)
                while (x > canvas.x0)
                    x -= w;
            if (ry)
                while (y > canvas.y0)
                    y -= h;
            int maxx = rx ? canvas.x1 : x + 1, maxy = ry ? canvas.y1 : y + 1;
            for (int yy = y; yy < maxy; yy += h)
                for (int xx = x; xx < maxx; xx += w)
                    draw_image(b.image.c_str(), b.baseurl.c_str(), xx, yy, w, h);
            canvas = old;
        }
    }
    void draw_borders(uint_ptr, const borders &b, const position &p, bool) override {
        const border *sides[] = {&b.top, &b.right, &b.bottom, &b.left};
        for (int i = 0; i < 4; i++) {
            auto &s = *sides[i];
            if (s.style == border_style_none || s.style == border_style_hidden || s.width <= 0)
                continue;
            int x = p.x, y = p.y, w = p.width, h = p.height;
            if (i == 0)
                h = s.width;
            if (i == 1) {
                x = p.right() - s.width;
                w = s.width;
            }
            if (i == 2) {
                y = p.bottom() - s.width;
                h = s.width;
            }
            if (i == 3)
                w = s.width;
            if (s.style == border_style_dashed || s.style == border_style_dotted) {
                int unit = s.style == border_style_dotted ? s.width : s.width * 3;
                for (int k = 0; k < (i % 2 ? h : w); k += unit * 2)
                    sb_fill(&canvas, x + (i % 2 ? 0 : k), y + (i % 2 ? k : 0),
                            i % 2 ? w : std::min(unit, w - k), i % 2 ? std::min(unit, h - k) : h,
                            color(s.color));
            } else
                sb_fill(&canvas, x, y, w, h, color(s.color));
        }
    }
    void set_caption(const char *s) override {
        say(SB_TITLE, s ? s : "");
    }
    void set_base_url(const char *s) override {
        string r = resolve_url(s);
        if (!r.empty())
            base = r;
    }
    void link(const document::ptr &, const element::ptr &) override {}
    void on_anchor_click(const char *s, const element::ptr &) override {
        string target = resolve_url(s);
        if (!target.empty())
            say(SB_LINK, target);
    }
    void set_cursor(const char *s) override {
        say(SB_STATUS, "",
            !strcmp(s, "pointer") ? SC32_CURSOR_HAND
            : !strcmp(s, "text")  ? SC32_CURSOR_TEXT
                                  : SC32_CURSOR_ARROW);
    }
    void transform_text(string &s, text_transform t) override {
        bool start = true;
        for (char &c : s) {
            if ((unsigned char)c >= 128)
                continue;
            if (t == text_transform_uppercase)
                c = toupper(c);
            if (t == text_transform_lowercase)
                c = tolower(c);
            if (t == text_transform_capitalize) {
                if (start)
                    c = toupper(c);
                start = isspace(c);
            }
        }
    }
    void import_css(string &text, const string &u, string &b) override {
        string key = resolve_url(u.c_str(), b.c_str());
        if (key.empty())
            return;
        auto r = resource(key);
        if (r->state == 1) {
            text.assign((char *)r->bytes.data, r->bytes.length);
            b = r->url;
        }
    }
    std::vector<SbCanvas> clips;
    void set_clip(const position &p, const border_radiuses &) override {
        clips.push_back(canvas);
        canvas.x0 = std::max(canvas.x0, p.x);
        canvas.y0 = std::max(canvas.y0, p.y);
        canvas.x1 = std::min(canvas.x1, p.right());
        canvas.y1 = std::min(canvas.y1, p.bottom());
    }
    void del_clip() override {
        if (!clips.empty()) {
            canvas = clips.back();
            clips.pop_back();
        }
    }
    void get_client_rect(position &p) const override {
        p.x = p.y = 0;
        p.width = width;
        p.height = height;
    }
    element::ptr create_element(const char *tag, const string_map &,
                                const document::ptr &d) override {
        if (!strcmp(tag, "input") || !strcmp(tag, "textarea") || !strcmp(tag, "select") ||
            !strcmp(tag, "button"))
            return std::make_shared<Control>(d);
        return nullptr;
    }
    void get_media_features(media_features &m) const override {
        m.type = media_type_screen;
        m.width = width;
        m.height = height;
        m.device_width = 1920;
        m.device_height = 1080;
        m.color = 8;
        m.resolution = 96;
    }
    void get_language(string &l, string &c) const override {
        l = "en";
        c = "US";
    }
    void repaint() {
        dirty = 1;
        if (changed && doc) {
            doc->rebuild();
            changed = false;
        }
        if (doc)
            doc->render(width);
    }
    void draw_controls(const element::ptr &e) {
        if (!e)
            return;
        string tag = e->get_tagName();
        if ((tag == "input" || tag == "textarea" || tag == "select" || tag == "button") &&
            e->css().get_display() != display_none) {
            position p = e->get_placement();
            p.x -= scrollx;
            p.y -= scrolly;
            string type = e->get_attr("type", "text"), value = e->get_attr("value", "");
            SbCanvas old = canvas;
            canvas.x0 = std::max(canvas.x0, p.x);
            canvas.y0 = std::max(canvas.y0, p.y);
            canvas.x1 = std::min(canvas.x1, p.right());
            canvas.y1 = std::min(canvas.y1, p.bottom());
            if (tag == "textarea" && !e->get_attr("value")) {
                value.clear();
                e->get_text(value);
            }
            if (tag == "select") {
                auto opts = selected(e, "option");
                if (!opts.empty()) {
                    int idx = atoi(e->get_attr("_sbindex", "0"));
                    idx = std::max(0, std::min((int)opts.size() - 1, idx));
                    value.clear();
                    opts[idx]->get_text(value);
                    sb_fill(&canvas, p.right() - 18, p.y, 18, p.height, 0xc0c0c0);
                    sb_bevel(&canvas, p.right() - 18, p.y, 18, p.height, 0);
                    sb_text(&canvas, p.right() - 14, p.y + 2, "v", {12, 0, 0, 0}, 0);
                }
            }
            if (tag == "button") {
                value.clear();
                e->get_text(value);
            }
            if (type == "checkbox" || type == "radio") {
                if (!strcmp(e->get_attr("_sbchecked", e->get_attr("checked") ? "1" : "0"), "1")) {
                    sb_line(&canvas, p.x + 2, p.y + 6, p.x + 6, p.y + 10, 0);
                    sb_line(&canvas, p.x + 6, p.y + 10, p.x + 13, p.y + 2, 0);
                }
            } else {
                if (type == "password")
                    value = string(value.size(), '*');
                if (value.empty() && (type == "submit" || type == "reset"))
                    value = type == "submit" ? "Submit" : "Reset";
                sb_text(&canvas, p.x + 2, p.y + 2, value.c_str(), {14, 0, 0, 0}, 0);
                if (e == focus) {
                    int w = sb_text_width(value.c_str(), {14, 0, 0, 0});
                    sb_fill(&canvas, p.x + 3 + w, p.y + 2, 1, 16, 0);
                }
            }
            canvas = old;
        }
        for (auto &child : e->children())
            draw_controls(child);
    }
    void frame() {
        if (!surface || !doc || frame_pending || !dirty)
            return;
        repaint();
        scrollx = std::max(0, std::min(scrollx, std::max(0, doc->content_width() - width)));
        scrolly = std::max(0, std::min(scrolly, std::max(0, doc->content_height() - height)));
        canvas = {(uint32_t *)((unsigned char *)surface + 4096),
                  width,
                  height,
                  width,
                  0,
                  0,
                  width,
                  height};
        sb_fill(&canvas, 0, 0, width, height, 0xffffff);
        position clip(0, 0, width, height);
        doc->draw(0, -scrollx, -scrolly, &clip);
        draw_controls(doc->root());
        surface->width = width;
        surface->height = height;
        surface->document_width = doc->content_width();
        surface->document_height = doc->content_height();
        surface->scroll_x = scrollx;
        surface->scroll_y = scrolly;
        surface->generation = ++frame_sequence;
        __asm__ volatile("" ::: "memory");
        int status = sb_send(channel, SB_FRAME, frame_sequence, epoch, width, height,
                             surface->document_width, surface->document_height, 0, 0);
        if (status >= 0) {
            frame_pending = 1;
            dirty = 0;
        }
    }
    static void *allocate(void *u, duk_size_t n) {
        Page *p = (Page *)u;
        if (!n || n > 8u * 1024u * 1024u - p->js_bytes)
            return nullptr;
        size_t *q = (size_t *)malloc(n + 4 * sizeof(size_t));
        if (!q)
            return nullptr;
        *q = n;
        p->js_bytes += n;
        return q + 4;
    }
    static void release(void *u, void *v) {
        if (v) {
            Page *p = (Page *)u;
            size_t *q = (size_t *)v - 4;
            p->js_bytes -= *q;
            free(q);
        }
    }
    static void *resize(void *u, void *v, duk_size_t n) {
        if (!v)
            return allocate(u, n);
        if (!n) {
            release(u, v);
            return nullptr;
        }
        Page *p = (Page *)u;
        size_t *q = (size_t *)v - 4, old = *q;
        if (n > old && n - old > 8u * 1024u * 1024u - p->js_bytes)
            return nullptr;
        size_t *r = (size_t *)realloc(q, n + 4 * sizeof(size_t));
        if (!r)
            return nullptr;
        *r = n;
        p->js_bytes = p->js_bytes - old + n;
        return r + 4;
    }
    void init_js();
    void run_scripts();
    void run_script(const string &s);
    bool event(const element::ptr &e, const char *name);
    void submit(const element::ptr &e);
    void input(const SbWire &h, const unsigned char *b, unsigned n);
    void dispatch(const SbWire &h, const unsigned char *b, unsigned n);
    void navigate();
    unsigned node_index(const element::ptr &e) {
        for (unsigned i = 0; i < nodes.size(); i++)
            if (nodes[i] == e)
                return i;
        nodes.push_back(e);
        return nodes.size() - 1;
    }
    void wrap(const element::ptr &e);
    static element::ptr this_node(duk_context *c);
    static duk_ret_t method(duk_context *c);
    static duk_ret_t get(duk_context *c);
    static duk_ret_t set(duk_context *c);
    static duk_ret_t global(duk_context *c);
    static duk_ret_t location(duk_context *c);
    static duk_ret_t style_set(duk_context *c);
};
extern "C" int sb_js_timeout(void *u) {
    Page *p = (Page *)u;
    return p->quit || p->cancelled || (int32_t)(sb_ticks() - p->deadline) >= 0;
}
extern "C" void sb_fatal(void *, const char *);
static void accessor(duk_context *c, const char *key, duk_c_function getter, duk_c_function setter,
                     int magic) {
    duk_push_string(c, key);
    if (getter) {
        duk_push_c_function(c, getter, 0);
        duk_set_magic(c, -1, magic);
    }
    if (setter) {
        duk_push_c_function(c, setter, 1);
        duk_set_magic(c, -1, magic);
    }
    duk_def_prop(c, -(2 + !!getter + !!setter),
                 DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE |
                     (getter ? DUK_DEFPROP_HAVE_GETTER : 0) |
                     (setter ? DUK_DEFPROP_HAVE_SETTER : 0));
}
static void function(duk_context *c, const char *key, duk_c_function fn, int args, int magic) {
    duk_push_c_function(c, fn, args);
    duk_set_magic(c, -1, magic);
    duk_put_prop_string(c, -2, key);
}
element::ptr Page::this_node(duk_context *c) {
    duk_push_this(c);
    duk_get_prop_string(c, -1,
                        "\xff"
                        "node");
    unsigned i = duk_get_uint_default(c, -1, ~0u);
    duk_pop_2(c);
    return active && i < active->nodes.size() ? active->nodes[i] : nullptr;
}
void Page::wrap(const element::ptr &e) {
    if (!e) {
        duk_push_null(js);
        return;
    }
    unsigned i = node_index(e);
    char key[24];
    snprintf(key, sizeof(key), "node%u", i);
    duk_push_heap_stash(js);
    if (duk_get_prop_string(js, -1, key)) {
        duk_remove(js, -2);
        return;
    }
    duk_pop(js);
    duk_push_object(js);
    duk_push_uint(js, i);
    duk_put_prop_string(js, -2,
                        "\xff"
                        "node");
    const char *props[] = {"id",        "className", "value",   "textContent", "innerText",
                           "innerHTML", "checked",   "tagName", "parentNode"};
    for (int k = 0; k < 9; k++)
        accessor(js, props[k], get, k == 7 || k == 8 ? nullptr : set, k);
    function(js, "setAttribute", method, 2, 1);
    function(js, "getAttribute", method, 1, 2);
    function(js, "appendChild", method, 1, 3);
    function(js, "removeChild", method, 1, 4);
    function(js, "addEventListener", method, 2, 5);
    function(js, "focus", method, 0, 6);
    function(js, "submit", method, 0, 7);
    function(js, "querySelector", method, 1, 8);
    function(js, "querySelectorAll", method, 1, 9);
    duk_push_object(js);
    duk_push_uint(js, i);
    duk_put_prop_string(js, -2,
                        "\xff"
                        "node");
    const char *styles[] = {"color",   "backgroundColor", "display",    "width",
                            "height",  "fontSize",        "visibility", "margin",
                            "padding", "border",          "cssText"};
    for (int k = 0; k < 11; k++)
        accessor(js, styles[k], nullptr, style_set, k);
    duk_put_prop_string(js, -2, "style");
    duk_dup(js, -1);
    duk_put_prop_string(js, -3, key);
    duk_remove(js, -2);
}
duk_ret_t Page::get(duk_context *c) {
    auto e = this_node(c);
    if (!e) {
        duk_push_undefined(c);
        return 1;
    }
    int k = duk_get_current_magic(c);
    if (k == 8) {
        active->wrap(e->parent());
        return 1;
    }
    if (k == 6) {
        duk_push_boolean(
            c, !strcmp(e->get_attr("_sbchecked", e->get_attr("checked") ? "1" : "0"), "1"));
        return 1;
    }
    if (k == 3 || k == 4 || k == 5) {
        string s;
        if (k == 5)
            s = serialize(e, true);
        else
            e->get_text(s);
        duk_push_lstring(c, s.data(), s.size());
        return 1;
    }
    if (k == 2 && !strcmp(e->get_tagName(), "textarea") && !e->get_attr("value")) {
        string text;
        e->get_text(text);
        duk_push_string(c, text.c_str());
        return 1;
    }
    if (k == 2 && !strcmp(e->get_tagName(), "select")) {
        auto opts = selected(e, "option");
        unsigned idx = strtoul(e->get_attr("_sbindex", "0"), 0, 10);
        if (idx < opts.size()) {
            string value = opts[idx]->get_attr("value", "");
            if (!opts[idx]->get_attr("value"))
                opts[idx]->get_text(value);
            duk_push_string(c, value.c_str());
            return 1;
        }
    }
    const char *names[] = {"id", "class", "value"};
    duk_push_string(c, k == 7 ? e->get_tagName() : e->get_attr(names[k], ""));
    return 1;
}
duk_ret_t Page::set(duk_context *c) {
    auto e = this_node(c);
    if (!e)
        return 0;
    int k = duk_get_current_magic(c);
    const char *s = duk_safe_to_string(c, 0);
    if (k <= 2) {
        const char *names[] = {"id", "class", "value"};
        e->set_attr(names[k], s);
        if (k == 2 && !strcmp(e->get_tagName(), "select")) {
            auto opts = selected(e, "option");
            for (unsigned i = 0; i < opts.size(); i++) {
                string value = opts[i]->get_attr("value", "");
                if (!opts[i]->get_attr("value"))
                    opts[i]->get_text(value);
                if (value == s) {
                    char v[16];
                    snprintf(v, sizeof(v), "%u", i);
                    e->set_attr("_sbindex", v);
                    break;
                }
            }
        }
    } else if (k == 6)
        e->set_attr("_sbchecked", duk_to_boolean(c, 0) ? "1" : "0");
    else if (k == 5) {
        e->clearRecursive();
        active->doc->append_children_from_string(*e, s);
    } else {
        e->clearRecursive();
        e->appendChild(std::make_shared<el_text>(s, active->doc));
    }
    active->changed = active->dirty = true;
    return 0;
}
duk_ret_t Page::style_set(duk_context *c) {
    auto e = this_node(c);
    if (!e)
        return 0;
    const char *names[] = {"color",     "background-color", "display", "width",   "height",
                           "font-size", "visibility",       "margin",  "padding", "border",
                           ""};
    int k = duk_get_current_magic(c);
    string css = e->get_attr("style", "");
    if (k == 10)
        css = duk_safe_to_string(c, 0);
    else
        css += ";" + string(names[k]) + ":" + duk_safe_to_string(c, 0) + ";";
    e->set_attr("style", css.c_str());
    litehtml::style st;
    st.add(css);
    e->add_style(st);
    active->changed = active->dirty = true;
    return 0;
}
duk_ret_t Page::method(duk_context *c) {
    auto e = this_node(c);
    if (!e)
        return 0;
    int k = duk_get_current_magic(c);
    if (k == 1) {
        e->set_attr(duk_require_string(c, 0), duk_safe_to_string(c, 1));
        active->changed = active->dirty = true;
    } else if (k == 2) {
        const char *s = e->get_attr(duk_require_string(c, 0));
        if (s)
            duk_push_string(c, s);
        else
            duk_push_null(c);
        return 1;
    } else if (k == 3 || k == 4) {
        duk_get_prop_string(c, 0,
                            "\xff"
                            "node");
        unsigned n = duk_get_uint_default(c, -1, ~0u);
        duk_pop(c);
        if (n < active->nodes.size()) {
            if (k == 3) {
                auto child = active->nodes[n];
                if (child != e && !child->is_ancestor(e)) {
                    if (child->parent())
                        child->parent()->removeChild(child);
                    e->appendChild(child);
                }
            } else
                e->removeChild(active->nodes[n]);
            active->changed = active->dirty = true;
        }
        duk_dup(c, 0);
        return 1;
    } else if (k == 5) {
        string name = "on" + string(duk_require_string(c, 0));
        duk_push_this(c);
        duk_dup(c, 1);
        duk_put_prop_string(c, -2, name.c_str());
        duk_pop(c);
    } else if (k == 6) {
        active->focus = e;
        active->dirty = 1;
    } else if (k == 7)
        active->submit(e);
    else if (k == 8) {
        active->wrap(e->select_one(duk_require_string(c, 0)));
        return 1;
    } else if (k == 9) {
        auto list = selected(e, duk_require_string(c, 0));
        duk_push_array(c);
        for (unsigned i = 0; i < list.size(); i++) {
            active->wrap(list[i]);
            duk_put_prop_index(c, -2, i);
        }
        return 1;
    }
    return 0;
}
duk_ret_t Page::location(duk_context *c) {
    int magic = duk_get_current_magic(c);
    if (magic == 3) {
        active->say(SB_LINK, active->url, 1);
        return 0;
    }
    if (duk_get_top(c) == 0) {
        if (magic == 0) {
            duk_push_heap_stash(c);
            duk_get_prop_string(c, -1, "location");
            duk_remove(c, -2);
            return 1;
        }
        duk_push_string(c, active->url.c_str());
        return 1;
    }
    string s = active->resolve_url(duk_safe_to_string(c, 0));
    if (!s.empty())
        active->say(SB_LINK, s);
    return 0;
}
duk_ret_t Page::global(duk_context *c) {
    Page *p = active;
    int k = duk_get_current_magic(c);
    if (k == 1) {
        p->wrap(find_id(p->doc->root(), duk_require_string(c, 0)));
        return 1;
    }
    if (k == 2) {
        p->wrap(p->doc->root()->select_one(duk_require_string(c, 0)));
        return 1;
    }
    if (k == 3 || k == 4) {
        string selector = duk_require_string(c, 0);
        if (k == 4)
            selector = "." + selector;
        auto list = selected(p->doc->root(), selector);
        duk_push_array(c);
        for (unsigned i = 0; i < list.size(); i++) {
            p->wrap(list[i]);
            duk_put_prop_index(c, -2, i);
        }
        return 1;
    }
    if (k == 5) {
        auto e = p->doc->create_element(duk_require_string(c, 0), {});
        p->wrap(e);
        return 1;
    }
    if (k == 6) {
        p->wrap(std::make_shared<el_text>(duk_safe_to_string(c, 0), p->doc));
        return 1;
    }
    if (k == 7) {
        p->say(SB_ALERT, duk_safe_to_string(c, 0));
        return 0;
    }
    if (k == 8 || k == 9) {
        if (p->timers.size() >= 64)
            return duk_error(c, DUK_ERR_RANGE_ERROR, "Timer limit reached");
        if (!duk_is_callable(c, 0) && !duk_is_string(c, 0))
            return duk_error(c, DUK_ERR_TYPE_ERROR, "Expected timer callback");
        unsigned delay = std::max(1u, std::min(60000u, duk_to_uint(c, 1))) / 10;
        unsigned id = ++p->timer_id;
        duk_push_heap_stash(c);
        duk_dup(c, 0);
        char name[32];
        snprintf(name, sizeof(name), "timer%u", id);
        duk_put_prop_string(c, -2, name);
        duk_pop(c);
        p->timers.push_back(
            {id, sb_ticks() + std::max(1u, delay), k == 9 ? std::max(1u, delay) : 0, nullptr});
        duk_push_uint(c, id);
        return 1;
    }
    if (k == 10) {
        unsigned id = duk_to_uint(c, 0);
        p->timers.erase(std::remove_if(p->timers.begin(), p->timers.end(),
                                       [id](const Timer &t) { return t.id == id; }),
                        p->timers.end());
        duk_push_heap_stash(c);
        char key[32];
        snprintf(key, sizeof(key), "timer%u", id);
        duk_del_prop_string(c, -1, key);
        duk_pop(c);
        return 0;
    }
    if (k == 11) {
        p->say(SB_STATUS, duk_safe_to_string(c, 0));
        return 0;
    }
    if (k == 12) {
        string name = "on" + string(duk_require_string(c, 0));
        duk_push_global_object(c);
        duk_dup(c, 1);
        duk_put_prop_string(c, -2, name.c_str());
        duk_pop(c);
        return 0;
    }
    if (k == 13) {
        duk_push_string(c, p->url.c_str());
        return 1;
    }
    if (k == 14) {
        duk_push_string(c, "SandBrowser");
        return 1;
    }
    if (k == 16) {
        const char *s = duk_safe_to_string(c, 0);
        p->set_caption(s);
        return 0;
    }
    if (k == 15) {
        auto body = p->doc->root()->select_one("body");
        if (body)
            p->doc->append_children_from_string(*body, duk_safe_to_string(c, 0));
        p->changed = p->dirty = 1;
        return 0;
    }
    return 0;
}
void Page::init_js() {
    if (js)
        duk_destroy_heap(js);
    js = nullptr;
    nodes.clear();
    timers.clear();
    js_bytes = 0;
    if (!js_enabled)
        return;
    js = duk_create_heap(allocate, resize, release, this, sb_fatal);
    if (!js) {
        say(SB_STATUS, "JavaScript heap allocation failed");
        return;
    }
    duk_push_global_object(js);
    duk_dup(js, -1);
    duk_put_prop_string(js, -2, "window");
    duk_dup(js, -1);
    duk_put_prop_string(js, -2, "self");
    function(js, "alert", global, 1, 7);
    function(js, "setTimeout", global, 2, 8);
    function(js, "setInterval", global, 2, 9);
    function(js, "clearTimeout", global, 1, 10);
    function(js, "clearInterval", global, 1, 10);
    function(js, "addEventListener", global, 2, 12);
    duk_push_heap_stash(js);
    duk_push_object(js);
    accessor(js, "href", location, location, 1);
    function(js, "assign", location, 1, 2);
    function(js, "reload", location, 0, 3);
    function(js, "toString", location, 0, 1);
    duk_put_prop_string(js, -2, "location");
    duk_pop(js);
    accessor(js, "location", location, location, 0);
    duk_push_object(js);
    function(js, "log", global, DUK_VARARGS, 11);
    function(js, "error", global, DUK_VARARGS, 11);
    duk_put_prop_string(js, -2, "console");
    duk_push_object(js);
    duk_push_string(js, "SandBrowser/" SB_VERSION);
    duk_put_prop_string(js, -2, "userAgent");
    duk_put_prop_string(js, -2, "navigator");
    duk_push_object(js);
    function(js, "getElementById", global, 1, 1);
    accessor(js, "location", location, location, 0);
    function(js, "querySelector", global, 1, 2);
    function(js, "querySelectorAll", global, 1, 3);
    function(js, "getElementsByTagName", global, 1, 3);
    function(js, "getElementsByClassName", global, 1, 4);
    function(js, "createElement", global, 1, 5);
    function(js, "createTextNode", global, 1, 6);
    function(js, "write", global, 1, 15);
    function(js, "addEventListener", global, 2, 12);
    duk_push_string(js, "complete");
    duk_put_prop_string(js, -2, "readyState");
    wrap(doc->root()->select_one("body"));
    duk_put_prop_string(js, -2, "body");
    wrap(doc->root());
    duk_put_prop_string(js, -2, "documentElement");
    accessor(js, "URL", global, nullptr, 13);
    accessor(js, "title", nullptr, global, 16);
    duk_put_prop_string(js, -2, "document");
    duk_pop(js);
}
void Page::run_script(const string &s) {
    if (!js || s.size() > 1024 * 1024)
        return;
    deadline = sb_ticks() + 150;
    int top = duk_get_top(js);
    if (duk_peval_lstring(js, s.data(), s.size()))
        say(SB_STATUS, string("Script: ") + duk_safe_to_string(js, -1));
    duk_set_top(js, top);
    if (changed)
        repaint();
}
bool Page::event(const element::ptr &e, const char *name) {
    if (!js || !e)
        return true;
    int top = duk_get_top(js);
    bool allowed = true;
    wrap(e);
    if (duk_get_prop_string(js, -1, name) && duk_is_callable(js, -1)) {
        duk_dup(js, -2);
        duk_push_object(js);
        duk_push_string(js, name + 2);
        duk_put_prop_string(js, -2, "type");
        wrap(e);
        duk_put_prop_string(js, -2, "target");
        deadline = sb_ticks() + 150;
        if (duk_pcall_method(js, 1))
            say(SB_STATUS, string("Event: ") + duk_safe_to_string(js, -1));
        else if (duk_is_boolean(js, -1) && !duk_get_boolean(js, -1))
            allowed = false;
    }
    duk_set_top(js, top);
    const char *code = e->get_attr(name);
    if (code && *code) {
        string script = "(function(event){" + string(code) + "})";
        deadline = sb_ticks() + 150;
        if (!duk_peval_lstring(js, script.data(), script.size())) {
            wrap(e);
            duk_push_object(js);
            if (duk_pcall_method(js, 1))
                say(SB_STATUS, string("Event: ") + duk_safe_to_string(js, -1));
            else if (duk_is_boolean(js, -1) && !duk_get_boolean(js, -1))
                allowed = false;
        }
        duk_set_top(js, top);
    }
    if (changed)
        repaint();
    return allowed;
}
void Page::run_scripts() {
    if (!js)
        return;
    auto scripts = doc->root()->select_all("script");
    for (auto &e : scripts) {
        if (quit || cancelled)
            break;
        const char *type = e->get_attr("type", "");
        if (*type && strcasecmp(type, "text/javascript") &&
            strcasecmp(type, "application/javascript"))
            continue;
        string code;
        const char *src = e->get_attr("src");
        if (src) {
            auto r = resource(resolve_url(src));
            if (r->state == 1)
                code.assign((char *)r->bytes.data, r->bytes.length);
        } else
            e->get_text(code);
        run_script(code);
    }
    event(doc->root()->select_one("body"), "onload");
    run_script("if(typeof onload==='function')onload(); if(typeof "
               "onDOMContentLoaded==='function')onDOMContentLoaded();");
}
void Page::submit(const element::ptr &el) {
    auto form = el;
    while (form && strcmp(form->get_tagName(), "form"))
        form = form->parent();
    if (!form || !event(form, "onsubmit"))
        return;
    string data;
    auto fields = selected(form, "input,textarea,select,button");
    for (auto &e : fields) {
        const char *name = e->get_attr("name");
        if (!name || !*name || e->get_attr("disabled"))
            continue;
        string type = e->get_attr("type", "text");
        if ((type == "submit" || type == "button") && e != el)
            continue;
        if (type == "reset" || type == "file")
            continue;
        if ((type == "checkbox" || type == "radio") &&
            strcmp(e->get_attr("_sbchecked", e->get_attr("checked") ? "1" : "0"), "1"))
            continue;
        string value = e->get_attr("value", type == "checkbox" ? "on" : "");
        if (!strcmp(e->get_tagName(), "textarea") && !e->get_attr("value"))
            e->get_text(value);
        if (!strcmp(e->get_tagName(), "select")) {
            auto opts = selected(e, "option");
            if (!opts.empty()) {
                unsigned idx = strtoul(e->get_attr("_sbindex", "0"), 0, 10);
                if (idx >= opts.size())
                    idx = 0;
                value = opts[idx]->get_attr("value", "");
                if (value.empty())
                    opts[idx]->get_text(value);
            }
        }
        char key[2048], val[8192];
        if (strlen(name) > 600 || value.size() > 2500) {
            say(SB_ALERT, "Form field exceeds this version's size limit.");
            return;
        }
        sb_percent_encode(name, key, sizeof(key), 1);
        sb_percent_encode(value.c_str(), val, sizeof(val), 1);
        if (!data.empty())
            data += '&';
        data += string(key) + "=" + val;
        if (data.size() > 32000) {
            say(SB_ALERT, "Form exceeds the 32 KiB submission limit.");
            return;
        }
    }
    string target = resolve_url(form->get_attr("action", url.c_str()));
    if (!strcasecmp(form->get_attr("method", "GET"), "POST")) {
        SbBuffer b = {};
        sb_buffer_append(&b, target.c_str(), target.size() + 1);
        sb_buffer_append(&b, data.data(), data.size());
        sb_send(channel, SB_FORM_POST, 0, epoch, 0, 0, 0, 0, b.data, b.length);
        sb_buffer_reset(&b);
    } else {
        size_t hash = target.find('#');
        if (hash != string::npos)
            target.resize(hash);
        size_t query = target.find('?');
        if (query != string::npos)
            target.resize(query);
        target += '?';
        target += data;
        if (target.size() >= SB_URL) {
            say(SB_ALERT, "Form URL is too long.");
            return;
        }
        say(SB_LINK, target);
    }
}
void Page::input(const SbWire &h, const unsigned char *b, unsigned n) {
    if (!doc)
        return;
    if (h.kind == SB_POINTER) {
        position::vector boxes;
        int x = h.a, y = h.b;
        if (h.c == 0) {
            doc->on_mouse_over(x + scrollx, y + scrolly, x, y, boxes);
            auto over = doc->get_over_element();
            while (over && strcmp(over->get_tagName(), "a"))
                over = over->parent();
            if (over)
                say(SB_STATUS, resolve_url(over->get_attr("href", "")), SC32_CURSOR_HAND);
            dirty = 1;
        } else if (h.c == 1) {
            doc->on_lbutton_down(x + scrollx, y + scrolly, x, y, boxes);
            focus = std::const_pointer_cast<element>(doc->get_over_element());
            while (focus && strcmp(focus->get_tagName(), "input") &&
                   strcmp(focus->get_tagName(), "textarea") &&
                   strcmp(focus->get_tagName(), "select") && strcmp(focus->get_tagName(), "button"))
                focus = focus->parent();
            dirty = 1;
        } else {
            auto target = std::const_pointer_cast<element>(doc->get_over_element());
            bool allow = true;
            for (auto e = target; e; e = e->parent())
                if (!event(e, "onclick"))
                    allow = false;
            if (allow) {
                if (focus && !focus->get_attr("disabled")) {
                    string tag = focus->get_tagName(), type = focus->get_attr("type", "text");
                    if (type == "checkbox" || type == "radio") {
                        if (type == "radio") {
                            string name = focus->get_attr("name", "");
                            for (auto &e : doc->root()->select_all("input"))
                                if (name == e->get_attr("name", "") &&
                                    !strcmp(e->get_attr("type", ""), "radio"))
                                    e->set_attr("_sbchecked", "0");
                        }
                        focus->set_attr(
                            "_sbchecked",
                            !strcmp(focus->get_attr("_sbchecked",
                                                    focus->get_attr("checked") ? "1" : "0"),
                                    "1") &&
                                    type != "radio"
                                ? "0"
                                : "1");
                        event(focus, "onchange");
                    } else if (tag == "select") {
                        auto opts = selected(focus, "option");
                        if (!opts.empty()) {
                            int i = (atoi(focus->get_attr("_sbindex", "0")) + 1) % opts.size();
                            char index[16];
                            snprintf(index, sizeof(index), "%d", i);
                            focus->set_attr("_sbindex", index);
                            event(focus, "onchange");
                        }
                    } else if (type == "reset") {
                        auto form = focus;
                        while (form && strcmp(form->get_tagName(), "form"))
                            form = form->parent();
                        if (form)
                            for (auto &e : selected(form, "input,textarea,select")) {
                                e->set_attr("value", e->get_attr("_sbdefault", ""));
                                e->set_attr("_sbchecked", e->get_attr("_sbchecked_default", "0"));
                                e->set_attr("_sbindex", e->get_attr("_sbindex_default", "0"));
                            }
                    } else if (type == "submit" || (tag == "button" && type != "button"))
                        submit(focus);
                }
                doc->on_lbutton_up(x + scrollx, y + scrolly, x, y, boxes);
            }
            dirty = 1;
        }
    } else if (h.kind == SB_KEY) {
        unsigned key = h.a, mods = h.b;
        if (key == 9) {
            auto fields = selected(doc->root(), "input,textarea,select,button,a[href]");
            if (!fields.empty()) {
                auto i = std::find(fields.begin(), fields.end(), focus);
                size_t idx =
                    i == fields.end()
                        ? 0
                        : (i - fields.begin() + (mods & SC32_MOD_SHIFT ? fields.size() - 1 : 1)) %
                              fields.size();
                focus = fields[idx];
                scrolly = std::max(0, focus->get_placement().y - height / 2);
            }
            dirty = 1;
        } else if (focus) {
            string tag = focus->get_tagName(), type = focus->get_attr("type", "text");
            if (!focus->get_attr("disabled") && !focus->get_attr("readonly") &&
                (tag == "textarea" || (tag == "input" && type != "submit" && type != "button" &&
                                       type != "checkbox" && type != "radio"))) {
                string value = focus->get_attr("value", "");
                if (tag == "textarea" && !focus->get_attr("value"))
                    focus->get_text(value);
                if (key == 8 && !value.empty()) {
                    size_t cut = value.size() - 1;
                    while (cut && ((unsigned char)value[cut] & 0xc0) == 0x80)
                        cut--;
                    value.resize(cut);
                } else if (key == '\n' && tag != "textarea") {
                    submit(focus);
                    return;
                } else if (n && value.size() + n < 8192)
                    value.append((char *)b, n);
                else if (key == '\n')
                    value += '\n';
                focus->set_attr("value", value.c_str());
                event(focus, "oninput");
                dirty = 1;
            } else if (key == '\n') {
                if (tag == "a")
                    on_anchor_click(focus->get_attr("href", ""), focus);
                else
                    submit(focus);
            }
        }
    }
}
void Page::navigate() {
    if (pending_url.empty())
        return;
    url = pending_url;
    base = url;
    string post = pending_post;
    epoch = pending_epoch;
    pending_url.clear();
    pending_post.clear();
    cancelled = 0;
    scrollx = scrolly = 0;
    focus.reset();
    if (js) {
        duk_destroy_heap(js);
        js = nullptr;
    }
    nodes.clear();
    timers.clear();
    doc.reset();
    cache.clear();
    resource_bytes = 0;
    source.clear();
    say(SB_STATUS, "Opening document...");
    auto r = resource(url, true, post);
    if (cancelled || quit)
        return;
    url = r->url;
    base = url;
    if (r->state != 1) {
        source =
            "<html><head><title>Cannot open page</title></head><body "
            "style='font-family:Tahoma;margin:40px'><h1>This page could not be opened</h1><hr><p>" +
            escape(r->error) + "</p><p>" + escape(url) +
            "</p><p>Check the address, network, and system UTC clock. Certificate errors cannot be "
            "bypassed.</p></body></html>";
        say(SB_HEADERS, url, 0);
    } else {
        source.assign((char *)r->bytes.data, r->bytes.length);
        if (source.size() > 4u * 1024u * 1024u) {
            source = "<h1>Document exceeds 4 MiB HTML limit</h1>";
        }
        say(SB_HEADERS, url, r->secure);
    }
    doc = document::createFromString(source.c_str(), this, litehtml::master_css, form_css);
    if (!doc) {
        say(SB_ERROR, "HTML document creation failed");
        return;
    }
    if (cancelled || quit)
        return;
    doc->render(width);
    init_js();
    run_scripts();
    size_t hash = url.find('#');
    if (hash != string::npos && hash + 1 < url.size()) {
        auto anchor = find_id(doc->root(), url.substr(hash + 1));
        if (anchor)
            scrolly = std::max(0, anchor->get_placement().y);
    }
    dirty = 1;
    say(SB_DONE, "Done", r->secure);
}
void Page::dispatch(const SbWire &h, const unsigned char *b, unsigned n) {
    ancillary(h, b, n);
    if (h.kind == SB_SURFACE && !surface) {
        if (h.a < 4096 + SB_SURFACE_BYTES)
            return;
        if (ScMemoryOpenShared(h.id, &memory) < 0)
            return;
        ScSharedMappingInfo m;
        sb_record(&m, sizeof(m));
        int status;
        unsigned began = sb_ticks();
        while ((status = ScMemoryMapShared(memory, 0, h.a, SC32_SHARED_READ | SC32_SHARED_WRITE,
                                           &m)) == SC32_PENDING &&
               sb_ticks() - began < 1000)
            ScProcessSleep(1);
        if (status >= 0 && m.state == SC32_SHARED_READY) {
            mapping = m.handle;
            surface = (SbSurface *)(uintptr_t)m.address;
            dirty = 1;
        }
    }
    if (h.epoch != epoch)
        return;
    if (h.kind == SB_POINTER || h.kind == SB_KEY)
        input(h, b, n);
    if (h.kind == SB_FIND && doc && n && b[n - 1] == 0) {
        string query = (char *)b;
        bool found = false;
        for (auto &e : selected(doc->root(), "p,td,h1,h2,h3,li,div,span")) {
            string text;
            e->get_text(text);
            if (text.find(query) != string::npos) {
                scrolly = std::max(0, e->get_placement().y - 20);
                dirty = 1;
                found = true;
                break;
            }
        }
        say(SB_STATUS, found ? "Text found" : "Text not found");
    }
    if (h.kind == SB_SAVE && n && b[n - 1] == 0) {
        int s = sb_file_write((char *)b, source.data(), source.size());
        say(SB_ALERT, s < 0 ? "Unable to save source file"
                            : "Page source saved. External resources are not included.");
    }
    if (h.kind == SB_SOURCE) {
        char root[128], path[160];
        sb_base_path(root, sizeof(root));
        snprintf(path, sizeof(path), "%s/SOURCE.HTM", root);
        string html = "<html><head><title>Page source</title></head><body><pre "
                      "style='white-space:pre-wrap'>" +
                      escape(source) + "</pre></body></html>";
        if (sb_file_write(path, html.data(), html.size()) >= 0)
            say(SB_SOURCE, string("file:") + path);
        else
            say(SB_ALERT, "Unable to create the source view file.");
    }
}
extern "C" int SbPageMain(void) {
    Page page;
    active = &page;
    page.channel = sb_worker_connect(SB_ROLE_PAGE, 0);
    if (!page.channel)
        return 1;
    page.packet = (SbPacket *)malloc(sizeof(SbPacket));
    if (!page.packet)
        return 2;
    unsigned timer_tick = 0;
    while (!page.quit) {
        ScIpcPacket meta;
        int n;
        while ((n = sb_receive(page.channel, page.packet, &meta)) >= 0) {
            SbWire h = page.packet->h;
            page.dispatch(h, page.packet->body, n);
        }
        if (n == SC32_E_BROKEN_PIPE)
            break;
        if (!page.pending_url.empty())
            page.navigate();
        if (page.quit)
            break;
        page.frame();
        unsigned now = sb_ticks();
        if (page.js && now != timer_tick) {
            timer_tick = now;
            auto timers = page.timers;
            for (auto &t : timers) {
                auto found = std::find_if(page.timers.begin(), page.timers.end(),
                                          [&](const Timer &x) { return x.id == t.id; });
                if (found == page.timers.end() || (int32_t)(now - t.due) < 0)
                    continue;
                if (t.interval)
                    found->due = now + t.interval;
                else
                    page.timers.erase(found);
                char key[32];
                snprintf(key, sizeof(key), "timer%u", t.id);
                duk_push_heap_stash(page.js);
                duk_get_prop_string(page.js, -1, key);
                if (duk_is_callable(page.js, -1)) {
                    page.deadline = now + 100;
                    if (duk_pcall(page.js, 0))
                        page.say(SB_STATUS, string("Timer: ") + duk_safe_to_string(page.js, -1));
                } else {
                    string code = duk_safe_to_string(page.js, -1);
                    page.run_script(code);
                }
                duk_pop_2(page.js);
                if (!t.interval) {
                    duk_push_heap_stash(page.js);
                    duk_del_prop_string(page.js, -1, key);
                    duk_pop(page.js);
                }
                if (page.changed)
                    page.repaint();
            }
        }
        sb_wait(page.channel, SC32_BROKER_READY_READ, 1);
    }
    ScIpcClose(page.channel);
    return 0;
}
