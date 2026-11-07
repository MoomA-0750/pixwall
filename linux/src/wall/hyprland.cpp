#include "hyprland.h"

#include <glib-unix.h>
#include <glib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "core.h"

namespace pixwall {

namespace {

// ---- 要るだけの JSON の読み手 ----

struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> items;
    std::map<std::string, Json> fields;

    const Json& operator[](const std::string& key) const {
        static const Json none;
        const auto it = fields.find(key);
        return it == fields.end() ? none : it->second;
    }
    const Json& operator[](size_t i) const {
        static const Json none;
        return i < items.size() ? items[i] : none;
    }
    double num(double fallback = 0) const { return kind == Number ? n : fallback; }
};

class Parser {
public:
    explicit Parser(const std::string& text) : t_(text) {}
    bool Parse(Json& out) {
        Value(out);
        Space();
        return ok_;
    }

private:
    void Space() {
        while (p_ < t_.size() && std::strchr(" \t\r\n", t_[p_])) ++p_;
    }
    bool Eat(char c) {
        Space();
        if (p_ < t_.size() && t_[p_] == c) {
            ++p_;
            return true;
        }
        return false;
    }
    void Str(std::string& out) {
        // 呼ぶ前に " を読んである
        while (p_ < t_.size() && t_[p_] != '"') {
            if (t_[p_] == '\\' && p_ + 1 < t_.size()) {
                const char e = t_[++p_];
                if (e == 'u') {
                    p_ += 4;  // 窓の題などの名前は使わないので、\u は読み飛ばす
                    out += '?';
                } else {
                    out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                }
                ++p_;
            } else {
                out += t_[p_++];
            }
        }
        if (p_ >= t_.size()) ok_ = false;
        ++p_;
    }
    void Value(Json& v) {
        Space();
        if (p_ >= t_.size() || depth_ > 64) {
            ok_ = false;
            return;
        }
        const char c = t_[p_];
        if (c == '{') {
            ++p_;
            ++depth_;
            v.kind = Json::Object;
            if (Eat('}')) {
                --depth_;
                return;
            }
            do {
                if (!Eat('"')) {
                    ok_ = false;
                    return;
                }
                std::string key;
                Str(key);
                if (!Eat(':')) {
                    ok_ = false;
                    return;
                }
                Value(v.fields[key]);
            } while (ok_ && Eat(','));
            if (!Eat('}')) ok_ = false;
            --depth_;
        } else if (c == '[') {
            ++p_;
            ++depth_;
            v.kind = Json::Array;
            if (Eat(']')) {
                --depth_;
                return;
            }
            do {
                v.items.emplace_back();
                Value(v.items.back());
            } while (ok_ && Eat(','));
            if (!Eat(']')) ok_ = false;
            --depth_;
        } else if (c == '"') {
            ++p_;
            v.kind = Json::String;
            Str(v.s);
        } else if (t_.compare(p_, 4, "true") == 0) {
            v.kind = Json::Bool;
            v.b = true;
            p_ += 4;
        } else if (t_.compare(p_, 5, "false") == 0) {
            v.kind = Json::Bool;
            p_ += 5;
        } else if (t_.compare(p_, 4, "null") == 0) {
            p_ += 4;
        } else {
            char* end = nullptr;
            v.n = std::strtod(t_.c_str() + p_, &end);
            if (end == t_.c_str() + p_) {
                ok_ = false;
                return;
            }
            v.kind = Json::Number;
            p_ = static_cast<size_t>(end - t_.c_str());
        }
    }

    const std::string& t_;
    size_t p_ = 0;
    int depth_ = 0;
    bool ok_ = true;
};

int Round(double v) { return static_cast<int>(std::lround(v)); }

std::string Request(const std::string& path, const std::string& command) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return {};
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    std::string out;
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
        write(fd, command.data(), command.size()) == static_cast<ssize_t>(command.size())) {
        char buf[8192];
        for (ssize_t n; (n = read(fd, buf, sizeof(buf))) > 0;) out.append(buf, static_cast<size_t>(n));
    }
    close(fd);
    return out;
}

// 窓が並ぶ・動く・閉じる・作業スペースが変わる知らせ
bool Relevant(const std::string& event) {
    static const char* const kEvents[] = {"workspace",  "workspacev2",   "focusedmon",   "fullscreen",
                                          "openwindow", "closewindow",   "movewindow",   "movewindowv2",
                                          "changefloatingmode", "activespecial", "activespecialv2",
                                          "moveworkspace", "moveworkspacev2", "monitoradded", "monitoraddedv2",
                                          "monitorremoved", "minimized", "togglegroup"};
    for (const char* e : kEvents) {
        if (event == e) return true;
    }
    return false;
}

}  // namespace

std::set<std::string> CoveredMonitors(const std::string& monitorsJson, const std::string& clientsJson) {
    Json monitors, clients;
    std::set<std::string> covered;
    if (!Parser(monitorsJson).Parse(monitors) || !Parser(clientsJson).Parse(clients)) return covered;
    for (const Json& m : monitors.items) {
        const std::string name = m["name"].s;
        if (m["dpmsStatus"].kind == Json::Bool && !m["dpmsStatus"].b) {
            covered.insert(name);
            continue;
        }
        const double scale = m["scale"].num(1) > 0 ? m["scale"].num(1) : 1;
        // 窓の位置は論理の座標。画面の幅・高さは物理ピクセルなので拡大率で割る（90°回した画面は縦横を入れ替える）
        double w = m["width"].num() / scale;
        double h = m["height"].num() / scale;
        if (static_cast<int>(m["transform"].num()) % 2 == 1) std::swap(w, h);
        const Json& reserved = m["reserved"];  // 左・上・右・下（バーの分）
        const PixelRect area{Round(m["x"].num() + reserved[0].num()), Round(m["y"].num() + reserved[1].num()),
                             Round(w - reserved[0].num() - reserved[2].num()),
                             Round(h - reserved[1].num() - reserved[3].num())};
        const int active = static_cast<int>(m["activeWorkspace"]["id"].num());
        const int special = static_cast<int>(m["specialWorkspace"]["id"].num());
        std::vector<PixelRect> rects;
        bool fullscreen = false;
        for (const Json& c : clients.items) {
            const int ws = static_cast<int>(c["workspace"]["id"].num());
            if ((ws != active && (special == 0 || ws != special)) || !c["mapped"].b || c["hidden"].b) continue;
            if (c["fullscreen"].num() > 0) fullscreen = true;
            rects.push_back({Round(c["at"][0].num()), Round(c["at"][1].num()), Round(c["size"][0].num()),
                             Round(c["size"][1].num())});
        }
        if (fullscreen || IsCovered(area, rects)) covered.insert(name);
    }
    return covered;
}

bool HyprlandWatcher::Start(Callback callback) {
    const char* signature = g_getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!signature || !*signature) return false;
    dir_ = std::string(g_get_user_runtime_dir()) + "/hypr/" + signature;
    callback_ = std::move(callback);
    fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::string path = dir_ + "/.socket2.sock";
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (fd_ < 0 || connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
        return false;
    }
    source_ = g_unix_fd_add(fd_, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
                            reinterpret_cast<GUnixFDSourceFunc>(&HyprlandWatcher::OnEvent), this);
    callback_(Query());
    return true;
}

HyprlandWatcher::~HyprlandWatcher() {
    if (settle_) g_source_remove(settle_);
    if (source_) g_source_remove(source_);
    if (fd_ >= 0) close(fd_);
}

std::set<std::string> HyprlandWatcher::Query() const {
    return CoveredMonitors(Request(dir_ + "/.socket.sock", "j/monitors"),
                           Request(dir_ + "/.socket.sock", "j/clients"));
}

int HyprlandWatcher::OnEvent(int fd, unsigned condition, void* data) {
    auto* self = static_cast<HyprlandWatcher*>(data);
    char buf[4096];
    ssize_t n = 0;
    while ((n = read(fd, buf, sizeof(buf))) > 0) self->buffer_.append(buf, static_cast<size_t>(n));
    if (n == 0 || (condition & (G_IO_HUP | G_IO_ERR))) {
        // Hyprland が終わった。隠れているとは扱わない
        self->source_ = 0;
        self->callback_({});
        return G_SOURCE_REMOVE;
    }
    bool relevant = false;
    for (size_t nl; (nl = self->buffer_.find('\n')) != std::string::npos;) {
        const std::string line = self->buffer_.substr(0, nl);
        self->buffer_.erase(0, nl + 1);
        const size_t mark = line.find(">>");
        if (mark != std::string::npos && Relevant(line.substr(0, mark))) relevant = true;
    }
    // 窓が動いている間（並べ直しの動き）は待ち、落ち着いてから1回だけ訊く
    if (relevant) {
        if (self->settle_) g_source_remove(self->settle_);
        self->settle_ = g_timeout_add(300, reinterpret_cast<GSourceFunc>(&HyprlandWatcher::OnSettled), self);
    }
    return G_SOURCE_CONTINUE;
}

int HyprlandWatcher::OnSettled(void* data) {
    auto* self = static_cast<HyprlandWatcher*>(data);
    self->settle_ = 0;
    self->callback_(self->Query());
    return G_SOURCE_REMOVE;
}

}  // namespace pixwall
