#include "config.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <variant>

namespace pixwall {

namespace {

// 入れ子のオブジェクト・配列は、中身を読まずに元の文字列のまま渡す（読むなら、もう一度 Reader で読む）
struct Raw {
    std::string_view text;
    char open;  // '{' か '['
};

// 値は文字列・数・真偽・null・入れ子
using Value = std::variant<std::monostate, std::string, double, bool, Raw>;

class Reader {
public:
    explicit Reader(std::string_view s) : s_(s) {}

    // 1つのキーと値ごとに f を呼ぶ。形が崩れていたら false
    template <class F>
    bool Object(F&& f) {
        Space();
        if (!Eat('{')) return false;
        Space();
        if (Eat('}')) return true;
        for (;;) {
            Space();
            auto key = String();
            if (!key) return false;
            Space();
            if (!Eat(':')) return false;
            Space();
            auto value = Any();
            if (!value) return false;
            f(*key, *value);
            Space();
            if (Eat(',')) continue;
            return Eat('}');
        }
    }

    // 配列の1つずつに f を呼ぶ。形が崩れていたら false
    template <class F>
    bool Array(F&& f) {
        Space();
        if (!Eat('[')) return false;
        Space();
        if (Eat(']')) return true;
        for (;;) {
            Space();
            auto value = Any();
            if (!value) return false;
            f(*value);
            Space();
            if (Eat(',')) continue;
            return Eat(']');
        }
    }

private:
    bool Eat(char c) {
        if (i_ < s_.size() && s_[i_] == c) {
            ++i_;
            return true;
        }
        return false;
    }
    void Space() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) ++i_;
    }
    bool Word(std::string_view w) {
        if (s_.substr(i_, w.size()) != w) return false;
        i_ += w.size();
        return true;
    }

    static void Utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::optional<unsigned> Hex4() {
        if (i_ + 4 > s_.size()) return std::nullopt;
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return std::nullopt;
        }
        return v;
    }

    std::optional<std::string> String() {
        if (!Eat('"')) return std::nullopt;
        std::string out;
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= s_.size()) return std::nullopt;
            switch (s_[i_++]) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    auto cp = Hex4();
                    if (!cp) return std::nullopt;
                    // サロゲートの組は1つの文字にする
                    if (*cp >= 0xD800 && *cp < 0xDC00 && Word("\\u")) {
                        auto low = Hex4();
                        if (!low) return std::nullopt;
                        *cp = 0x10000 + ((*cp - 0xD800) << 10) + (*low - 0xDC00);
                    }
                    Utf8(out, *cp);
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // 入れ子の配列・オブジェクトは中身を見ずに飛ばす
    bool Skip(char open, char close) {
        int depth = 0;
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == '"') {
                if (!String()) return false;
                continue;
            }
            ++i_;
            if (c == open) ++depth;
            if (c == close && --depth == 0) return true;
        }
        return false;
    }

    std::optional<Value> Any() {
        if (i_ >= s_.size()) return std::nullopt;
        const char c = s_[i_];
        if (c == '"') {
            auto s = String();
            if (!s) return std::nullopt;
            return Value{*s};
        }
        if (c == '{' || c == '[') {
            const size_t begin = i_;
            if (!Skip(c, c == '{' ? '}' : ']')) return std::nullopt;
            return Value{Raw{s_.substr(begin, i_ - begin), c}};
        }
        if (Word("true")) return Value{true};
        if (Word("false")) return Value{false};
        if (Word("null")) return Value{};
        const std::string rest(s_.substr(i_, 64));
        char* end = nullptr;
        const double d = std::strtod(rest.c_str(), &end);
        if (end == rest.c_str()) return std::nullopt;
        i_ += end - rest.c_str();
        return Value{d};
    }

    std::string_view s_;
    size_t i_ = 0;
};

void Quote(std::string& out, const std::string& s) {
    out += '"';
    for (const unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

const char* FitName(Fit fit) {
    switch (fit) {
        case Fit::Fit: return "fit";
        case Fit::Fill: return "fill";
        case Fit::Free: return "free";
        default: return "integer";
    }
}

// Slot の項目（image・fit・position・freeX・freeY・freeSize）なら読んで true
bool ReadSlotKey(Slot& slot, const std::string& key, const Value& v) {
    const auto* s = std::get_if<std::string>(&v);
    const auto* n = std::get_if<double>(&v);
    Placement& p = slot.placement;
    if (key == "image") {
        slot.image = s ? *s : std::string();
    } else if (key == "fit") {
        if (s && *s == "integer") p.fit = Fit::Integer;
        if (s && *s == "fit") p.fit = Fit::Fit;
        if (s && *s == "fill") p.fit = Fit::Fill;
        if (s && *s == "free") p.fit = Fit::Free;
    } else if (key == "position") {
        if (n) p.position = std::clamp(*n, 0.0, 1.0);
    } else if (key == "freeX") {
        if (n) p.freeX = std::clamp(*n, 0.0, 1.0);
    } else if (key == "freeY") {
        if (n) p.freeY = std::clamp(*n, 0.0, 1.0);
    } else if (key == "freeSize") {
        if (n) p.freeSize = std::clamp(*n, kFreeSizeMin, kFreeSizeMax);
    } else {
        return false;
    }
    return true;
}

// 入れ子のオブジェクトを Slot として読む。オブジェクトでなければ nullopt。start があれば読む（読めなければ 0）
std::optional<Slot> ReadSlot(const Value& v, int* start = nullptr) {
    const auto* raw = std::get_if<Raw>(&v);
    if (!raw || raw->open != '{') return std::nullopt;
    Slot slot;
    if (start) *start = 0;
    const bool ok = Reader(raw->text).Object([&](const std::string& key, const Value& value) {
        if (ReadSlotKey(slot, key, value)) return;
        if (key == "start" && start) {
            const auto* s = std::get_if<std::string>(&value);
            *start = s ? std::max(ParseClock(*s), 0) : 0;
        }
    });
    if (!ok) return std::nullopt;
    return slot;
}

std::string Number(double v) {
    char s[32];
    std::snprintf(s, sizeof(s), "%g", v);
    return s;
}

// Slot の項目を名前順に書く（start は TimeSlot のときだけ）。indent は項目の前の空白
void WriteSlot(std::string& out, const Slot& slot, const std::string& indent, const int* start) {
    const Placement& p = slot.placement;
    out += "{\n";
    out += indent + "\"fit\" : \"" + FitName(p.fit) + "\",\n";
    out += indent + "\"freeSize\" : " + Number(p.freeSize) + ",\n";
    out += indent + "\"freeX\" : " + Number(p.freeX) + ",\n";
    out += indent + "\"freeY\" : " + Number(p.freeY) + ",\n";
    if (!slot.image.empty()) {
        out += indent + "\"image\" : ";
        Quote(out, slot.image);
        out += ",\n";
    }
    out += indent + "\"position\" : " + Number(p.position);
    if (start) out += ",\n" + indent + "\"start\" : \"" + FormatClock(*start) + "\"";
    out += "\n" + indent.substr(2) + "}";
}

const char* SwitchByName(SwitchBy mode) {
    switch (mode) {
        case SwitchBy::Time: return "time";
        case SwitchBy::Appearance: return "appearance";
        default: return "none";
    }
}

}  // namespace

int ParseClock(std::string_view s) {
    const size_t colon = s.find(':');
    if (colon == std::string_view::npos || colon < 1 || colon > 2 || s.size() != colon + 3) return -1;
    int h = 0, m = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == colon) continue;
        if (s[i] < '0' || s[i] > '9') return -1;
        (i < colon ? h : m) = (i < colon ? h : m) * 10 + (s[i] - '0');
    }
    return h < 24 && m < 60 ? h * 60 + m : -1;
}

std::string FormatClock(int minutes) {
    const int m = ((minutes % kMinutesPerDay) + kMinutesPerDay) % kMinutesPerDay;
    char s[8];
    std::snprintf(s, sizeof(s), "%02d:%02d", m / 60, m % 60);
    return s;
}

void Config::setMainSlot(const Slot& slot) {
    image = slot.image;
    fit = slot.placement.fit;
    position = slot.placement.position;
    freeX = slot.placement.freeX;
    freeY = slot.placement.freeY;
    freeSize = slot.placement.freeSize;
}

int Config::ActiveTimeIndex(int minute) const {
    int best = -1;      // 今以前に始まるもののうち一番遅いもの
    int latest = -1;    // 一番遅く始まるもの
    for (int i = 0; i < static_cast<int>(timeSlots.size()); ++i) {
        const int start = timeSlots[i].start;
        if (start <= minute && (best < 0 || start >= timeSlots[best].start)) best = i;
        if (latest < 0 || start >= timeSlots[latest].start) latest = i;
    }
    return best >= 0 ? best : latest;
}

Slot Config::ActiveSlot(int minute, bool dark) const {
    switch (switchBy) {
        case SwitchBy::Time: {
            const int i = ActiveTimeIndex(minute);
            return i >= 0 ? timeSlots[i].slot : mainSlot();
        }
        case SwitchBy::Appearance: {
            const auto& slot = dark ? darkSlot : lightSlot;
            return slot ? *slot : mainSlot();
        }
        default: return mainSlot();
    }
}

Config Config::Resolved(int minute, bool dark) const {
    Config c = *this;
    c.setMainSlot(ActiveSlot(minute, dark));
    return c;
}

int Config::MinutesToNextSwitch(int minute) const {
    if (switchBy != SwitchBy::Time) return -1;
    bool differ = false;
    int best = -1;
    for (const auto& t : timeSlots) {
        if (t.start != timeSlots[0].start) differ = true;
        const int wait = ((t.start - minute - 1) % kMinutesPerDay + kMinutesPerDay) % kMinutesPerDay + 1;
        if (best < 0 || wait < best) best = wait;
    }
    return differ ? best : -1;
}

void Config::SwitchTo(SwitchBy mode) {
    switchBy = mode;
    if (mode == SwitchBy::Time && timeSlots.empty()) {
        timeSlots = {TimeSlot{6 * 60, mainSlot()}, TimeSlot{18 * 60, mainSlot()}};
    }
    if (mode == SwitchBy::Appearance) {
        if (!lightSlot) lightSlot = mainSlot();
        if (!darkSlot) darkSlot = mainSlot();
    }
}

Slot Config::GetSlot(SlotRef ref) const {
    switch (ref.kind) {
        case SlotRef::Time:
            return ref.index >= 0 && ref.index < static_cast<int>(timeSlots.size()) ? timeSlots[ref.index].slot
                                                                                    : mainSlot();
        case SlotRef::Light: return lightSlot ? *lightSlot : mainSlot();
        case SlotRef::Dark: return darkSlot ? *darkSlot : mainSlot();
        default: return mainSlot();
    }
}

void Config::SetSlot(SlotRef ref, const Slot& slot) {
    switch (ref.kind) {
        case SlotRef::Time:
            if (ref.index >= 0 && ref.index < static_cast<int>(timeSlots.size())) timeSlots[ref.index].slot = slot;
            break;
        case SlotRef::Light: lightSlot = slot; break;
        case SlotRef::Dark: darkSlot = slot; break;
        default: setMainSlot(slot); break;
    }
}

std::vector<std::string> Config::AllImages() const {
    std::vector<std::string> all;
    const auto add = [&](const std::string& path) {
        if (!path.empty() && std::find(all.begin(), all.end(), path) == all.end()) all.push_back(path);
    };
    add(image);
    for (const auto& t : timeSlots) add(t.slot.image);
    if (lightSlot) add(lightSlot->image);
    if (darkSlot) add(darkSlot->image);
    return all;
}

int ParseHexColor(std::string_view s) {
    if (s.size() != 7 || s[0] != '#') return -1;
    int v = 0;
    for (char ch : s.substr(1)) {
        int d = -1;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        if (d < 0) return -1;
        v = v * 16 + d;
    }
    return v;
}

Config ParseConfig(std::string_view json) {
    // 先頭の BOM は飛ばす（Windows PowerShell 5 の Set-Content -Encoding UTF8 などが付ける）
    if (json.substr(0, 3) == "\xEF\xBB\xBF") json.remove_prefix(3);
    Config c;
    const Config d;
    const bool ok = Reader(json).Object([&](const std::string& key, const Value& v) {
        const auto* s = std::get_if<std::string>(&v);
        const auto* n = std::get_if<double>(&v);
        const auto* b = std::get_if<bool>(&v);
        if (key == "version" && n) c.version = static_cast<int>(*n);
        // 画像と並べ方は、切り替える先の1つ（Slot）と同じ読み方をする
        Slot main = c.mainSlot();
        if (ReadSlotKey(main, key, v)) c.setMainSlot(main);
        if (key == "switchBy" && s) {
            if (*s == "none") c.switchBy = SwitchBy::None;
            if (*s == "time") c.switchBy = SwitchBy::Time;
            if (*s == "appearance") c.switchBy = SwitchBy::Appearance;
        }
        // 読めない1つは飛ばす
        if (key == "timeSlots") {
            if (const auto* raw = std::get_if<Raw>(&v); raw && raw->open == '[') {
                Reader(raw->text).Array([&](const Value& item) {
                    int start = 0;
                    if (auto slot = ReadSlot(item, &start)) c.timeSlots.push_back(TimeSlot{start, *slot});
                });
            }
        }
        if (key == "lightSlot") c.lightSlot = ReadSlot(v);
        if (key == "darkSlot") c.darkSlot = ReadSlot(v);
        if (key == "background" && s) {
            if (*s == "auto") c.customBackground = false;
            if (*s == "custom") c.customBackground = true;
        }
        if (key == "backgroundColor" && s) c.backgroundColor = ParseHexColor(*s);
        if (key == "speed" && n) c.speed = std::clamp(*n, 0.25, 4.0);
        if (key == "pauseOnLowPower" && b) c.pauseOnLowPower = *b;
        if (key == "launchAtLogin" && b) c.launchAtLogin = *b;
    });
    return ok ? c : Config{};
}

std::string FormatConfig(const Config& c) {
    char speed[32];
    std::snprintf(speed, sizeof(speed), "%g", c.speed);
    const auto number = [](double v) {
        char s[32];
        std::snprintf(s, sizeof(s), "%g", v);
        return std::string(s);
    };
    std::string out = "{\n";
    out += std::string("  \"background\" : \"") + (c.customBackground ? "custom" : "auto") + "\",\n";
    if (c.backgroundColor >= 0) {
        char color[16];
        std::snprintf(color, sizeof(color), "#%06X", c.backgroundColor & 0xFFFFFF);
        out += std::string("  \"backgroundColor\" : \"") + color + "\",\n";
    }
    if (c.darkSlot) {
        out += "  \"darkSlot\" : ";
        WriteSlot(out, *c.darkSlot, "    ", nullptr);
        out += ",\n";
    }
    out += "  \"fit\" : \"";
    out += FitName(c.fit);
    out += "\",\n";
    out += "  \"freeSize\" : " + number(c.freeSize) + ",\n";
    out += "  \"freeX\" : " + number(c.freeX) + ",\n";
    out += "  \"freeY\" : " + number(c.freeY) + ",\n";
    if (!c.image.empty()) {
        out += "  \"image\" : ";
        Quote(out, c.image);
        out += ",\n";
    }
    out += std::string("  \"launchAtLogin\" : ") + (c.launchAtLogin ? "true" : "false") + ",\n";
    if (c.lightSlot) {
        out += "  \"lightSlot\" : ";
        WriteSlot(out, *c.lightSlot, "    ", nullptr);
        out += ",\n";
    }
    out += std::string("  \"pauseOnLowPower\" : ") + (c.pauseOnLowPower ? "true" : "false") + ",\n";
    out += "  \"position\" : " + number(c.position) + ",\n";
    out += std::string("  \"speed\" : ") + speed + ",\n";
    out += std::string("  \"switchBy\" : \"") + SwitchByName(c.switchBy) + "\",\n";
    // Mac 版（JSONEncoder）と同じく、空の配列は改行を2つ挟む
    out += "  \"timeSlots\" : [\n";
    for (size_t i = 0; i < c.timeSlots.size(); ++i) {
        out += "    ";
        WriteSlot(out, c.timeSlots[i].slot, "      ", &c.timeSlots[i].start);
        out += i + 1 < c.timeSlots.size() ? ",\n" : "\n";
    }
    if (c.timeSlots.empty()) out += "\n";
    out += "  ],\n";
    out += "  \"version\" : " + std::to_string(c.version) + "\n}";
    return out;
}

}  // namespace pixwall
