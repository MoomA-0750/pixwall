#include "config.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <variant>

namespace pixwall {

namespace {

// 平らなオブジェクト（値は文字列・数・真偽・null）だけ読む。入れ子の値は読み飛ばす
using Value = std::variant<std::monostate, std::string, double, bool>;

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
            if (!Skip(c, c == '{' ? '}' : ']')) return std::nullopt;
            return Value{};
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
        default: return "integer";
    }
}

}  // namespace

Config ParseConfig(std::string_view json) {
    Config c;
    const Config d;
    const bool ok = Reader(json).Object([&](const std::string& key, const Value& v) {
        const auto* s = std::get_if<std::string>(&v);
        const auto* n = std::get_if<double>(&v);
        const auto* b = std::get_if<bool>(&v);
        if (key == "version" && n) c.version = static_cast<int>(*n);
        if (key == "image") c.image = s ? *s : d.image;
        if (key == "fit" && s) {
            if (*s == "integer") c.fit = Fit::Integer;
            if (*s == "fit") c.fit = Fit::Fit;
            if (*s == "fill") c.fit = Fit::Fill;
        }
        if (key == "speed" && n) c.speed = std::clamp(*n, 0.25, 4.0);
        if (key == "pauseOnLowPower" && b) c.pauseOnLowPower = *b;
        if (key == "launchAtLogin" && b) c.launchAtLogin = *b;
    });
    return ok ? c : Config{};
}

std::string FormatConfig(const Config& c) {
    char speed[32];
    std::snprintf(speed, sizeof(speed), "%g", c.speed);
    std::string out = "{\n";
    out += "  \"fit\" : \"";
    out += FitName(c.fit);
    out += "\",\n";
    if (!c.image.empty()) {
        out += "  \"image\" : ";
        Quote(out, c.image);
        out += ",\n";
    }
    out += std::string("  \"launchAtLogin\" : ") + (c.launchAtLogin ? "true" : "false") + ",\n";
    out += std::string("  \"pauseOnLowPower\" : ") + (c.pauseOnLowPower ? "true" : "false") + ",\n";
    out += std::string("  \"speed\" : ") + speed + ",\n";
    out += "  \"version\" : " + std::to_string(c.version) + "\n}";
    return out;
}

}  // namespace pixwall
