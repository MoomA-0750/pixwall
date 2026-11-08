#include "omarchy.h"

#include <glib.h>

namespace pixwall {

namespace {

bool IsFile(const std::string& path) { return g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR); }

std::string Lower(std::string s) {
    for (char& c : s) c = g_ascii_tolower(c);
    return s;
}

}  // namespace

std::string OmarchyCurrentDir() { return std::string(g_get_user_state_dir()) + "/omarchy/current"; }

std::string OmarchyImage(const std::string& linkPath) {
    // リンクの先。リンクでなければ（ファイルそのものなら）それを使う
    std::string target = linkPath;
    if (gchar* link = g_file_read_link(linkPath.c_str(), nullptr)) {
        if (g_path_is_absolute(link)) {
            target = link;
        } else {
            gchar* dir = g_path_get_dirname(linkPath.c_str());
            gchar* full = g_canonicalize_filename(link, dir);
            target = full;
            g_free(full);
            g_free(dir);
        }
        g_free(link);
    }
    if (!IsFile(target)) return {};
    const size_t slash = target.find_last_of('/');
    const std::string dir = target.substr(0, slash + 1);
    const std::string name = target.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    const std::string stem = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    const std::string ext = dot == std::string::npos || dot == 0 ? "" : Lower(name.substr(dot + 1));
    // GIF そのものを選んだ
    if (ext == "gif") return target;
    for (const char* e : {".gif", ".GIF", ".webp", ".WEBP"}) {
        const std::string pair = dir + "pixwall/" + stem + e;
        if (IsFile(pair)) return pair;
    }
    return {};
}

Config ResolveNow(const Config& saved, int minute, bool dark) {
    if (saved.switchBy != SwitchBy::Omarchy) return saved.Resolved(minute, dark);
    Config c = saved;
    c.setMainSlot(saved.OmarchySlot(OmarchyImage(OmarchyCurrentDir() + "/background")));
    return c;
}

}  // namespace pixwall
