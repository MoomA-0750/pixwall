#include "gnome_wallpaper.h"

#include <gio/gio.h>
#include <glib/gstdio.h>

namespace pixwall {

namespace {

const char* const kSchema = "org.gnome.desktop.background";

GSettings* Background() {
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    GSettingsSchema* schema = source ? g_settings_schema_source_lookup(source, kSchema, TRUE) : nullptr;
    if (!schema) return nullptr;
    g_settings_schema_unref(schema);
    return g_settings_new(kSchema);
}

std::string Get(GSettings* s, const char* key) {
    gchar* v = g_settings_get_string(s, key);
    std::string out = v ? v : "";
    g_free(v);
    return out;
}

std::string Uri(const std::string& path) {
    gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    std::string out = uri ? uri : "";
    g_free(uri);
    return out;
}

std::string Png(const std::string& dir, char which) { return dir + "/wallpaper-" + which + ".png"; }

// pixwall の PNG を指しているか
bool Ours(const std::string& dir, const std::string& uri) {
    return uri == Uri(Png(dir, 'a')) || uri == Uri(Png(dir, 'b'));
}

std::string StatePath(const std::string& dir) { return dir + "/original.ini"; }

}  // namespace

std::string GnomeWallpaperDir() { return std::string(g_get_user_data_dir()) + "/pixwall/desktop"; }

bool ApplyGnomeWallpaper(const std::string& dir, const std::string& key,
                         const std::function<bool(const std::string& png)>& render) {
    GSettings* s = Background();
    if (!s) return false;
    g_mkdir_with_parents(dir.c_str(), 0700);
    const std::string uri = Get(s, "picture-uri");
    const std::string dark = Get(s, "picture-uri-dark");

    GKeyFile* state = g_key_file_new();
    const bool haveState = g_key_file_load_from_file(state, StatePath(dir).c_str(), G_KEY_FILE_NONE, nullptr);
    // pixwall の PNG を指していなければ、今のが本人の壁紙（初めて入れる・本人が替えた）。覚え書きが無いのに
    // pixwall の PNG を指していたら、元はわからないので空（戻すときは GNOME の既定に）
    auto remember = [&](const char* name, const std::string& value) {
        if (!Ours(dir, value)) {
            g_key_file_set_string(state, "original", name, value.c_str());
            g_key_file_remove_key(state, "current", "key", nullptr);
        } else if (!haveState) {
            g_key_file_set_string(state, "original", name, "");
        }
    };
    if (!Ours(dir, uri))
        g_key_file_set_string(state, "original", "picture-options", Get(s, "picture-options").c_str());
    else if (!haveState)
        g_key_file_set_string(state, "original", "picture-options", "");
    remember("picture-uri", uri);
    remember("picture-uri-dark", dark);
    gchar* last = g_key_file_get_string(state, "current", "key", nullptr);
    gchar* file = g_filename_from_uri(uri.c_str(), nullptr, nullptr);
    const bool same = last && key == last && Ours(dir, uri) && uri == dark && file &&
                      g_file_test(file, G_FILE_TEST_IS_REGULAR);
    g_free(file);
    g_free(last);
    bool ok = true;
    if (!same) {
        // 前と別の名前に書く（同じ名前に書き直すと、GNOME が前の絵を使い続けることがある）
        const char next = uri == Uri(Png(dir, 'a')) ? 'b' : 'a';
        const std::string png = Png(dir, next);
        ok = render(png);
        if (ok) {
            // 先に覚え書きを書く（設定を替えたあとに落ちても、元の壁紙を覚えている）
            g_key_file_set_string(state, "current", "key", key.c_str());
            ok = g_key_file_save_to_file(state, StatePath(dir).c_str(), nullptr);
        }
        if (ok) {
            const std::string newUri = Uri(png);
            g_settings_set_string(s, "picture-uri", newUri.c_str());
            g_settings_set_string(s, "picture-uri-dark", newUri.c_str());
            // その画面の大きさに描いてあるので、拡大されずにそのまま出る
            g_settings_set_string(s, "picture-options", "zoom");
            g_settings_sync();
            g_remove(Png(dir, next == 'a' ? 'b' : 'a').c_str());
        }
    } else {
        g_key_file_save_to_file(state, StatePath(dir).c_str(), nullptr);
    }
    g_key_file_free(state);
    g_object_unref(s);
    return ok;
}

bool RestoreGnomeWallpaper(const std::string& dir) {
    GKeyFile* state = g_key_file_new();
    if (!g_key_file_load_from_file(state, StatePath(dir).c_str(), G_KEY_FILE_NONE, nullptr)) {
        g_key_file_free(state);
        return true;
    }
    GSettings* s = Background();
    if (!s) {
        g_key_file_free(state);
        return false;
    }
    // 覚えた値に戻す。空なら GNOME の既定に
    auto restore = [&](const char* name) {
        gchar* v = g_key_file_get_string(state, "original", name, nullptr);
        if (v && *v) g_settings_set_string(s, name, v);
        else g_settings_reset(s, name);
        g_free(v);
    };
    // 本人が替えたところは残す
    if (Ours(dir, Get(s, "picture-uri"))) restore("picture-uri");
    // 置き方は、pixwall の置き方のままなら戻す（本人が替えた壁紙にも、元の置き方を使う）
    if (Get(s, "picture-options") == "zoom") restore("picture-options");
    if (Ours(dir, Get(s, "picture-uri-dark"))) restore("picture-uri-dark");
    g_settings_sync();
    g_object_unref(s);
    g_key_file_free(state);
    g_remove(Png(dir, 'a').c_str());
    g_remove(Png(dir, 'b').c_str());
    g_remove(StatePath(dir).c_str());
    return true;
}

}  // namespace pixwall
