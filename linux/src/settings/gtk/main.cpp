// pixwall の設定画面（GTK4・libadwaita）。壁紙を動かすプロセス（Hyprland などは pixwall、GNOME は拡張機能）とは別で、
// 閉じたら終わる。変えるとすぐ config.json に書き、常駐側に D-Bus で読み直してもらう（Mac・Windows 版と同じ中身）
#include <adwaita.h>
#include <gtk/gtk.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "bus.h"
#include "config.h"
#include "config_file.h"
#include "image_loader.h"
#include "omarchy.h"
#include "still.h"
#include "settings/strings.h"

using namespace pixwall;

namespace {

constexpr const char* kExtensionUuid = "pixwall@mooma0750.xyz";
const double kSpeeds[] = {0.25, 0.5, 1, 2, 4};

// GTK の知らせに C++ のラムダをつなぐ（ウィジェットが消えるときにラムダも消す）
template <typename... Args>
void On(gpointer object, const char* signal, std::type_identity_t<std::function<void(Args...)>> fn) {
    auto* holder = new std::function<void(Args...)>(std::move(fn));
    g_signal_connect_data(
        object, signal, G_CALLBACK(+[](gpointer, Args... args, gpointer data) {
            (*static_cast<std::function<void(Args...)>*>(data))(args...);
        }),
        holder, +[](gpointer data, GClosure*) { delete static_cast<std::function<void(Args...)>*>(data); },
        G_CONNECT_DEFAULT);
}

void OnClick(GtkWidget* button, std::function<void()> fn) { On<>(button, "clicked", std::move(fn)); }

bool IsGnome() {
    const char* desktop = g_getenv("XDG_CURRENT_DESKTOP");
    return desktop && std::strstr(desktop, "GNOME");
}

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

struct TimeRow {
    GtkWidget* row = nullptr;
    GtkWidget* check = nullptr;
    GtkWidget* hour = nullptr;
    GtkWidget* minute = nullptr;
    GtkWidget* now = nullptr;
};

struct Ui {
    AdwApplication* app = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* toasts = nullptr;
    GtkWidget* banner = nullptr;
    GtkWidget* switchGroup = nullptr;
    GtkWidget* switchCombo = nullptr;
    std::vector<TimeRow> timeRows;
    GtkWidget* addTimeRow = nullptr;
    GtkWidget* appearanceRow = nullptr;
    GtkWidget* lightButton = nullptr;
    GtkWidget* darkButton = nullptr;
    GtkWidget* imageGroup = nullptr;
    GtkWidget* imageRow = nullptr;
    GtkWidget* chooseButton = nullptr;
    GtkWidget* previewRow = nullptr;
    GtkWidget* exportRow = nullptr;
    GtkWidget* placementRows[5] = {};  // 並べ方の行（Omarchy で組む GIF が無いときは触れない）
    GtkWidget* preview = nullptr;
    GtkWidget* frameRow = nullptr;
    GtkWidget* frameScale = nullptr;
    GtkWidget* fitCombo = nullptr;
    GtkWidget* positionRow = nullptr;
    GtkWidget* positionScale = nullptr;
    GtkWidget* sizeRow = nullptr;
    GtkWidget* sizeScale = nullptr;
    GtkWidget* xRow = nullptr;
    GtkWidget* xScale = nullptr;
    GtkWidget* yRow = nullptr;
    GtkWidget* yScale = nullptr;
    GtkWidget* backgroundCombo = nullptr;
    GtkWidget* colorButton = nullptr;
    GtkWidget* speedCombo = nullptr;
    GtkWidget* lowPowerRow = nullptr;
    GtkWidget* keepRow = nullptr;
    GtkWidget* quitRow = nullptr;
    GtkWidget* pauseRow = nullptr;
    GtkWidget* loginRow = nullptr;

    std::string configPath;
    Config saved;
    std::string lastWritten;  // 最後にこちらで書いた中身（自分で書いたのを読み直さない）
    SlotRef editing;
    bool syncing = false;     // 画面を設定に合わせている間は、変えた知らせを無視する
    bool running = false;

    std::string previewPath;  // 見本に読んだ画像
    std::unique_ptr<LoadedImage> previewImage;
    GFileMonitor* monitor = nullptr;
    GFileMonitor* omarchyMonitor = nullptr;
};

Ui ui;

void Sync();
void RebuildTimes();

void Toast(const std::string& text) {
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(ui.toasts), adw_toast_new(text.c_str()));
}

// ---- 設定を書く ----

void Commit() {
    ui.lastWritten = FormatConfig(ui.saved);
    SaveConfig(ui.configPath, ui.saved);
    if (GVariant* reply = CallResident("Reload")) g_variant_unref(reply);
}

bool OmarchyMode() { return ui.saved.switchBy == SwitchBy::Omarchy; }

// Omarchy の今の壁紙に組む画像（無ければ空）
std::string OmarchyPair() { return OmarchyImage(OmarchyCurrentDir() + "/background"); }

// Omarchy で切り替えるときは、今の Omarchy の壁紙に組む画像の並べ方を編集する
Slot EditingSlot() { return OmarchyMode() ? ui.saved.OmarchySlot(OmarchyPair()) : ui.saved.GetSlot(ui.editing); }

void SetEditingSlot(const Slot& slot) {
    if (OmarchyMode()) {
        ui.saved.SetOmarchySlot(slot);
    } else {
        ui.saved.SetSlot(ui.editing, slot);
    }
    Commit();
}

SlotRef InitialEditing() {
    switch (ui.saved.switchBy) {
        case SwitchBy::Time: {
            const int i = ui.saved.ActiveTimeIndex(LocalMinute());
            return i >= 0 ? SlotRef{SlotRef::Time, i} : SlotRef{};
        }
        case SwitchBy::Appearance:
            return SlotRef{SystemIsDark() ? SlotRef::Dark : SlotRef::Light, 0};
        default:
            return SlotRef{};
    }
}

// ---- 見本 ----

const LoadedImage* PreviewImage(const std::string& path) {
    if (path != ui.previewPath) {
        ui.previewPath = path;
        auto image = std::make_unique<LoadedImage>();
        ui.previewImage = !path.empty() && LoadImageFile(path, *image) ? std::move(image) : nullptr;
    }
    return ui.previewImage.get();
}

int BackgroundFor(const LoadedImage* image) {
    if (ui.saved.customBackground && ui.saved.backgroundColor >= 0) return ui.saved.backgroundColor;
    return image ? TopLeftColor(*image) : 0;
}

void UpdatePreview() {
    const Slot slot = EditingSlot();
    const LoadedImage* image = PreviewImage(slot.image);
    // 主の画面の形で、長い辺 480 の絵にする（ドットはぼかさない）
    std::vector<ScreenSize> screens = MonitorSizes();
    const ScreenSize screen = screens.empty() ? ScreenSize{1920, 1080} : screens[0];
    const double scale = 480.0 / std::max(screen.width, screen.height);
    const int w = std::max(1, static_cast<int>(std::lround(screen.width * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(screen.height * scale)));
    size_t frame = 0;
    if (image) {
        frame = std::min(static_cast<size_t>(gtk_range_get_value(GTK_RANGE(ui.frameScale))) - 1,
                         image->frames.size() - 1);
    }
    const int rgb = BackgroundFor(image);
    const std::vector<uint8_t> pixels =
        RenderStill(image ? image->frames[frame].data() : nullptr, image ? image->width : 0,
                    image ? image->height : 0, w, h, slot.placement, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    GBytes* bytes = g_bytes_new(pixels.data(), pixels.size());
    GdkTexture* texture = gdk_memory_texture_new(w, h, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, w * 4);
    g_bytes_unref(bytes);
    gtk_picture_set_paintable(GTK_PICTURE(ui.preview), GDK_PAINTABLE(texture));
    g_object_unref(texture);
    gtk_widget_set_size_request(ui.preview, w, h);

    const size_t frames = image ? image->frames.size() : 0;
    gtk_widget_set_visible(ui.frameRow, frames > 1);
    if (frames > 1) {
        gtk_range_set_range(GTK_RANGE(ui.frameScale), 1, static_cast<double>(frames));
        adw_preferences_row_set_title(
            ADW_PREFERENCES_ROW(ui.frameRow),
            Format(S::FrameOf, std::to_string(frames), std::to_string(frame + 1)).c_str());
    }
}

// ---- 画面を設定に合わせる ----

std::string ImageHeading() {
    if (OmarchyMode()) return T(S::ImageOmarchy);
    switch (ui.editing.kind) {
        case SlotRef::Time:
            return Format(S::ImageFrom, FormatClock(ui.saved.timeSlots[ui.editing.index].start));
        case SlotRef::Light:
            return T(S::ImageLight);
        case SlotRef::Dark:
            return T(S::ImageDark);
        default:
            return T(S::ImageMain);
    }
}

void Sync() {
    ui.syncing = true;
    const Config& c = ui.saved;
    if (ui.editing.kind == SlotRef::Time && (c.switchBy != SwitchBy::Time ||
                                             ui.editing.index >= static_cast<int>(c.timeSlots.size()))) {
        ui.editing = InitialEditing();
    }
    if ((ui.editing.kind == SlotRef::Light || ui.editing.kind == SlotRef::Dark) && c.switchBy != SwitchBy::Appearance) {
        ui.editing = InitialEditing();
    }
    if (ui.editing.kind == SlotRef::Main && c.switchBy != SwitchBy::None) ui.editing = InitialEditing();

    adw_combo_row_set_selected(ADW_COMBO_ROW(ui.switchCombo), static_cast<guint>(c.switchBy));
    RebuildTimes();
    gtk_widget_set_visible(ui.appearanceRow, c.switchBy == SwitchBy::Appearance);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.lightButton), ui.editing.kind == SlotRef::Light);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.darkButton), ui.editing.kind == SlotRef::Dark);

    const Slot slot = EditingSlot();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(ui.imageGroup), ImageHeading().c_str());
    if (OmarchyMode()) {
        // 画像は Omarchy の壁紙で決まるので選ばない。組が無ければ並べ方も触れない
        adw_action_row_set_subtitle(
            ADW_ACTION_ROW(ui.imageRow),
            slot.image.empty() ? T(S::OmarchyNoPair) : Format(S::OmarchyHint, FileName(slot.image)).c_str());
    } else {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(ui.imageRow), FileName(slot.image).c_str());
    }
    gtk_widget_set_visible(ui.chooseButton, !OmarchyMode());
    const Placement& p = slot.placement;
    adw_combo_row_set_selected(ADW_COMBO_ROW(ui.fitCombo), static_cast<guint>(p.fit));
    gtk_widget_set_visible(ui.positionRow, p.fit == Fit::Fit || p.fit == Fit::Fill);
    gtk_widget_set_visible(ui.sizeRow, p.fit == Fit::Free);
    gtk_widget_set_visible(ui.xRow, p.fit == Fit::Free);
    gtk_widget_set_visible(ui.yRow, p.fit == Fit::Free);
    // Omarchy で組になる GIF が無いときは、見本・書き出し・並べ方を出さない（描かないので効かない）
    const bool paired = !OmarchyMode() || !slot.image.empty();
    gtk_widget_set_visible(ui.previewRow, paired);
    gtk_widget_set_visible(ui.exportRow, paired);
    if (!paired) {
        for (GtkWidget* row : ui.placementRows) gtk_widget_set_visible(row, FALSE);
    } else {
        gtk_widget_set_visible(ui.fitCombo, TRUE);
    }
    gtk_range_set_value(GTK_RANGE(ui.positionScale), std::round(p.position * 100));
    gtk_range_set_value(GTK_RANGE(ui.sizeScale), std::round(p.freeSize * 100));
    gtk_range_set_value(GTK_RANGE(ui.xScale), std::round(p.freeX * 100));
    gtk_range_set_value(GTK_RANGE(ui.yScale), std::round(p.freeY * 100));

    adw_combo_row_set_selected(ADW_COMBO_ROW(ui.backgroundCombo), c.customBackground ? 1 : 0);
    gtk_widget_set_visible(ui.colorButton, c.customBackground);
    const int rgb = c.backgroundColor >= 0 ? c.backgroundColor : BackgroundFor(PreviewImage(slot.image));
    GdkRGBA color = {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1};
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(ui.colorButton), &color);
    guint speed = 2;
    for (guint i = 0; i < G_N_ELEMENTS(kSpeeds); ++i) {
        if (std::abs(kSpeeds[i] - c.speed) < 1e-9) speed = i;
    }
    adw_combo_row_set_selected(ADW_COMBO_ROW(ui.speedCombo), speed);
    adw_switch_row_set_active(ADW_SWITCH_ROW(ui.lowPowerRow), c.pauseOnLowPower);
    adw_switch_row_set_active(ADW_SWITCH_ROW(ui.loginRow), c.launchAtLogin);
    adw_switch_row_set_active(ADW_SWITCH_ROW(ui.keepRow), c.keepWallpaperOnQuit);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ui.quitRow), T(c.keepWallpaperOnQuit ? S::QuitKeeps : S::QuitLabel));
    UpdatePreview();
    ui.syncing = false;
}

// ---- 時刻の一覧 ----

void SelectEditing(SlotRef ref) {
    ui.editing = ref;
    Sync();
}

void UpdateTimeLabels() {
    const int active = ui.saved.ActiveTimeIndex(LocalMinute());
    for (size_t i = 0; i < ui.timeRows.size(); ++i) {
        const TimeSlot& t = ui.saved.timeSlots[i];
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ui.timeRows[i].row), FormatClock(t.start).c_str());
        adw_action_row_set_subtitle(ADW_ACTION_ROW(ui.timeRows[i].row), FileName(t.slot.image).c_str());
        gtk_widget_set_visible(ui.timeRows[i].now, static_cast<int>(i) == active);
    }
}

GtkWidget* Spin(int max, int value) {
    GtkWidget* spin = gtk_spin_button_new_with_range(0, max, 1);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    gtk_widget_set_valign(spin, GTK_ALIGN_CENTER);
    // 2 けたで出す
    g_signal_connect(spin, "output", G_CALLBACK(+[](GtkSpinButton* s, gpointer) -> gboolean {
                         gchar* text = g_strdup_printf("%02d", gtk_spin_button_get_value_as_int(s));
                         gtk_editable_set_text(GTK_EDITABLE(s), text);
                         g_free(text);
                         return TRUE;
                     }),
                     nullptr);
    return spin;
}

void RebuildTimes() {
    for (auto& r : ui.timeRows) adw_preferences_group_remove(ADW_PREFERENCES_GROUP(ui.switchGroup), r.row);
    ui.timeRows.clear();
    const bool shown = ui.saved.switchBy == SwitchBy::Time;
    gtk_widget_set_visible(ui.addTimeRow, shown);
    if (!shown) return;
    adw_preferences_group_remove(ADW_PREFERENCES_GROUP(ui.switchGroup), ui.addTimeRow);
    GtkWidget* first = nullptr;
    for (size_t i = 0; i < ui.saved.timeSlots.size(); ++i) {
        const int index = static_cast<int>(i);
        TimeRow r;
        r.row = adw_action_row_new();
        // 行を選ぶと、その時刻の画像と並べ方を下で編集する
        r.check = gtk_check_button_new();
        if (first) {
            gtk_check_button_set_group(GTK_CHECK_BUTTON(r.check), GTK_CHECK_BUTTON(first));
        } else {
            first = r.check;
        }
        gtk_check_button_set_active(GTK_CHECK_BUTTON(r.check),
                                    ui.editing.kind == SlotRef::Time && ui.editing.index == index);
        gtk_widget_set_valign(r.check, GTK_ALIGN_CENTER);
        adw_action_row_add_prefix(ADW_ACTION_ROW(r.row), r.check);
        adw_action_row_set_activatable_widget(ADW_ACTION_ROW(r.row), r.check);
        On<>(r.check, "toggled", [index, check = r.check] {
            if (ui.syncing || !gtk_check_button_get_active(GTK_CHECK_BUTTON(check))) return;
            // 一覧を作り直すと押した部品が消えるので、手が空いてから
            g_idle_add_once(+[](gpointer data) { SelectEditing(SlotRef{SlotRef::Time, GPOINTER_TO_INT(data)}); },
                            GINT_TO_POINTER(index));
        });

        r.now = gtk_label_new(T(S::Now));
        gtk_widget_add_css_class(r.now, "accent");
        adw_action_row_add_suffix(ADW_ACTION_ROW(r.row), r.now);

        const int start = ui.saved.timeSlots[i].start;
        r.hour = Spin(23, start / 60);
        r.minute = Spin(59, start % 60);
        gtk_accessible_update_property(GTK_ACCESSIBLE(r.hour), GTK_ACCESSIBLE_PROPERTY_LABEL, T(S::StartTime), -1);
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(box), r.hour);
        gtk_box_append(GTK_BOX(box), gtk_label_new(":"));
        gtk_box_append(GTK_BOX(box), r.minute);
        adw_action_row_add_suffix(ADW_ACTION_ROW(r.row), box);
        const auto changed = [index, hour = r.hour, minute = r.minute] {
            if (ui.syncing) return;
            ui.saved.timeSlots[index].start = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(hour)) * 60 +
                                              gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(minute));
            Commit();
            UpdateTimeLabels();
            if (ui.editing.kind == SlotRef::Time && ui.editing.index == index) {
                adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(ui.imageGroup), ImageHeading().c_str());
            }
        };
        On<>(r.hour, "value-changed", changed);
        On<>(r.minute, "value-changed", changed);

        GtkWidget* remove = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_widget_set_tooltip_text(remove, T(S::RemoveTime));
        gtk_widget_add_css_class(remove, "flat");
        gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);
        // 最後の1つは消さない（時刻で切り替えるのに、時刻が無くなる）
        gtk_widget_set_sensitive(remove, ui.saved.timeSlots.size() > 1);
        OnClick(remove, [index] {
            g_idle_add_once(+[](gpointer data) {
                const int i = GPOINTER_TO_INT(data);
                if (i >= static_cast<int>(ui.saved.timeSlots.size())) return;
                ui.saved.timeSlots.erase(ui.saved.timeSlots.begin() + i);
                if (ui.editing.kind == SlotRef::Time && ui.editing.index >= i) {
                    ui.editing = ui.editing.index == i ? InitialEditing() : SlotRef{SlotRef::Time, ui.editing.index - 1};
                }
                Commit();
                Sync();
            }, GINT_TO_POINTER(index));
        });
        adw_action_row_add_suffix(ADW_ACTION_ROW(r.row), remove);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(ui.switchGroup), r.row);
        ui.timeRows.push_back(r);
    }
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ui.switchGroup), ui.addTimeRow);
    UpdateTimeLabels();
}

// ---- 部品を作る ----

GtkWidget* Group(AdwPreferencesPage* page, const char* title, const char* description = nullptr) {
    GtkWidget* group = adw_preferences_group_new();
    if (title) adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), title);
    if (description) adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(group), description);
    adw_preferences_page_add(page, ADW_PREFERENCES_GROUP(group));
    return group;
}

GtkWidget* Combo(GtkWidget* group, const char* title, std::vector<std::string> items) {
    std::vector<const char*> list;
    for (const auto& s : items) list.push_back(s.c_str());
    list.push_back(nullptr);
    GtkWidget* row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    GtkStringList* model = gtk_string_list_new(list.data());
    adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));
    g_object_unref(model);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    return row;
}

void OnSelected(GtkWidget* combo, std::function<void(guint)> fn) {
    On<GParamSpec*>(combo, "notify::selected", [combo, fn = std::move(fn)](GParamSpec*) {
        if (!ui.syncing) fn(adw_combo_row_get_selected(ADW_COMBO_ROW(combo)));
    });
}

// つまみの行。値は min〜max（1 刻み）で、動かしている間も壁紙がついてくる
GtkWidget* Slider(GtkWidget* group, const char* title, const char* hint, double min, double max, GtkWidget** scale,
                  std::function<void(double)> fn) {
    GtkWidget* row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (hint) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), hint);
    *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, 1);
    gtk_scale_set_draw_value(GTK_SCALE(*scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(*scale), GTK_POS_LEFT);
    gtk_scale_set_digits(GTK_SCALE(*scale), 0);
    gtk_widget_set_hexpand(*scale, TRUE);
    gtk_widget_set_size_request(*scale, 220, -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(*scale), GTK_ACCESSIBLE_PROPERTY_LABEL, title, -1);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), *scale);
    GtkWidget* s = *scale;
    On<>(s, "value-changed", [s, fn = std::move(fn)] {
        if (!ui.syncing) fn(gtk_range_get_value(GTK_RANGE(s)));
    });
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    return row;
}

void ChangePlacement(const std::function<void(Placement&)>& change) {
    Slot slot = EditingSlot();
    change(slot.placement);
    SetEditingSlot(slot);
    UpdatePreview();
}

void ChooseImage() {
    GtkFileDialog* dialog = gtk_file_dialog_new();
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, T(S::Images));
    gtk_file_filter_add_mime_type(filter, "image/*");
    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filters);
    g_object_unref(filter);
    gtk_file_dialog_open(
        dialog, GTK_WINDOW(ui.window), nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer) {
            GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, nullptr);
            if (!file) return;
            gchar* path = g_file_get_path(file);
            g_object_unref(file);
            if (!path) return;
            const std::string chosen = path;
            g_free(path);
            // 読めない画像は設定に書かない
            LoadedImage image;
            if (!LoadImageFile(chosen, image)) {
                Toast(Format(S::CouldNotRead, FileName(chosen)));
                return;
            }
            Slot slot = EditingSlot();
            slot.image = chosen;
            SetEditingSlot(slot);
            gtk_range_set_value(GTK_RANGE(ui.frameScale), 1);
            Sync();
        },
        nullptr);
    g_object_unref(dialog);
}

void ExportStillImage() {
    const Slot slot = EditingSlot();
    const LoadedImage* image = PreviewImage(slot.image);
    if (!image) return;
    std::vector<ScreenSize> screens = MonitorSizes();
    if (screens.empty()) return;
    gchar* stem = g_path_get_basename(slot.image.c_str());
    std::string name = stem;
    g_free(stem);
    if (const size_t dot = name.find_last_of('.'); dot != std::string::npos && dot > 0) name.resize(dot);
    name += "-" + std::to_string(screens[0].width) + "x" + std::to_string(screens[0].height) + ".png";
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_initial_name(dialog, name.c_str());
    gtk_file_dialog_save(
        dialog, GTK_WINDOW(ui.window), nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer) {
            GFile* file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, nullptr);
            if (!file) return;
            gchar* path = g_file_get_path(file);
            g_object_unref(file);
            const Slot slot = EditingSlot();
            const LoadedImage* image = PreviewImage(slot.image);
            const size_t frame = static_cast<size_t>(gtk_range_get_value(GTK_RANGE(ui.frameScale))) - 1;
            const bool ok = path && image &&
                            ExportStill(*image, slot.image, frame, slot.placement, BackgroundFor(image),
                                        MonitorSizes(), path);
            Toast(Format(ok ? S::Exported : S::ExportFailed, FileName(path ? path : "")));
            g_free(path);
        },
        nullptr);
    g_object_unref(dialog);
}

// 常駐側を動かす。GNOME は拡張機能を有効にし、ほかは pixwall を起動する
void StartResident() {
    if (IsGnome()) {
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
        if (!ok) Toast(T(S::StartFailedGnome));
        return;
    }
    char self[4096] = {};
    const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    std::string path = n > 0 ? std::string(self, n) : std::string();
    path = path.substr(0, path.find_last_of('/') + 1) + "pixwall";
    gchar* argv[] = {const_cast<gchar*>(path.c_str()), nullptr};
    g_spawn_async(nullptr, argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr);
}

void SetRunning(bool running) {
    ui.running = running;
    adw_banner_set_revealed(ADW_BANNER(ui.banner), !running);
    gtk_widget_set_sensitive(ui.pauseRow, running);
    if (!running) return;
    if (GVariant* reply = CallResident("IsPaused")) {
        gboolean paused = FALSE;
        g_variant_get(reply, "(b)", &paused);
        g_variant_unref(reply);
        ui.syncing = true;
        adw_switch_row_set_active(ADW_SWITCH_ROW(ui.pauseRow), paused);
        ui.syncing = false;
    }
}

// pixwallctl set などで config.json が変わったら、読み直して画面を合わせる
void OnConfigChanged(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent event, gpointer) {
    if (event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT && event != G_FILE_MONITOR_EVENT_RENAMED &&
        event != G_FILE_MONITOR_EVENT_MOVED_IN && event != G_FILE_MONITOR_EVENT_CREATED) {
        return;
    }
    const Config next = LoadConfig(ui.configPath);
    if (FormatConfig(next) == ui.lastWritten || next == ui.saved) return;
    ui.saved = next;
    ui.lastWritten = FormatConfig(next);
    Sync();
}

void Build(GtkApplication* app) {
    ui.window = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(ui.window), T(S::WindowTitle));
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 640, g_getenv("PIXWALL_SNAPSHOT") ? 1900 : 860);

    GtkWidget* toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), adw_header_bar_new());
    ui.banner = adw_banner_new(T(S::NotRunning));
    adw_banner_set_button_label(ADW_BANNER(ui.banner), T(S::Start));
    On<>(ui.banner, "button-clicked", [] { StartResident(); });
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), ui.banner);
    ui.toasts = adw_toast_overlay_new();
    auto* page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(ui.toasts), GTK_WIDGET(page));
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), ui.toasts);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(ui.window), toolbar);

    // 表示を更新
    GtkWidget* top = Group(page, nullptr);
    GtkWidget* refreshRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(refreshRow), T(S::RefreshLabel));
    GtkWidget* refresh = gtk_button_new_with_label(T(S::RefreshButton));
    gtk_widget_set_valign(refresh, GTK_ALIGN_CENTER);
    OnClick(refresh, [] {
        if (GVariant* reply = CallResident("Refresh")) g_variant_unref(reply);
    });
    adw_action_row_add_suffix(ADW_ACTION_ROW(refreshRow), refresh);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(top), refreshRow);

    // 画像の切り替え
    ui.switchGroup = Group(page, T(S::SwitchLabel), T(S::SwitchHint));
    // Omarchy に付いていくのは、Omarchy があるときだけ選べる（設定がもう omarchy なら出しておく）
    ui.configPath = ConfigPath();
    ui.saved = LoadConfig(ui.configPath);
    std::vector<std::string> modes = {T(S::SwitchNone), T(S::SwitchTime), T(S::SwitchAppearance)};
    if (g_file_test(OmarchyCurrentDir().c_str(), G_FILE_TEST_IS_DIR) || OmarchyMode()) {
        modes.push_back(T(S::SwitchOmarchy));
    }
    ui.switchCombo = Combo(ui.switchGroup, T(S::SwitchBy), modes);
    OnSelected(ui.switchCombo, [](guint i) {
        ui.saved.SwitchTo(static_cast<SwitchBy>(i));
        ui.editing = InitialEditing();
        Commit();
        Sync();
    });
    ui.addTimeRow = adw_button_row_new();
    // 時刻の行の後ろへ付け直すために一度外すので、外しても消えないよう持っておく
    g_object_ref_sink(ui.addTimeRow);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ui.addTimeRow), T(S::AddTime));
    adw_button_row_set_start_icon_name(ADW_BUTTON_ROW(ui.addTimeRow), "list-add-symbolic");
    On<>(ui.addTimeRow, "activated", [] {
        // 今の時刻から、今編集しているものと同じ画像と並べ方で始める
        ui.saved.timeSlots.push_back(TimeSlot{LocalMinute(), EditingSlot()});
        ui.editing = SlotRef{SlotRef::Time, static_cast<int>(ui.saved.timeSlots.size()) - 1};
        Commit();
        Sync();
    });
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ui.switchGroup), ui.addTimeRow);
    ui.appearanceRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ui.appearanceRow), T(S::EditAppearance));
    GtkWidget* toggles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(toggles, "linked");
    gtk_widget_set_valign(toggles, GTK_ALIGN_CENTER);
    ui.lightButton = gtk_toggle_button_new_with_label(T(S::Light));
    ui.darkButton = gtk_toggle_button_new_with_label(T(S::Dark));
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(ui.darkButton), GTK_TOGGLE_BUTTON(ui.lightButton));
    gtk_box_append(GTK_BOX(toggles), ui.lightButton);
    gtk_box_append(GTK_BOX(toggles), ui.darkButton);
    adw_action_row_add_suffix(ADW_ACTION_ROW(ui.appearanceRow), toggles);
    for (auto [button, kind] : {std::pair{ui.lightButton, SlotRef::Light}, {ui.darkButton, SlotRef::Dark}}) {
        On<>(button, "toggled", [button, kind] {
            if (!ui.syncing && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) SelectEditing({kind, 0});
        });
    }
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ui.switchGroup), ui.appearanceRow);

    // 画像
    ui.imageGroup = Group(page, T(S::ImageMain));
    ui.imageRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ui.imageRow), T(S::AllDisplays));
    GtkWidget* choose = gtk_button_new_with_label(T(S::Choose));
    ui.chooseButton = choose;
    gtk_widget_set_valign(choose, GTK_ALIGN_CENTER);
    OnClick(choose, [] { ChooseImage(); });
    adw_action_row_add_suffix(ADW_ACTION_ROW(ui.imageRow), choose);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ui.imageGroup), ui.imageRow);
    ui.preview = gtk_picture_new();
    gtk_picture_set_can_shrink(GTK_PICTURE(ui.preview), FALSE);
    gtk_widget_set_halign(ui.preview, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(ui.preview, 12);
    gtk_widget_set_margin_bottom(ui.preview, 12);
    gtk_widget_add_css_class(ui.preview, "card");
    gtk_widget_set_margin_start(ui.preview, 12);
    gtk_widget_set_margin_end(ui.preview, 12);
    gtk_widget_set_overflow(ui.preview, GTK_OVERFLOW_HIDDEN);
    // 見本は一覧の中に置く（一覧の外に足すと、一覧の下に出る）
    GtkWidget* previewRow = ui.previewRow = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(previewRow), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(previewRow), FALSE);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(previewRow), ui.preview);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ui.imageGroup), previewRow);
    ui.frameRow = Slider(ui.imageGroup, "", nullptr, 1, 2, &ui.frameScale, [](double) { UpdatePreview(); });
    // 何枚目かは行の題に出す
    gtk_scale_set_draw_value(GTK_SCALE(ui.frameScale), FALSE);
    GtkWidget* exportRow = ui.exportRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(exportRow), T(S::StillLabel));
    GtkWidget* exportButton = gtk_button_new_with_label(T(S::ExportButton));
    gtk_widget_set_valign(exportButton, GTK_ALIGN_CENTER);
    OnClick(exportButton, [] { ExportStillImage(); });
    adw_action_row_add_suffix(ADW_ACTION_ROW(exportRow), exportButton);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ui.imageGroup), exportRow);

    // 表示
    GtkWidget* display = Group(page, T(S::DisplayHeading));
    ui.fitCombo = Combo(display, T(S::Fit), {T(S::FitInteger), T(S::FitFit), T(S::FitFill), T(S::FitFree)});
    OnSelected(ui.fitCombo, [](guint i) {
        ChangePlacement([i](Placement& p) { p.fit = static_cast<Fit>(i); });
        Sync();
    });
    ui.positionRow = Slider(display, T(S::Position), T(S::PositionHint), 0, 100, &ui.positionScale,
                            [](double v) { ChangePlacement([v](Placement& p) { p.position = v / 100; }); });
    ui.sizeRow = Slider(display, T(S::FreeSize), T(S::FreeSizeHint), 10, 300, &ui.sizeScale,
                        [](double v) { ChangePlacement([v](Placement& p) { p.freeSize = v / 100; }); });
    ui.xRow = Slider(display, T(S::FreeX), T(S::FreeXHint), 0, 100, &ui.xScale,
                     [](double v) { ChangePlacement([v](Placement& p) { p.freeX = v / 100; }); });
    ui.yRow = Slider(display, T(S::FreeY), T(S::FreeYHint), 0, 100, &ui.yScale,
                     [](double v) { ChangePlacement([v](Placement& p) { p.freeY = v / 100; }); });
    for (GtkWidget* row : {ui.fitCombo, ui.positionRow, ui.sizeRow, ui.xRow, ui.yRow}) {
        *std::find(std::begin(ui.placementRows), std::end(ui.placementRows), nullptr) = row;
    }
    ui.backgroundCombo = Combo(display, T(S::Background), {T(S::BackgroundAuto), T(S::BackgroundCustom)});
    adw_action_row_set_subtitle(ADW_ACTION_ROW(ui.backgroundCombo), T(S::BackgroundHint));
    GtkColorDialog* colorDialog = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(colorDialog, FALSE);
    ui.colorButton = gtk_color_dialog_button_new(colorDialog);
    gtk_widget_set_valign(ui.colorButton, GTK_ALIGN_CENTER);
    gtk_accessible_update_property(GTK_ACCESSIBLE(ui.colorButton), GTK_ACCESSIBLE_PROPERTY_LABEL, T(S::Background), -1);
    adw_action_row_add_suffix(ADW_ACTION_ROW(ui.backgroundCombo), ui.colorButton);
    OnSelected(ui.backgroundCombo, [](guint i) {
        ui.saved.customBackground = i == 1;
        // 初めて「自分で選ぶ」にしたときは、今の自動の色から始める
        if (ui.saved.customBackground && ui.saved.backgroundColor < 0) {
            ui.saved.backgroundColor = BackgroundFor(PreviewImage(EditingSlot().image));
        }
        Commit();
        Sync();
    });
    On<GParamSpec*>(ui.colorButton, "notify::rgba", [](GParamSpec*) {
        if (ui.syncing) return;
        const GdkRGBA* c = gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(ui.colorButton));
        const auto byte = [](float v) { return std::clamp(static_cast<int>(std::lround(v * 255)), 0, 255); };
        ui.saved.backgroundColor = (byte(c->red) << 16) | (byte(c->green) << 8) | byte(c->blue);
        Commit();
        UpdatePreview();
    });
    std::vector<std::string> speeds;
    for (double s : kSpeeds) speeds.push_back(Format(S::SpeedTimes, Number(s)));
    ui.speedCombo = Combo(display, T(S::Speed), speeds);
    OnSelected(ui.speedCombo, [](guint i) {
        ui.saved.speed = kSpeeds[std::min<guint>(i, G_N_ELEMENTS(kSpeeds) - 1)];
        Commit();
    });

    // 止める
    GtkWidget* pause = Group(page, T(S::PauseHeading));
    ui.lowPowerRow = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ui.lowPowerRow), T(S::LowPower));
    On<GParamSpec*>(ui.lowPowerRow, "notify::active", [](GParamSpec*) {
        if (ui.syncing) return;
        ui.saved.pauseOnLowPower = adw_switch_row_get_active(ADW_SWITCH_ROW(ui.lowPowerRow));
        Commit();
    });
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(pause), ui.lowPowerRow);
    ui.pauseRow = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ui.pauseRow), T(S::Pause));
    On<GParamSpec*>(ui.pauseRow, "notify::active", [](GParamSpec*) {
        if (ui.syncing) return;
        const gboolean paused = adw_switch_row_get_active(ADW_SWITCH_ROW(ui.pauseRow));
        if (GVariant* reply = CallResident("SetPaused", g_variant_new("(b)", paused))) g_variant_unref(reply);
    });
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(pause), ui.pauseRow);

    // 起動（GNOME では拡張機能がログインのたびに動くので出さない）
    GtkWidget* startup = Group(page, T(S::StartupHeading));
    gtk_widget_set_visible(startup, !IsGnome());
    ui.loginRow = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ui.loginRow), T(S::Login));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(ui.loginRow), T(S::LoginHint));
    On<GParamSpec*>(ui.loginRow, "notify::active", [](GParamSpec*) {
        if (ui.syncing) return;
        ui.saved.launchAtLogin = adw_switch_row_get_active(ADW_SWITCH_ROW(ui.loginRow));
        Commit();
    });
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(startup), ui.loginRow);

    // 終了
    GtkWidget* quit = Group(page, nullptr);
    // 終えたときに GNOME の壁紙を戻さない（GNOME だけ。ほかには入れ替える OS の壁紙が無い）
    ui.keepRow = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ui.keepRow), T(S::Keep));
    gtk_widget_set_visible(ui.keepRow, IsGnome());
    On<GParamSpec*>(ui.keepRow, "notify::active", [](GParamSpec*) {
        if (ui.syncing) return;
        ui.saved.keepWallpaperOnQuit = adw_switch_row_get_active(ADW_SWITCH_ROW(ui.keepRow));
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ui.quitRow),
                                      T(ui.saved.keepWallpaperOnQuit ? S::QuitKeeps : S::QuitLabel));
        Commit();
    });
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(quit), ui.keepRow);
    GtkWidget* quitRow = ui.quitRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(quitRow), T(S::QuitLabel));
    GtkWidget* quitButton = gtk_button_new_with_label(T(S::QuitButton));
    gtk_widget_add_css_class(quitButton, "destructive-action");
    gtk_widget_set_valign(quitButton, GTK_ALIGN_CENTER);
    OnClick(quitButton, [] {
        if (GVariant* reply = CallResident("Quit")) g_variant_unref(reply);
        gtk_window_close(GTK_WINDOW(ui.window));
    });
    adw_action_row_add_suffix(ADW_ACTION_ROW(quitRow), quitButton);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(quit), quitRow);

    ui.configPath = ConfigPath();
    ui.saved = LoadConfig(ui.configPath);
    ui.lastWritten = FormatConfig(ui.saved);
    ui.editing = InitialEditing();
    GFile* file = g_file_new_for_path(ui.configPath.c_str());
    ui.monitor = g_file_monitor_file(file, G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr);
    g_object_unref(file);
    if (ui.monitor) g_signal_connect(ui.monitor, "changed", G_CALLBACK(OnConfigChanged), nullptr);
    // Omarchy で壁紙を選び直したら、編集する画像も替える
    if (g_file_test(OmarchyCurrentDir().c_str(), G_FILE_TEST_IS_DIR)) {
        GFile* current = g_file_new_for_path(OmarchyCurrentDir().c_str());
        ui.omarchyMonitor = g_file_monitor_directory(current, G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr);
        g_object_unref(current);
        if (ui.omarchyMonitor) {
            g_signal_connect(ui.omarchyMonitor, "changed",
                             G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer) {
                                 if (OmarchyMode()) Sync();
                             }),
                             nullptr);
        }
    }
    g_bus_watch_name(G_BUS_TYPE_SESSION, kBusName, G_BUS_NAME_WATCHER_FLAGS_NONE,
                     +[](GDBusConnection*, const gchar*, const gchar*, gpointer) { SetRunning(true); },
                     +[](GDBusConnection*, const gchar*, gpointer) { SetRunning(false); }, nullptr, nullptr);
    Sync();
    // 開いている間に時刻が進んでも「今」の印が合うよう、30 秒ごとに付け直す（設定画面を開いている間だけ）
    g_timeout_add_seconds(30, +[](gpointer) -> gboolean {
        if (!ui.timeRows.empty()) UpdateTimeLabels();
        return G_SOURCE_CONTINUE;
    }, nullptr);
    gtk_window_present(GTK_WINDOW(ui.window));
    // 確かめる用: PIXWALL_SNAPSHOT=<png> なら、開いて少ししてから窓の絵を書いて閉じる（docs/linux-trial.md）
    if (g_getenv("PIXWALL_SNAPSHOT")) {
        g_timeout_add_once(1500, +[](gpointer) {
            // 窓に収まらない下のほうも撮れるよう、スクロールの中身をまるごと描く
            GtkWidget* target = ui.window;
            std::function<GtkWidget*(GtkWidget*)> find = [&](GtkWidget* w) -> GtkWidget* {
                if (GTK_IS_VIEWPORT(w)) return gtk_viewport_get_child(GTK_VIEWPORT(w));
                for (GtkWidget* c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c)) {
                    if (GtkWidget* found = find(c)) return found;
                }
                return nullptr;
            };
            if (g_getenv("PIXWALL_SNAPSHOT_CONTENT")) {
                if (GtkWidget* content = find(ui.window)) target = content;
            }
            const int w = gtk_widget_get_width(target);
            const int h = gtk_widget_get_height(target);
            GdkPaintable* paintable = gtk_widget_paintable_new(target);
            GtkSnapshot* snapshot = gtk_snapshot_new();
            gdk_paintable_snapshot(paintable, snapshot, w, h);
            GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
            GskRenderer* renderer = gtk_native_get_renderer(GTK_NATIVE(ui.window));
            if (node && renderer) {
                GdkTexture* texture = gsk_renderer_render_texture(renderer, node, nullptr);
                gdk_texture_save_to_png(texture, g_getenv("PIXWALL_SNAPSHOT"));
                g_object_unref(texture);
            }
            if (node) gsk_render_node_unref(node);
            g_object_unref(paintable);
            gtk_window_close(GTK_WINDOW(ui.window));
        }, nullptr);
    }
}

}  // namespace

int main(int argc, char** argv) {
    // 設定画面は1つだけ開く（もう開いていれば、それを前に出す）
    AdwApplication* app = adw_application_new("xyz.mooma0750.pixwall.Settings", G_APPLICATION_DEFAULT_FLAGS);
    ui.app = app;
    g_signal_connect(app, "activate", G_CALLBACK(+[](GtkApplication* a, gpointer) {
                         if (ui.window) {
                             gtk_window_present(GTK_WINDOW(ui.window));
                             return;
                         }
                         Build(a);
                     }),
                     nullptr);
    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
