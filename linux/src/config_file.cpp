#include "config_file.h"

#include <gio/gio.h>
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace pixwall {

std::string ConfigPath() {
    return std::string(g_get_user_config_dir()) + "/pixwall/config.json";
}

Config LoadConfig(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return ParseConfig({});
    std::ostringstream text;
    text << in.rdbuf();
    return ParseConfig(text.str());
}

bool SaveConfig(const std::string& path, const Config& config) {
    const std::string dir = path.substr(0, path.find_last_of('/'));
    g_mkdir_with_parents(dir.c_str(), 0700);
    // 書きかけで落ちても壊れないよう、別のファイルに書いてから入れ替える（rename は読んでいる相手がいても入れ替わる）
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << FormatConfig(config);
        if (!out) return false;
    }
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

int LocalMinute() {
    // time() は粗い時計（CLOCK_REALTIME_COARSE）で数ミリ秒遅れることがあり、切り替わる時刻ちょうどに
    // 起きたタイマーから読むと、まだ前の分を返す。精密な時計で読む
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    const std::time_t now = ts.tv_sec;
    std::tm t{};
    tzset();
    localtime_r(&now, &t);
    return t.tm_hour * 60 + t.tm_min;
}

namespace {

// ポータルの color-scheme（0 好みなし・1 ダーク・2 ライト）。読めなければ -1
int PortalColorScheme() {
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return -1;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings",
        "ReadOne", g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
    g_object_unref(bus);
    if (!reply) return -1;
    GVariant* value = nullptr;
    g_variant_get(reply, "(v)", &value);
    int scheme = -1;
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) scheme = static_cast<int>(g_variant_get_uint32(value));
    g_variant_unref(value);
    g_variant_unref(reply);
    return scheme;
}

}  // namespace

bool SystemIsDark() {
    const int scheme = PortalColorScheme();
    if (scheme >= 0) return scheme == 1;
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    GSettingsSchema* schema =
        source ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE) : nullptr;
    if (!schema) return false;
    const bool has = g_settings_schema_has_key(schema, "color-scheme");
    g_settings_schema_unref(schema);
    if (!has) return false;
    GSettings* settings = g_settings_new("org.gnome.desktop.interface");
    gchar* value = g_settings_get_string(settings, "color-scheme");
    const bool dark = g_strcmp0(value, "prefer-dark") == 0;
    g_free(value);
    g_object_unref(settings);
    return dark;
}

std::string AbsolutePath(const std::string& path) {
    if (path.empty() || path[0] == '/') return path;
    gchar* dir = g_get_current_dir();
    gchar* full = g_canonicalize_filename(path.c_str(), dir);
    std::string out = full;
    g_free(full);
    g_free(dir);
    return out;
}

}  // namespace pixwall
