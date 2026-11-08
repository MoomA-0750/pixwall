// 画面の大きさを GDK で数える（pixwallctl と GTK 版の設定画面）。QML 版の設定画面は settings/qt/monitors.cpp で Qt に訊く
#include <gtk/gtk.h>

#include <cmath>

#include "still.h"

namespace pixwall {

std::vector<ScreenSize> MonitorSizes() {
    std::vector<ScreenSize> sizes;
    if (!gtk_init_check()) return sizes;
    GListModel* monitors = gdk_display_get_monitors(gdk_display_get_default());
    for (guint i = 0; i < g_list_model_get_n_items(monitors); ++i) {
        auto* monitor = static_cast<GdkMonitor*>(g_list_model_get_item(monitors, i));
        GdkRectangle r;
        gdk_monitor_get_geometry(monitor, &r);
        const double scale = gdk_monitor_get_scale(monitor);
        sizes.push_back({static_cast<int>(std::lround(r.width * scale)), static_cast<int>(std::lround(r.height * scale))});
        g_object_unref(monitor);
    }
    return sizes;
}

}  // namespace pixwall
