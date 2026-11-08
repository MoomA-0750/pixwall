// QML 版の設定画面の中身。GTK 版（settings/gtk/main.cpp）と同じことを、QML から呼べる形にする。
// 変えるとすぐ config.json に書き、常駐側に D-Bus で読み直してもらう。pixwallctl set などで config.json が
// 変わったら読み直して画面を合わせる
#pragma once

#include <gio/gio.h>

#include <QAbstractListModel>
#include <QColor>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <string>

#include "config.h"
#include "image_loader.h"

namespace pixwall {

// 見本の絵（image://preview/<番号>）。番号を替えると読み直す
class PreviewProvider : public QQuickImageProvider {
public:
    PreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;
    void setImage(const QImage& image);

private:
    QMutex m_mutex;
    QImage m_image;
};

// 時刻の一覧（時刻で切り替えるとき）
class TimeModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { TitleRole = Qt::UserRole + 1, FileRole, HourRole, MinuteRole, NowRole, EditingRole };
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    struct Row {
        QString title, file;
        int hour = 0, minute = 0;
        bool now = false, editing = false;
    };
    // 行の数が変われば作り直し、同じなら中身だけ知らせる（時と分の欄を触っている最中に消さない）
    void set(const QList<Row>& rows);

private:
    QList<Row> m_rows;
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap s READ strings CONSTANT)  // 言葉の表（strings.h の名前で引く）
    Q_PROPERTY(bool gnome READ gnome CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY runningChanged)
    Q_PROPERTY(QStringList switchModes READ switchModes CONSTANT)
    Q_PROPERTY(QStringList fitLabels READ fitLabels CONSTANT)
    Q_PROPERTY(QStringList speedLabels READ speedLabels CONSTANT)
    Q_PROPERTY(QObject* times READ times CONSTANT)
    Q_PROPERTY(int switchBy READ switchBy NOTIFY changed)
    Q_PROPERTY(bool canRemoveTime READ canRemoveTime NOTIFY changed)
    Q_PROPERTY(bool editingLight READ editingLight NOTIFY changed)
    Q_PROPERTY(bool editingDark READ editingDark NOTIFY changed)
    Q_PROPERTY(QString imageHeading READ imageHeading NOTIFY changed)
    Q_PROPERTY(QString imageSubtitle READ imageSubtitle NOTIFY changed)
    Q_PROPERTY(bool canChoose READ canChoose NOTIFY changed)
    Q_PROPERTY(bool paired READ paired NOTIFY changed)
    Q_PROPERTY(QUrl previewSource READ previewSource NOTIFY previewChanged)
    Q_PROPERTY(int previewWidth READ previewWidth NOTIFY previewChanged)
    Q_PROPERTY(int previewHeight READ previewHeight NOTIFY previewChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY previewChanged)
    Q_PROPERTY(int frame READ frame NOTIFY previewChanged)
    Q_PROPERTY(QString frameTitle READ frameTitle NOTIFY previewChanged)
    Q_PROPERTY(int fit READ fit NOTIFY changed)
    Q_PROPERTY(int position READ position NOTIFY changed)
    Q_PROPERTY(int freeSize READ freeSize NOTIFY changed)
    Q_PROPERTY(int freeX READ freeX NOTIFY changed)
    Q_PROPERTY(int freeY READ freeY NOTIFY changed)
    Q_PROPERTY(bool customBackground READ customBackground NOTIFY changed)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY changed)
    Q_PROPERTY(int speedIndex READ speedIndex NOTIFY changed)
    Q_PROPERTY(bool pauseOnLowPower READ pauseOnLowPower NOTIFY changed)
    Q_PROPERTY(bool launchAtLogin READ launchAtLogin NOTIFY changed)
    Q_PROPERTY(bool keepWallpaperOnQuit READ keepWallpaperOnQuit NOTIFY changed)
    Q_PROPERTY(QString quitLabel READ quitLabel NOTIFY changed)

public:
    explicit Backend(PreviewProvider* preview, QObject* parent = nullptr);
    ~Backend() override;

    QVariantMap strings() const;
    bool gnome() const;
    bool running() const { return m_running; }
    bool paused() const { return m_paused; }
    QStringList switchModes() const;
    QStringList fitLabels() const;
    QStringList speedLabels() const;
    QObject* times() { return &m_times; }
    int switchBy() const { return static_cast<int>(m_saved.switchBy); }
    bool canRemoveTime() const { return m_saved.timeSlots.size() > 1; }
    bool editingLight() const { return m_editing.kind == SlotRef::Light; }
    bool editingDark() const { return m_editing.kind == SlotRef::Dark; }
    QString imageHeading() const;
    QString imageSubtitle() const;
    bool canChoose() const { return !omarchyMode(); }
    bool paired() const;
    QUrl previewSource() const;
    int previewWidth() const { return m_previewW; }
    int previewHeight() const { return m_previewH; }
    int frameCount() const;
    int frame() const { return m_frame; }
    QString frameTitle() const;
    int fit() const;
    int position() const;
    int freeSize() const;
    int freeX() const;
    int freeY() const;
    bool customBackground() const { return m_saved.customBackground; }
    QColor backgroundColor() const;
    int speedIndex() const;
    bool pauseOnLowPower() const { return m_saved.pauseOnLowPower; }
    bool launchAtLogin() const { return m_saved.launchAtLogin; }
    bool keepWallpaperOnQuit() const { return m_saved.keepWallpaperOnQuit; }
    QString quitLabel() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void start();
    Q_INVOKABLE void setSwitchBy(int mode);
    Q_INVOKABLE void selectTime(int index);
    Q_INVOKABLE void setTimeStart(int index, int hour, int minute);
    Q_INVOKABLE void removeTime(int index);
    Q_INVOKABLE void addTime();
    Q_INVOKABLE void selectAppearance(bool dark);
    Q_INVOKABLE void chooseImage(const QUrl& file);
    Q_INVOKABLE void setFrame(int frame);
    Q_INVOKABLE QString exportName() const;
    Q_INVOKABLE void exportStill(const QUrl& file);
    Q_INVOKABLE void setFit(int fit);
    Q_INVOKABLE void setPosition(int value);
    Q_INVOKABLE void setFreeSize(int value);
    Q_INVOKABLE void setFreeX(int value);
    Q_INVOKABLE void setFreeY(int value);
    Q_INVOKABLE void setCustomBackground(bool custom);
    Q_INVOKABLE void setBackgroundColor(const QColor& color);
    Q_INVOKABLE void setSpeed(int index);
    Q_INVOKABLE void setPauseOnLowPower(bool on);
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE void setLaunchAtLogin(bool on);
    Q_INVOKABLE void setKeepWallpaperOnQuit(bool on);
    Q_INVOKABLE void quit();

    // 30 秒ごとに「今」の印を付け直す。GIO の知らせから呼ぶので公開する
    void updateTimes();
    void onConfigFileChanged();
    void onOmarchyChanged();
    void setRunning(bool running);

Q_SIGNALS:
    void changed();
    void previewChanged();
    void runningChanged();
    void toast(const QString& text);
    void quitRequested();

private:
    bool omarchyMode() const { return m_saved.switchBy == SwitchBy::Omarchy; }
    Slot editingSlot() const;
    void setEditingSlot(const Slot& slot);
    SlotRef initialEditing() const;
    void commit();
    void sync();  // 設定に合わせて、編集するものを直し、画面に知らせる
    void changePlacement(const std::function<void(Placement&)>& change);
    const LoadedImage* previewImage(const std::string& path) const;
    int backgroundFor(const LoadedImage* image) const;
    void updatePreview();

    PreviewProvider* m_preview;
    TimeModel m_times;
    std::string m_configPath;
    Config m_saved;
    std::string m_lastWritten;  // 最後にこちらで書いた中身（自分で書いたのを読み直さない）
    SlotRef m_editing;
    bool m_running = false;
    bool m_paused = false;
    int m_frame = 1;
    int m_previewW = 1, m_previewH = 1, m_previewSerial = 0;

    mutable std::string m_previewPath;
    mutable std::unique_ptr<LoadedImage> m_previewImage;
    GFileMonitor* m_monitor = nullptr;
    GFileMonitor* m_omarchyMonitor = nullptr;
    guint m_busWatch = 0;
    guint m_timer = 0;
};

}  // namespace pixwall
