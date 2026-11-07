// pixwall の常駐側（Hyprland・Sway・KDE など、wlr-layer-shell を持つ Wayland の合成器）。
// 画面ごとに背景の層（background）の面を作り、GIF のフレームを GPU に置いて、絵が変わる瞬間だけ描く。
// GNOME は layer-shell を持たないので、シェルの拡張機能（gnome-extension/）が同じ役をする
#include <gio/gio.h>
#include <glib-unix.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-egl.h>

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "bus.h"
#include "config.h"
#include "config_file.h"
#include "fractional-scale-v1-client-protocol.h"
#include "image_loader.h"
#include "renderer.h"
#include "viewporter-client-protocol.h"
// 生成したヘッダーは C++ の予約語 namespace を引数の名前に使う
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

using namespace pixwall;

namespace {

struct App;

// 1つの画面の、背景の層の面
struct Surface {
    App* app = nullptr;
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    wp_viewport* viewport = nullptr;
    wp_fractional_scale_v1* fractional = nullptr;
    wl_egl_window* window = nullptr;
    EGLSurface egl = EGL_NO_SURFACE;
    int logicalWidth = 0;
    int logicalHeight = 0;
    int scale120 = 0;  // 合成器が望む拡大率 × 120（fractional-scale）。0 なら画面の整数の拡大率を使う
    int outputScale = 1;
    bool configured = false;
    wl_callback* frame = nullptr;  // 前に描いた絵が出るのを待っている
    bool dirty = false;            // 待っている間に絵が変わった
};

struct Output {
    App* app = nullptr;
    uint32_t name = 0;
    wl_output* output = nullptr;
    int scale = 1;
    std::unique_ptr<Surface> surface;
};

struct App {
    GMainLoop* loop = nullptr;
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    zwlr_layer_shell_v1* layerShell = nullptr;
    wp_viewporter* viewporter = nullptr;
    wp_fractional_scale_manager_v1* fractionalManager = nullptr;
    std::vector<std::unique_ptr<Output>> outputs;
    Renderer renderer;

    std::string configPath;
    Config saved;   // config.json のとおり
    Config config;  // 今出すものを mainSlot に入れたもの。描くのはこちら
    std::string loadedImage;
    bool imageOk = false;
    std::vector<double> delays;
    int autoColor = 0;
    size_t index = 0;

    guint frameTimer = 0;
    bool stalled = false;  // どの画面も前の絵がまだ出ていない（隠れている）ので、タイマーを止めている
    bool userPaused = false;
    bool lowPower = false;
    bool dark = false;

    int switchTimer = -1;
    GDBusConnection* session = nullptr;
    GDBusConnection* system = nullptr;
    GDBusProxy* power = nullptr;
    GFileMonitor* zoneMonitor = nullptr;
    guint busOwner = 0;
};

App* gApp = nullptr;

// eglSwapBuffers は合成器の知らせを読んで溜めることがあり、そのときは読める印が来ない。描いたあとに一度捌く
// （知らせの中から描くこともあるので、その場では捌かず、手が空いてから）
guint gPendingDispatch = 0;

gboolean OnDispatchPending(gpointer data) {
    gPendingDispatch = 0;
    auto* app = static_cast<App*>(data);
    wl_display_dispatch_pending(app->display);
    wl_display_flush(app->display);
    return G_SOURCE_REMOVE;
}

void DrawSurface(Surface* s);
void ScheduleFrame(App* app);

// ---- 描く ----

int Background(const App* app) {
    return app->config.customBackground && app->config.backgroundColor >= 0 ? app->config.backgroundColor
                                                                             : app->autoColor;
}

void PhysicalSize(const Surface* s, int& w, int& h) {
    if (s->scale120 > 0) {
        w = static_cast<int>(std::lround(s->logicalWidth * s->scale120 / 120.0));
        h = static_cast<int>(std::lround(s->logicalHeight * s->scale120 / 120.0));
    } else {
        w = s->logicalWidth * s->outputScale;
        h = s->logicalHeight * s->outputScale;
    }
}

void OnFrameDone(void* data, wl_callback* callback, uint32_t) {
    auto* s = static_cast<Surface*>(data);
    wl_callback_destroy(callback);
    s->frame = nullptr;
    if (s->dirty) DrawSurface(s);
    App* app = s->app;
    if (app->stalled) {
        // 隠れていた画面が見えるようになった
        app->stalled = false;
        ScheduleFrame(app);
    }
}

const wl_callback_listener kFrameListener = {OnFrameDone};

void DrawSurface(Surface* s) {
    if (!s->configured || s->egl == EGL_NO_SURFACE) return;
    if (s->frame) {
        // 前の絵がまだ出ていない（隠れている・合成が追いついていない）。出たら描く
        s->dirty = true;
        return;
    }
    s->dirty = false;
    int w = 0, h = 0;
    PhysicalSize(s, w, h);
    if (w <= 0 || h <= 0) return;
    int ww = 0, wh = 0;
    wl_egl_window_get_attached_size(s->window, &ww, &wh);
    if (ww != w || wh != h) wl_egl_window_resize(s->window, w, h, 0, 0);
    if (s->viewport) {
        wp_viewport_set_destination(s->viewport, s->logicalWidth, s->logicalHeight);
    } else {
        wl_surface_set_buffer_scale(s->surface, s->outputScale);
    }
    s->frame = wl_surface_frame(s->surface);
    wl_callback_add_listener(s->frame, &kFrameListener, s);
    App* app = s->app;
    app->renderer.Draw(s->egl, w, h, app->index, app->config.placement(), Background(app));
    if (!gPendingDispatch) gPendingDispatch = g_idle_add(OnDispatchPending, app);
}

void DrawAll(App* app) {
    for (auto& o : app->outputs) {
        if (o->surface) DrawSurface(o->surface.get());
    }
    wl_display_flush(app->display);
}

bool Animating(const App* app) {
    return app->imageOk && app->renderer.frameCount() > 1 && !app->userPaused &&
           !(app->lowPower && app->config.pauseOnLowPower);
}

gboolean OnFrameTimer(gpointer data) {
    auto* app = static_cast<App*>(data);
    app->frameTimer = 0;
    app->index = (app->index + 1) % app->renderer.frameCount();
    bool shown = false;
    bool any = false;
    for (auto& o : app->outputs) {
        Surface* s = o->surface.get();
        if (!s || !s->configured) continue;
        any = true;
        if (!s->frame) shown = true;
        DrawSurface(s);
    }
    wl_display_flush(app->display);
    if (any && !shown) {
        // どの画面も前の絵が出ていない（全画面の窓・ロック・画面が消えている）。出るまでタイマーを止める
        app->stalled = true;
        return G_SOURCE_REMOVE;
    }
    ScheduleFrame(app);
    return G_SOURCE_REMOVE;
}

void ScheduleFrame(App* app) {
    if (app->frameTimer) {
        g_source_remove(app->frameTimer);
        app->frameTimer = 0;
    }
    if (!Animating(app) || app->stalled) return;
    const double delay = FrameDelay(app->delays[app->index], app->config.speed);
    app->frameTimer = g_timeout_add(static_cast<guint>(std::lround(delay * 1000)), OnFrameTimer, app);
}

// ---- 画面ごとの面 ----

void OnPreferredScale(void* data, wp_fractional_scale_v1*, uint32_t scale) {
    auto* s = static_cast<Surface*>(data);
    if (s->scale120 == static_cast<int>(scale)) return;
    s->scale120 = static_cast<int>(scale);
    DrawSurface(s);
    wl_display_flush(s->app->display);
}

const wp_fractional_scale_v1_listener kFractionalListener = {OnPreferredScale};

void DestroySurface(Output* o);

void OnLayerConfigure(void* data, zwlr_layer_surface_v1* layer, uint32_t serial, uint32_t w, uint32_t h) {
    auto* s = static_cast<Surface*>(data);
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    s->logicalWidth = static_cast<int>(w);
    s->logicalHeight = static_cast<int>(h);
    s->configured = true;
    if (!s->window) {
        int pw = 0, ph = 0;
        PhysicalSize(s, pw, ph);
        s->window = wl_egl_window_create(s->surface, std::max(pw, 1), std::max(ph, 1));
        s->egl = s->app->renderer.CreateSurface(reinterpret_cast<EGLNativeWindowType>(s->window));
    }
    // 大きさが変わった・初めて出すときは、前の絵を待たずに描く
    if (s->frame) {
        wl_callback_destroy(s->frame);
        s->frame = nullptr;
    }
    DrawSurface(s);
    wl_display_flush(s->app->display);
}

void OnLayerClosed(void* data, zwlr_layer_surface_v1*) {
    auto* s = static_cast<Surface*>(data);
    for (auto& o : s->app->outputs) {
        if (o->surface.get() == s) {
            DestroySurface(o.get());
            return;
        }
    }
}

const zwlr_layer_surface_v1_listener kLayerListener = {OnLayerConfigure, OnLayerClosed};

void CreateSurface(Output* o) {
    App* app = o->app;
    if (o->surface || !app->compositor || !app->layerShell) return;
    auto s = std::make_unique<Surface>();
    s->app = app;
    s->outputScale = o->scale;
    s->surface = wl_compositor_create_surface(app->compositor);
    // クリックやドラッグは素通しする（入力を受ける場所を空にする）
    wl_region* empty = wl_compositor_create_region(app->compositor);
    wl_surface_set_input_region(s->surface, empty);
    wl_region_destroy(empty);
    if (app->viewporter) s->viewport = wp_viewporter_get_viewport(app->viewporter, s->surface);
    if (app->fractionalManager && s->viewport) {
        s->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(app->fractionalManager, s->surface);
        wp_fractional_scale_v1_add_listener(s->fractional, &kFractionalListener, s.get());
    }
    s->layer = zwlr_layer_shell_v1_get_layer_surface(app->layerShell, s->surface, o->output,
                                                     ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "pixwall");
    zwlr_layer_surface_v1_add_listener(s->layer, &kLayerListener, s.get());
    // 端から端まで（バーの下も）。ほかの面を押しのけない
    zwlr_layer_surface_v1_set_anchor(s->layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                   ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                                   ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(s->layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(s->layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_set_size(s->layer, 0, 0);
    wl_surface_commit(s->surface);
    o->surface = std::move(s);
}

void DestroySurface(Output* o) {
    Surface* s = o->surface.get();
    if (!s) return;
    if (s->frame) wl_callback_destroy(s->frame);
    o->app->renderer.DestroySurface(s->egl);
    if (s->window) wl_egl_window_destroy(s->window);
    if (s->fractional) wp_fractional_scale_v1_destroy(s->fractional);
    if (s->viewport) wp_viewport_destroy(s->viewport);
    if (s->layer) zwlr_layer_surface_v1_destroy(s->layer);
    wl_surface_destroy(s->surface);
    o->surface.reset();
}

// 画像が読めていれば全部の画面に面を作る。読めなければ面を消し、後ろの壁紙を見せる
void SyncSurfaces(App* app) {
    for (auto& o : app->outputs) {
        if (app->imageOk) {
            CreateSurface(o.get());
        } else {
            DestroySurface(o.get());
        }
    }
    wl_display_flush(app->display);
}

void OnOutputGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*,
                      int32_t) {}
void OnOutputMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
void OnOutputDone(void*, wl_output*) {}
void OnOutputScale(void* data, wl_output*, int32_t factor) {
    auto* o = static_cast<Output*>(data);
    o->scale = factor;
    if (o->surface) {
        o->surface->outputScale = factor;
        DrawSurface(o->surface.get());
    }
}
void OnOutputName(void*, wl_output*, const char*) {}
void OnOutputDescription(void*, wl_output*, const char*) {}

const wl_output_listener kOutputListener = {OnOutputGeometry, OnOutputMode, OnOutputDone,
                                            OnOutputScale,    OnOutputName, OnOutputDescription};

void OnGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto* app = static_cast<App*>(data);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        app->layerShell = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, wp_viewporter_interface.name) == 0) {
        app->viewporter =
            static_cast<wp_viewporter*>(wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
    } else if (std::strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        app->fractionalManager = static_cast<wp_fractional_scale_manager_v1*>(
            wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        auto o = std::make_unique<Output>();
        o->app = app;
        o->name = name;
        o->output =
            static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u)));
        wl_output_add_listener(o->output, &kOutputListener, o.get());
        Output* raw = o.get();
        app->outputs.push_back(std::move(o));
        // 起動したあとにつないだ画面
        if (app->imageOk) CreateSurface(raw);
    }
}

void OnGlobalRemove(void* data, wl_registry*, uint32_t name) {
    auto* app = static_cast<App*>(data);
    for (auto it = app->outputs.begin(); it != app->outputs.end(); ++it) {
        if ((*it)->name == name) {
            DestroySurface(it->get());
            wl_output_release((*it)->output);
            app->outputs.erase(it);
            return;
        }
    }
}

const wl_registry_listener kRegistryListener = {OnGlobal, OnGlobalRemove};

// 画面を数え直して作り直す（「表示を更新」）
void Refresh(App* app) {
    for (auto& o : app->outputs) DestroySurface(o.get());
    wl_display_roundtrip(app->display);
    SyncSurfaces(app);
}

// ---- 設定と切り替え ----

void LoadImage(App* app) {
    app->loadedImage = app->config.image;
    app->index = 0;
    app->delays.clear();
    LoadedImage image;
    app->imageOk = !app->config.image.empty() && LoadImageFile(app->config.image, image);
    if (!app->imageOk) {
        if (!app->config.image.empty()) std::fprintf(stderr, "pixwall: cannot read %s\n", app->config.image.c_str());
        image = {};
    }
    app->delays = image.delays;
    app->autoColor = TopLeftColor(image);
    // GPU に置いたら、元の絵は手放す（image がここで消える）
    app->renderer.Upload(image);
}

void Apply(App* app, const Config& resolved) {
    app->config = resolved;
    if (app->config.image != app->loadedImage) LoadImage(app);
    SyncSurfaces(app);
    DrawAll(app);
    app->stalled = false;
    ScheduleFrame(app);
}

void ScheduleSwitch(App* app);

void Reselect(App* app) {
    const Config next = app->saved.Resolved(LocalMinute(), app->dark);
    if (!(next == app->config)) Apply(app, next);
    ScheduleSwitch(app);
}

// 時刻で切り替えるとき、次に切り替わる時刻に1回だけ起きる。壁時計（CLOCK_REALTIME）で数えるのでスリープの間も進み、
// 時計を変えると取り消される（TFD_TIMER_CANCEL_ON_SET）ので選び直せる
void ScheduleSwitch(App* app) {
    itimerspec spec{};
    const int minutes = app->saved.MinutesToNextSwitch(LocalMinute());
    if (minutes > 0) {
        timespec now{};
        clock_gettime(CLOCK_REALTIME, &now);
        spec.it_value.tv_sec = (now.tv_sec / 60) * 60 + static_cast<time_t>(minutes) * 60;
    }
    timerfd_settime(app->switchTimer, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &spec, nullptr);
}

gboolean OnSwitchTimer(gint fd, GIOCondition, gpointer data) {
    uint64_t count = 0;
    // 時計が変わったときは ECANCELED で返る。どちらでも選び直す
    if (read(fd, &count, sizeof(count)) < 0 && errno != ECANCELED && errno != EAGAIN) return G_SOURCE_CONTINUE;
    Reselect(static_cast<App*>(data));
    return G_SOURCE_CONTINUE;
}

std::string Sibling(const char* name);

// 「ログイン時に起動」。~/.config/autostart/pixwall.desktop を置く・消す（XDG の自動起動。GNOME では拡張機能が動くので外す）
void SyncAutostart(const Config& config) {
    const std::string dir = std::string(g_get_user_config_dir()) + "/autostart";
    const std::string path = dir + "/pixwall.desktop";
    if (!config.launchAtLogin) {
        std::remove(path.c_str());
        return;
    }
    const std::string text = "[Desktop Entry]\nType=Application\nName=pixwall\nExec=" + Sibling("pixwall") +
                             "\nNotShowIn=GNOME;\nNoDisplay=true\nX-GNOME-Autostart-enabled=false\n";
    gchar* current = nullptr;
    if (g_file_get_contents(path.c_str(), &current, nullptr, nullptr) && text == current) {
        g_free(current);
        return;
    }
    g_free(current);
    g_mkdir_with_parents(dir.c_str(), 0700);
    g_file_set_contents(path.c_str(), text.c_str(), -1, nullptr);
}

void ReloadConfig(App* app) {
    app->saved = LoadConfig(app->configPath);
    SyncAutostart(app->saved);
    Apply(app, app->saved.Resolved(LocalMinute(), app->dark));
    ScheduleSwitch(app);
}

void UpdatePower(App* app) {
    bool low = false;
    if (app->power) {
        if (GVariant* v = g_dbus_proxy_get_cached_property(app->power, "ActiveProfile")) {
            low = g_strcmp0(g_variant_get_string(v, nullptr), "power-saver") == 0;
            g_variant_unref(v);
        }
    }
    if (low == app->lowPower) return;
    app->lowPower = low;
    DrawAll(app);
    ScheduleFrame(app);
}

void OnPowerChanged(GDBusProxy*, GVariant*, GStrv, gpointer data) { UpdatePower(static_cast<App*>(data)); }

void OnPortalSetting(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* params,
                     gpointer data) {
    auto* app = static_cast<App*>(data);
    const gchar* ns = nullptr;
    const gchar* key = nullptr;
    GVariant* value = nullptr;
    g_variant_get(params, "(&s&sv)", &ns, &key, &value);
    if (g_strcmp0(ns, "org.freedesktop.appearance") == 0 && g_strcmp0(key, "color-scheme") == 0 &&
        g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) {
        app->dark = g_variant_get_uint32(value) == 1;
        Reselect(app);
    }
    g_variant_unref(value);
}

void OnPrepareForSleep(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* params,
                       gpointer data) {
    gboolean sleeping = FALSE;
    g_variant_get(params, "(b)", &sleeping);
    // スリープから戻った
    if (!sleeping) Reselect(static_cast<App*>(data));
}

void OnZoneChanged(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer data) {
    Reselect(static_cast<App*>(data));
}

// ---- D-Bus で受ける頼みごと（bus.h） ----

void OnMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method, GVariant* params,
              GDBusMethodInvocation* invocation, gpointer data) {
    auto* app = static_cast<App*>(data);
    if (g_strcmp0(method, "Reload") == 0) {
        ReloadConfig(app);
    } else if (g_strcmp0(method, "Refresh") == 0) {
        Refresh(app);
        DrawAll(app);
    } else if (g_strcmp0(method, "SetPaused") == 0) {
        gboolean paused = FALSE;
        g_variant_get(params, "(b)", &paused);
        app->userPaused = paused;
        app->stalled = false;
        ScheduleFrame(app);
    } else if (g_strcmp0(method, "IsPaused") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", app->userPaused));
        return;
    } else if (g_strcmp0(method, "Quit") == 0) {
        g_main_loop_quit(app->loop);
    }
    g_dbus_method_invocation_return_value(invocation, nullptr);
}

const GDBusInterfaceVTable kVTable = {OnMethod, nullptr, nullptr, {}};

void OnBusAcquired(GDBusConnection* connection, const gchar*, gpointer data) {
    GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(kBusXml, nullptr);
    g_dbus_connection_register_object(connection, kBusPath, info->interfaces[0], &kVTable, data, nullptr, nullptr);
    g_dbus_node_info_unref(info);
}

void OnNameLost(GDBusConnection*, const gchar*, gpointer data) {
    // ほかの pixwall（GNOME では拡張機能）がもう動いている
    std::fprintf(stderr, "pixwall: another pixwall is running\n");
    g_main_loop_quit(static_cast<App*>(data)->loop);
}

gboolean OnWayland(gint, GIOCondition condition, gpointer data) {
    auto* app = static_cast<App*>(data);
    if ((condition & (G_IO_ERR | G_IO_HUP)) || wl_display_dispatch(app->display) < 0) {
        std::fprintf(stderr, "pixwall: lost the Wayland connection\n");
        g_main_loop_quit(app->loop);
        return G_SOURCE_REMOVE;
    }
    wl_display_flush(app->display);
    return G_SOURCE_CONTINUE;
}


gboolean OnSignal(gpointer data) {
    g_main_loop_quit(static_cast<App*>(data)->loop);
    return G_SOURCE_REMOVE;
}

std::string Sibling(const char* name) {
    char self[PATH_MAX] = {};
    const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    std::string dir = n > 0 ? std::string(self, n) : std::string();
    dir = dir.substr(0, dir.find_last_of('/') + 1);
    return dir + name;
}

void OpenSettings() {
    const std::string path = Sibling("pixwall-settings");
    gchar* argv[] = {const_cast<gchar*>(path.c_str()), nullptr};
    g_spawn_async(nullptr, argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr);
}

}  // namespace

int main(int argc, char** argv) {
    // もう動いていれば（GNOME では拡張機能が動いていれば）、設定画面を開くだけ
    if (ResidentRunning()) {
        OpenSettings();
        return 0;
    }
    App app;
    gApp = &app;
    app.configPath = ConfigPath();
    app.saved = LoadConfig(app.configPath);
    // 起動の引数で画像を渡すと、それを動かして設定にも書く
    if (argc > 1) {
        app.saved.image = AbsolutePath(argv[1]);
        app.saved.switchBy = SwitchBy::None;
        SaveConfig(app.configPath, app.saved);
    }

    app.display = wl_display_connect(nullptr);
    if (!app.display) {
        std::fprintf(stderr, "pixwall: cannot connect to Wayland\n");
        return 1;
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &kRegistryListener, &app);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);  // 画面の拡大率を受け取る
    if (!app.compositor || !app.layerShell) {
        std::fprintf(stderr,
                     "pixwall: this desktop has no wlr-layer-shell. On GNOME, turn on the pixwall extension "
                     "(gnome-extensions enable pixwall@mooma0750.xyz)\n");
        // 設定画面から拡張機能を動かせる
        OpenSettings();
        return 1;
    }
    if (!app.renderer.Init(app.display)) {
        std::fprintf(stderr, "pixwall: cannot start OpenGL ES (EGL)\n");
        return 1;
    }

    app.loop = g_main_loop_new(nullptr, FALSE);
    app.session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    app.system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    app.busOwner = g_bus_own_name(G_BUS_TYPE_SESSION, kBusName, G_BUS_NAME_OWNER_FLAGS_NONE, OnBusAcquired, nullptr,
                                  OnNameLost, &app, nullptr);
    app.dark = SystemIsDark();
    if (app.session) {
        g_dbus_connection_signal_subscribe(app.session, "org.freedesktop.portal.Desktop",
                                           "org.freedesktop.portal.Settings", "SettingChanged",
                                           "/org/freedesktop/portal/desktop", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                                           OnPortalSetting, &app, nullptr);
    }
    if (app.system) {
        g_dbus_connection_signal_subscribe(app.system, "org.freedesktop.login1", "org.freedesktop.login1.Manager",
                                           "PrepareForSleep", "/org/freedesktop/login1", nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE, OnPrepareForSleep, &app, nullptr);
        app.power = g_dbus_proxy_new_sync(app.system, G_DBUS_PROXY_FLAGS_NONE, nullptr, "net.hadess.PowerProfiles",
                                          "/net/hadess/PowerProfiles", "net.hadess.PowerProfiles", nullptr, nullptr);
        if (app.power) g_signal_connect(app.power, "g-properties-changed", G_CALLBACK(OnPowerChanged), &app);
    }
    // 時間帯を変えると /etc/localtime が差し替わる
    GFile* zone = g_file_new_for_path("/etc/localtime");
    app.zoneMonitor = g_file_monitor_file(zone, G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr);
    g_object_unref(zone);
    if (app.zoneMonitor) g_signal_connect(app.zoneMonitor, "changed", G_CALLBACK(OnZoneChanged), &app);
    app.switchTimer = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
    g_unix_fd_add(app.switchTimer, G_IO_IN, OnSwitchTimer, &app);

    g_unix_fd_add(wl_display_get_fd(app.display), static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP), OnWayland,
                  &app);
    g_unix_signal_add(SIGTERM, OnSignal, &app);
    g_unix_signal_add(SIGINT, OnSignal, &app);

    UpdatePower(&app);
    SyncAutostart(app.saved);
    Apply(&app, app.saved.Resolved(LocalMinute(), app.dark));
    ScheduleSwitch(&app);
    // 画像をまだ選んでいない・読めないときは、設定画面を出す
    if (!app.imageOk) OpenSettings();

    g_main_loop_run(app.loop);

    g_bus_unown_name(app.busOwner);
    for (auto& o : app.outputs) DestroySurface(o.get());
    wl_display_flush(app.display);
    return 0;
}
