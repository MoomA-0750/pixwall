#include "cli.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pixwall {

const char* const kCliUsage =
    "usage: pixwallctl <command>\n"
    "\n"
    "  show                         print the settings (config.json)\n"
    "  get <key>                    print one setting\n"
    "  set <key> <value> [...]      change settings; the wallpaper follows at once\n"
    "  status                       print running, paused or stopped\n"
    "  pause | resume               pause or resume the animation\n"
    "  refresh                      count the screens again and redraw\n"
    "  quit                         quit pixwall (the OS wallpaper is restored)\n"
    "  export <file.png> [--frame N]\n"
    "                               write a still image for each screen (frame N, from 1)\n"
    "\n"
    "keys and values:\n"
    "  image            path to a GIF, PNG or WebP\n"
    "  fit              integer | fit | fill | free\n"
    "  position         0 to 1 (0 top/left, 0.5 center, 1 bottom/right; fit and fill)\n"
    "  freeX, freeY     0 to 1 (0 left/top edge, 1 right/bottom edge; free)\n"
    "  freeSize         0.1 to 3 (1 is the size that fits the screen; free)\n"
    "  background       auto | custom\n"
    "  backgroundColor  #RRGGBB (used when background is custom)\n"
    "  speed            0.25 to 4\n"
    "  pauseOnLowPower  true | false\n"
    "  launchAtLogin    true | false";

namespace {

const char* const kKeys[] = {"image", "fit", "position", "freeX", "freeY", "freeSize", "background", "backgroundColor",
                             "speed", "pauseOnLowPower", "launchAtLogin"};

// 0.5 は "0.5"、1 は "1"（config.json と同じ）
std::string Number(double v) {
    char s[32];
    std::snprintf(s, sizeof(s), "%g", v);
    return s;
}

// 全部が数として読めて、範囲の中なら true
bool ParseNumber(const std::string& s, double low, double high, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(v) || v < low || v > high) return false;
    out = v;
    return true;
}

}  // namespace

bool IsConfigKey(const std::string& key) {
    for (const char* k : kKeys) {
        if (key == k) return true;
    }
    return false;
}

bool ParseCli(const std::vector<std::string>& args, CliCommand& out, std::string& error) {
    out = {};
    if (args.empty()) {
        error = "no command";
        return false;
    }
    const std::string& name = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    const std::pair<const char*, CliAction> simple[] = {
        {"show", CliAction::Show},     {"status", CliAction::Status},   {"pause", CliAction::Pause},
        {"resume", CliAction::Resume}, {"refresh", CliAction::Refresh}, {"quit", CliAction::Quit},
    };
    for (const auto& [n, action] : simple) {
        if (name != n) continue;
        if (!rest.empty()) {
            error = name + " takes no arguments";
            return false;
        }
        out.action = action;
        return true;
    }
    if (name == "help" || name == "-h" || name == "--help") {
        out.action = CliAction::Help;
        return true;
    }
    auto key = [&](const std::string& k) {
        if (IsConfigKey(k)) return true;
        error = "unknown key: " + k;
        return false;
    };
    if (name == "get") {
        if (rest.size() != 1) {
            error = "get takes one key";
            return false;
        }
        if (!key(rest[0])) return false;
        out.action = CliAction::Get;
        out.pairs.push_back({rest[0], ""});
        return true;
    }
    if (name == "set") {
        if (rest.empty() || rest.size() % 2 != 0) {
            error = "set takes pairs of key and value";
            return false;
        }
        for (size_t i = 0; i < rest.size(); i += 2) {
            if (!key(rest[i])) return false;
            out.pairs.push_back({rest[i], rest[i + 1]});
        }
        out.action = CliAction::Set;
        return true;
    }
    if (name == "export") {
        bool havePath = false;
        for (size_t i = 0; i < rest.size(); ++i) {
            if (rest[i] == "--frame") {
                double n = 0;
                if (i + 1 >= rest.size() || !ParseNumber(rest[i + 1], 1, 1e9, n) || n != std::floor(n)) {
                    error = "--frame takes a number from 1";
                    return false;
                }
                out.frame = static_cast<int>(n) - 1;
                ++i;
            } else if (!havePath) {
                out.path = rest[i];
                havePath = true;
            } else {
                error = "export takes one file";
                return false;
            }
        }
        if (!havePath) {
            error = "export takes a file";
            return false;
        }
        out.action = CliAction::Export;
        return true;
    }
    error = "unknown command: " + name;
    return false;
}

std::string ConfigValue(const Config& c, const std::string& key) {
    if (key == "image") return c.image;
    if (key == "fit") {
        switch (c.fit) {
            case Fit::Fit: return "fit";
            case Fit::Fill: return "fill";
            case Fit::Free: return "free";
            default: return "integer";
        }
    }
    if (key == "position") return Number(c.position);
    if (key == "freeX") return Number(c.freeX);
    if (key == "freeY") return Number(c.freeY);
    if (key == "freeSize") return Number(c.freeSize);
    if (key == "background") return c.customBackground ? "custom" : "auto";
    if (key == "backgroundColor") {
        if (c.backgroundColor < 0) return "";
        char s[16];
        std::snprintf(s, sizeof(s), "#%06X", c.backgroundColor & 0xFFFFFF);
        return s;
    }
    if (key == "speed") return Number(c.speed);
    if (key == "pauseOnLowPower") return c.pauseOnLowPower ? "true" : "false";
    if (key == "launchAtLogin") return c.launchAtLogin ? "true" : "false";
    return "";
}

bool SetConfigValue(Config& c, const std::string& key, const std::string& v, std::string& error) {
    auto number = [&](double low, double high, double& out) {
        if (ParseNumber(v, low, high, out)) return true;
        error = key + " must be from " + Number(low) + " to " + Number(high);
        return false;
    };
    auto boolean = [&](bool& out) {
        if (v == "true" || v == "false") {
            out = v == "true";
            return true;
        }
        error = key + " must be true or false";
        return false;
    };
    if (key == "image") {
        if (v.empty()) {
            error = "image must be a path";
            return false;
        }
        c.image = v;
        return true;
    }
    if (key == "fit") {
        if (v == "integer") c.fit = Fit::Integer;
        else if (v == "fit") c.fit = Fit::Fit;
        else if (v == "fill") c.fit = Fit::Fill;
        else if (v == "free") c.fit = Fit::Free;
        else {
            error = "fit must be integer, fit, fill or free";
            return false;
        }
        return true;
    }
    if (key == "position") return number(0, 1, c.position);
    if (key == "freeX") return number(0, 1, c.freeX);
    if (key == "freeY") return number(0, 1, c.freeY);
    if (key == "freeSize") return number(kFreeSizeMin, kFreeSizeMax, c.freeSize);
    if (key == "background") {
        if (v != "auto" && v != "custom") {
            error = "background must be auto or custom";
            return false;
        }
        c.customBackground = v == "custom";
        return true;
    }
    if (key == "backgroundColor") {
        const int color = ParseHexColor(v);
        if (color < 0) {
            error = "backgroundColor must be #RRGGBB";
            return false;
        }
        c.backgroundColor = color;
        return true;
    }
    if (key == "speed") return number(0.25, 4, c.speed);
    if (key == "pauseOnLowPower") return boolean(c.pauseOnLowPower);
    if (key == "launchAtLogin") return boolean(c.launchAtLogin);
    error = "unknown key: " + key;
    return false;
}

}  // namespace pixwall
