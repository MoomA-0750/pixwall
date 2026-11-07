#include "bus.h"

namespace pixwall {

const char* const kBusXml =
    "<node>"
    "  <interface name='xyz.mooma0750.pixwall'>"
    "    <method name='Reload'/>"
    "    <method name='Refresh'/>"
    "    <method name='SetPaused'><arg type='b' name='paused' direction='in'/></method>"
    "    <method name='IsPaused'><arg type='b' name='paused' direction='out'/></method>"
    "    <method name='Quit'/>"
    "  </interface>"
    "</node>";

namespace {

GDBusConnection* Bus() {
    static GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    return bus;
}

}  // namespace

bool ResidentRunning() {
    if (!Bus()) return false;
    GVariant* reply = g_dbus_connection_call_sync(Bus(), "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                  "org.freedesktop.DBus", "NameHasOwner",
                                                  g_variant_new("(s)", kBusName), G_VARIANT_TYPE("(b)"),
                                                  G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
    if (!reply) return false;
    gboolean has = FALSE;
    g_variant_get(reply, "(b)", &has);
    g_variant_unref(reply);
    return has;
}

GVariant* CallResident(const char* method, GVariant* args) {
    if (!Bus() || !ResidentRunning()) {
        if (args) g_variant_unref(g_variant_ref_sink(args));
        return nullptr;
    }
    // 起動し直しで名前が無い間に呼んでも、D-Bus に起こしてもらわない
    return g_dbus_connection_call_sync(Bus(), kBusName, kBusPath, kBusInterface, method, args, nullptr,
                                       G_DBUS_CALL_FLAGS_NO_AUTO_START, 5000, nullptr, nullptr);
}

}  // namespace pixwall
