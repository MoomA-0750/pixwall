#include "backend.h"

#include <unistd.h>

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

#include "bus.h"
#include "config_file.h"
#include "omarchy.h"
#include "settings/strings.h"
#include "still.h"

namespace pixwall {

namespace {

constexpr const char* kExtensionUuid = "pixwall@mooma0750.xyz";
const double kSpeeds[] = {0.25, 0.5, 1, 2, 4};

QString Q(const std::string& s) { return QString::fromStdString(s); }
QString Q(S key) { return QString::fromUtf8(T(key)); }

std::string FileName(const std::string& path) {
    if (path.empty()) return T(S::NoImage);
    gchar* base = g_path_get_basename(path.c_str());
    std::string out = base;
    g_free(base);
    return out;
}

std::string Number(double v) {
    std::ostringstream s;
    s << v;
    return s.str();
}

std::string LocalPath(const QUrl& url) { return url.isLocalFile() ? url.toLocalFile().toStdString() : std::string(); }

// Omarchy の今の壁紙に組む画像（無ければ空）
std::string OmarchyPair() { return OmarchyImage(OmarchyCurrentDir() + "/background"); }

}  // namespace

// ---- 見本の絵 ----

QImage PreviewProvider::requestImage(const QString&, QSize* size, const QSize&) {
    QMutexLocker lock(&m_mutex);
    if (size) *size = m_image.size();
    return m_image;
}

void PreviewProvider::setImage(const QImage& image) {
    QMutexLocker lock(&m_mutex);
    m_image = image;
}

// ---- 時刻の一覧 ----

int TimeModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_rows.size(); }

QVariant TimeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row& r = m_rows[index.row()];
    switch (role) {
        case TitleRole: return r.title;
        case FileRole: return r.file;
        case HourRole: return r.hour;
        case MinuteRole: return r.minute;
        case NowRole: return r.now;
        case EditingRole: return r.editing;
        default: return {};
    }
}

QHash<int, QByteArray> TimeModel::roleNames() const {
    return {{TitleRole, "clock"}, {FileRole, "file"},  {HourRole, "hour"},
            {MinuteRole, "minute"}, {NowRole, "now"}, {EditingRole, "editing"}};
}

void TimeModel::set(const QList<Row>& rows) {
    if (rows.size() != m_rows.size()) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
        return;
    }
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const Row& a = m_rows[i];
        const Row& b = rows[i];
        if (a.title == b.title && a.file == b.file && a.hour == b.hour && a.minute == b.minute && a.now == b.now &&
            a.editing == b.editing) {
            continue;
        }
        m_rows[i] = b;
        Q_EMIT dataChanged(index(i), index(i));
    }
}

// ---- 中身 ----

Backend::Backend(PreviewProvider* preview, QObject* parent) : QObject(parent), m_preview(preview) {
    m_configPath = ConfigPath();
    m_saved = LoadConfig(m_configPath);
    m_lastWritten = FormatConfig(m_saved);
    m_editing = initialEditing();

    // pixwallctl set などで config.json が変わったら、読み直して画面を合わせる（GTK 版と同じく GIO で見張る。
    // Qt の待ち受けは GLib の上で回るので、GIO の知らせも届く）
    GFile* file = g_file_new_for_path(m_configPath.c_str());
    m_monitor = g_file_monitor_file(file, G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr);
    g_object_unref(file);
    if (m_monitor) {
        g_signal_connect(m_monitor, "changed",
                         G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent event, gpointer self) {
                             if (event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
                                 event == G_FILE_MONITOR_EVENT_RENAMED || event == G_FILE_MONITOR_EVENT_MOVED_IN ||
                                 event == G_FILE_MONITOR_EVENT_CREATED) {
                                 static_cast<Backend*>(self)->onConfigFileChanged();
                             }
                         }),
                         this);
    }
    // Omarchy で壁紙を選び直したら、編集する画像も替える
    if (g_file_test(OmarchyCurrentDir().c_str(), G_FILE_TEST_IS_DIR)) {
        GFile* current = g_file_new_for_path(OmarchyCurrentDir().c_str());
        m_omarchyMonitor = g_file_monitor_directory(current, G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr);
        g_object_unref(current);
        if (m_omarchyMonitor) {
            g_signal_connect(m_omarchyMonitor, "changed",
                             G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer self) {
                                 static_cast<Backend*>(self)->onOmarchyChanged();
                             }),
                             this);
        }
    }
    m_busWatch = g_bus_watch_name(
        G_BUS_TYPE_SESSION, kBusName, G_BUS_NAME_WATCHER_FLAGS_NONE,
        +[](GDBusConnection*, const gchar*, const gchar*, gpointer self) { static_cast<Backend*>(self)->setRunning(true); },
        +[](GDBusConnection*, const gchar*, gpointer self) { static_cast<Backend*>(self)->setRunning(false); }, this,
        nullptr);
    // 開いている間に時刻が進んでも「今」の印が合うよう、30 秒ごとに付け直す（設定画面を開いている間だけ）
    m_timer = g_timeout_add_seconds(
        30,
        +[](gpointer self) -> gboolean {
            static_cast<Backend*>(self)->updateTimes();
            return G_SOURCE_CONTINUE;
        },
        this);
    sync();
}

Backend::~Backend() {
    if (m_timer) g_source_remove(m_timer);
    if (m_busWatch) g_bus_unwatch_name(m_busWatch);
    if (m_monitor) g_object_unref(m_monitor);
    if (m_omarchyMonitor) g_object_unref(m_omarchyMonitor);
}

QVariantMap Backend::strings() const {
    QVariantMap map;
    for (int i = 0; i < static_cast<int>(S::Count); ++i) map.insert(kStringNames[i], Q(static_cast<S>(i)));
    return map;
}

bool Backend::gnome() const {
    const char* desktop = g_getenv("XDG_CURRENT_DESKTOP");
    return desktop && std::strstr(desktop, "GNOME");
}

QStringList Backend::switchModes() const {
    QStringList modes = {Q(S::SwitchNone), Q(S::SwitchTime), Q(S::SwitchAppearance)};
    // Omarchy に付いていくのは、Omarchy があるときだけ選べる（設定がもう omarchy なら出しておく）
    if (g_file_test(OmarchyCurrentDir().c_str(), G_FILE_TEST_IS_DIR) || omarchyMode()) modes << Q(S::SwitchOmarchy);
    return modes;
}

QStringList Backend::fitLabels() const {
    return {Q(S::FitInteger), Q(S::FitFit), Q(S::FitFill), Q(S::FitFree)};
}

QStringList Backend::speedLabels() const {
    QStringList out;
    for (double s : kSpeeds) out << Q(Format(S::SpeedTimes, Number(s)));
    return out;
}

// ---- 設定を書く ----

void Backend::commit() {
    m_lastWritten = FormatConfig(m_saved);
    SaveConfig(m_configPath, m_saved);
    if (GVariant* reply = CallResident("Reload")) g_variant_unref(reply);
}

Slot Backend::editingSlot() const {
    return omarchyMode() ? m_saved.OmarchySlot(OmarchyPair()) : m_saved.GetSlot(m_editing);
}

void Backend::setEditingSlot(const Slot& slot) {
    if (omarchyMode()) {
        m_saved.SetOmarchySlot(slot);
    } else {
        m_saved.SetSlot(m_editing, slot);
    }
    commit();
}

SlotRef Backend::initialEditing() const {
    switch (m_saved.switchBy) {
        case SwitchBy::Time: {
            const int i = m_saved.ActiveTimeIndex(LocalMinute());
            return i >= 0 ? SlotRef{SlotRef::Time, i} : SlotRef{};
        }
        case SwitchBy::Appearance:
            return SlotRef{SystemIsDark() ? SlotRef::Dark : SlotRef::Light, 0};
        default:
            return SlotRef{};
    }
}

// ---- 見本 ----

const LoadedImage* Backend::previewImage(const std::string& path) const {
    if (path != m_previewPath) {
        m_previewPath = path;
        auto image = std::make_unique<LoadedImage>();
        m_previewImage = !path.empty() && LoadImageFile(path, *image) ? std::move(image) : nullptr;
    }
    return m_previewImage.get();
}

int Backend::backgroundFor(const LoadedImage* image) const {
    if (m_saved.customBackground && m_saved.backgroundColor >= 0) return m_saved.backgroundColor;
    return image ? TopLeftColor(*image) : 0;
}

void Backend::updatePreview() {
    const Slot slot = editingSlot();
    const LoadedImage* image = previewImage(slot.image);
    // 主の画面の形で、長い辺 480 の絵にする（ドットはぼかさない）
    const std::vector<ScreenSize> screens = MonitorSizes();
    const ScreenSize screen = screens.empty() ? ScreenSize{1920, 1080} : screens[0];
    const double scale = 480.0 / std::max(screen.width, screen.height);
    const int w = std::max(1, static_cast<int>(std::lround(screen.width * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(screen.height * scale)));
    const int frames = image ? static_cast<int>(image->frames.size()) : 0;
    m_frame = std::clamp(m_frame, 1, std::max(1, frames));
    const int rgb = backgroundFor(image);
    const std::vector<uint8_t> pixels =
        RenderStill(image ? image->frames[m_frame - 1].data() : nullptr, image ? image->width : 0,
                    image ? image->height : 0, w, h, slot.placement, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    // RenderStill は premultiplied の BGRA。リトルエンディアンでは ARGB32 の並びと同じ
    QImage picture(pixels.data(), w, h, w * 4, QImage::Format_ARGB32_Premultiplied);
    m_preview->setImage(picture.copy());
    m_previewW = w;
    m_previewH = h;
    ++m_previewSerial;
    Q_EMIT previewChanged();
}

QUrl Backend::previewSource() const { return QUrl("image://preview/" + QString::number(m_previewSerial)); }

int Backend::frameCount() const {
    const LoadedImage* image = previewImage(editingSlot().image);
    return image ? static_cast<int>(image->frames.size()) : 0;
}

QString Backend::frameTitle() const {
    return Q(Format(S::FrameOf, std::to_string(frameCount()), std::to_string(m_frame)));
}

void Backend::setFrame(int frame) {
    if (frame == m_frame) return;
    m_frame = frame;
    updatePreview();
}

// ---- 画面に出すもの ----

QString Backend::imageHeading() const {
    if (omarchyMode()) return Q(S::ImageOmarchy);
    switch (m_editing.kind) {
        case SlotRef::Time:
            return Q(Format(S::ImageFrom, FormatClock(m_saved.timeSlots[m_editing.index].start)));
        case SlotRef::Light:
            return Q(S::ImageLight);
        case SlotRef::Dark:
            return Q(S::ImageDark);
        default:
            return Q(S::ImageMain);
    }
}

QString Backend::imageSubtitle() const {
    const Slot slot = editingSlot();
    if (omarchyMode()) {
        // 画像は Omarchy の壁紙で決まるので選ばない
        return slot.image.empty() ? Q(S::OmarchyNoPair) : Q(Format(S::OmarchyHint, FileName(slot.image)));
    }
    return Q(FileName(slot.image));
}

// Omarchy で組になる GIF が無いときは、見本・書き出し・並べ方を出さない（描かないので効かない）
bool Backend::paired() const { return !omarchyMode() || !editingSlot().image.empty(); }

int Backend::fit() const { return static_cast<int>(editingSlot().placement.fit); }
int Backend::position() const { return static_cast<int>(std::lround(editingSlot().placement.position * 100)); }
int Backend::freeSize() const { return static_cast<int>(std::lround(editingSlot().placement.freeSize * 100)); }
int Backend::freeX() const { return static_cast<int>(std::lround(editingSlot().placement.freeX * 100)); }
int Backend::freeY() const { return static_cast<int>(std::lround(editingSlot().placement.freeY * 100)); }

QColor Backend::backgroundColor() const {
    const int rgb =
        m_saved.backgroundColor >= 0 ? m_saved.backgroundColor : backgroundFor(previewImage(editingSlot().image));
    return QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

int Backend::speedIndex() const {
    int speed = 2;
    for (int i = 0; i < static_cast<int>(std::size(kSpeeds)); ++i) {
        if (std::abs(kSpeeds[i] - m_saved.speed) < 1e-9) speed = i;
    }
    return speed;
}

QString Backend::quitLabel() const { return Q(m_saved.keepWallpaperOnQuit ? S::QuitKeeps : S::QuitLabel); }

void Backend::updateTimes() {
    QList<TimeModel::Row> rows;
    if (m_saved.switchBy == SwitchBy::Time) {
        const int active = m_saved.ActiveTimeIndex(LocalMinute());
        for (size_t i = 0; i < m_saved.timeSlots.size(); ++i) {
            const TimeSlot& t = m_saved.timeSlots[i];
            rows.append({Q(FormatClock(t.start)), Q(FileName(t.slot.image)), t.start / 60, t.start % 60,
                         static_cast<int>(i) == active,
                         m_editing.kind == SlotRef::Time && m_editing.index == static_cast<int>(i)});
        }
    }
    m_times.set(rows);
}

void Backend::sync() {
    const Config& c = m_saved;
    if (m_editing.kind == SlotRef::Time &&
        (c.switchBy != SwitchBy::Time || m_editing.index >= static_cast<int>(c.timeSlots.size()))) {
        m_editing = initialEditing();
    }
    if ((m_editing.kind == SlotRef::Light || m_editing.kind == SlotRef::Dark) && c.switchBy != SwitchBy::Appearance) {
        m_editing = initialEditing();
    }
    if (m_editing.kind == SlotRef::Main && c.switchBy != SwitchBy::None) m_editing = initialEditing();
    updateTimes();
    updatePreview();
    Q_EMIT changed();
}

// ---- 変える ----

void Backend::refresh() {
    if (GVariant* reply = CallResident("Refresh")) g_variant_unref(reply);
}

// 常駐側を動かす。GNOME は拡張機能を有効にし、ほかは pixwall を起動する
void Backend::start() {
    if (gnome()) {
        GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
        GVariant* reply = bus ? g_dbus_connection_call_sync(bus, "org.gnome.Shell.Extensions",
                                                            "/org/gnome/Shell/Extensions", "org.gnome.Shell.Extensions",
                                                            "EnableExtension", g_variant_new("(s)", kExtensionUuid),
                                                            G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 5000,
                                                            nullptr, nullptr)
                              : nullptr;
        gboolean ok = FALSE;
        if (reply) {
            g_variant_get(reply, "(b)", &ok);
            g_variant_unref(reply);
        }
        if (bus) g_object_unref(bus);
        // 入れたばかりの拡張機能は、ログインし直すまでシェルが知らない
        if (!ok) Q_EMIT toast(Q(S::StartFailedGnome));
        return;
    }
    char self[4096] = {};
    const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    std::string path = n > 0 ? std::string(self, n) : std::string();
    path = path.substr(0, path.find_last_of('/') + 1) + "pixwall";
    gchar* argv[] = {const_cast<gchar*>(path.c_str()), nullptr};
    g_spawn_async(nullptr, argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr);
}

void Backend::setRunning(bool running) {
    m_running = running;
    m_paused = false;
    if (running) {
        if (GVariant* reply = CallResident("IsPaused")) {
            gboolean paused = FALSE;
            g_variant_get(reply, "(b)", &paused);
            g_variant_unref(reply);
            m_paused = paused;
        }
    }
    Q_EMIT runningChanged();
}

void Backend::onConfigFileChanged() {
    const Config next = LoadConfig(m_configPath);
    if (FormatConfig(next) == m_lastWritten || next == m_saved) return;
    m_saved = next;
    m_lastWritten = FormatConfig(next);
    sync();
}

void Backend::onOmarchyChanged() {
    if (omarchyMode()) sync();
}

void Backend::setSwitchBy(int mode) {
    if (mode < 0 || mode > static_cast<int>(SwitchBy::Omarchy) || mode == switchBy()) return;
    m_saved.SwitchTo(static_cast<SwitchBy>(mode));
    m_editing = initialEditing();
    commit();
    sync();
}

// 行を選ぶと、その時刻の画像と並べ方を下で編集する
void Backend::selectTime(int index) {
    if (index < 0 || index >= static_cast<int>(m_saved.timeSlots.size())) return;
    m_editing = SlotRef{SlotRef::Time, index};
    sync();
}

void Backend::setTimeStart(int index, int hour, int minute) {
    if (index < 0 || index >= static_cast<int>(m_saved.timeSlots.size())) return;
    const int start = std::clamp(hour, 0, 23) * 60 + std::clamp(minute, 0, 59);
    if (m_saved.timeSlots[index].start == start) return;
    m_saved.timeSlots[index].start = start;
    commit();
    updateTimes();
    Q_EMIT changed();
}

void Backend::removeTime(int index) {
    // 最後の1つは消さない（時刻で切り替えるのに、時刻が無くなる）
    if (index < 0 || index >= static_cast<int>(m_saved.timeSlots.size()) || m_saved.timeSlots.size() <= 1) return;
    m_saved.timeSlots.erase(m_saved.timeSlots.begin() + index);
    if (m_editing.kind == SlotRef::Time && m_editing.index >= index) {
        m_editing = m_editing.index == index ? initialEditing() : SlotRef{SlotRef::Time, m_editing.index - 1};
    }
    commit();
    sync();
}

void Backend::addTime() {
    // 今の時刻から、今編集しているものと同じ画像と並べ方で始める
    m_saved.timeSlots.push_back(TimeSlot{LocalMinute(), editingSlot()});
    m_editing = SlotRef{SlotRef::Time, static_cast<int>(m_saved.timeSlots.size()) - 1};
    commit();
    sync();
}

void Backend::selectAppearance(bool dark) {
    m_editing = SlotRef{dark ? SlotRef::Dark : SlotRef::Light, 0};
    sync();
}

void Backend::chooseImage(const QUrl& file) {
    const std::string chosen = LocalPath(file);
    if (chosen.empty()) return;
    // 読めない画像は設定に書かない
    LoadedImage image;
    if (!LoadImageFile(chosen, image)) {
        Q_EMIT toast(Q(Format(S::CouldNotRead, FileName(chosen))));
        return;
    }
    Slot slot = editingSlot();
    slot.image = chosen;
    setEditingSlot(slot);
    m_frame = 1;
    sync();
}

QString Backend::exportName() const {
    const Slot slot = editingSlot();
    const std::vector<ScreenSize> screens = MonitorSizes();
    if (slot.image.empty() || screens.empty()) return {};
    gchar* stem = g_path_get_basename(slot.image.c_str());
    std::string name = stem;
    g_free(stem);
    if (const size_t dot = name.find_last_of('.'); dot != std::string::npos && dot > 0) name.resize(dot);
    return Q(name + "-" + std::to_string(screens[0].width) + "x" + std::to_string(screens[0].height) + ".png");
}

void Backend::exportStill(const QUrl& file) {
    const std::string path = LocalPath(file);
    const Slot slot = editingSlot();
    const LoadedImage* image = previewImage(slot.image);
    const bool ok = !path.empty() && image &&
                    ExportStill(*image, slot.image, static_cast<size_t>(m_frame - 1), slot.placement,
                                backgroundFor(image), MonitorSizes(), path);
    Q_EMIT toast(Q(Format(ok ? S::Exported : S::ExportFailed, FileName(path))));
}

void Backend::changePlacement(const std::function<void(Placement&)>& change) {
    Slot slot = editingSlot();
    change(slot.placement);
    if (slot == editingSlot()) return;
    setEditingSlot(slot);
    updatePreview();
    Q_EMIT changed();
}

void Backend::setFit(int fit) {
    if (fit < 0 || fit > static_cast<int>(Fit::Free)) return;
    changePlacement([fit](Placement& p) { p.fit = static_cast<Fit>(fit); });
}

void Backend::setPosition(int v) { changePlacement([v](Placement& p) { p.position = v / 100.0; }); }
void Backend::setFreeSize(int v) { changePlacement([v](Placement& p) { p.freeSize = v / 100.0; }); }
void Backend::setFreeX(int v) { changePlacement([v](Placement& p) { p.freeX = v / 100.0; }); }
void Backend::setFreeY(int v) { changePlacement([v](Placement& p) { p.freeY = v / 100.0; }); }

void Backend::setCustomBackground(bool custom) {
    if (custom == m_saved.customBackground) return;
    m_saved.customBackground = custom;
    // 初めて「自分で選ぶ」にしたときは、今の自動の色から始める
    if (custom && m_saved.backgroundColor < 0) m_saved.backgroundColor = backgroundFor(previewImage(editingSlot().image));
    commit();
    sync();
}

void Backend::setBackgroundColor(const QColor& color) {
    const int rgb = (color.red() << 16) | (color.green() << 8) | color.blue();
    if (rgb == m_saved.backgroundColor) return;
    m_saved.backgroundColor = rgb;
    commit();
    updatePreview();
    Q_EMIT changed();
}

void Backend::setSpeed(int index) {
    m_saved.speed = kSpeeds[std::clamp<int>(index, 0, static_cast<int>(std::size(kSpeeds)) - 1)];
    commit();
    Q_EMIT changed();
}

void Backend::setPauseOnLowPower(bool on) {
    m_saved.pauseOnLowPower = on;
    commit();
    Q_EMIT changed();
}

void Backend::setPaused(bool paused) {
    if (GVariant* reply = CallResident("SetPaused", g_variant_new("(b)", paused))) g_variant_unref(reply);
    m_paused = paused;
    Q_EMIT runningChanged();
}

void Backend::setLaunchAtLogin(bool on) {
    m_saved.launchAtLogin = on;
    commit();
    Q_EMIT changed();
}

void Backend::setKeepWallpaperOnQuit(bool on) {
    m_saved.keepWallpaperOnQuit = on;
    commit();
    Q_EMIT changed();
}

void Backend::quit() {
    if (GVariant* reply = CallResident("Quit")) g_variant_unref(reply);
    Q_EMIT quitRequested();
}

}  // namespace pixwall
