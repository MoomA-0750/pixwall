// QML 版の設定画面の色と角の丸み。Omarchy のテーマ（~/.local/state/omarchy/current/theme/colors.toml）を読み、
// QFileSystemWatcher で見張って、テーマを切り替えたらその場で合わせる。色の対応は omarchy-gtk-tint と同じにして、
// GTK のアプリと見た目をそろえる。Omarchy が無いときは Qt が知っている明暗に合わせ、Material の既定の色を使う
#pragma once

#include <QColor>

#include <algorithm>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace pixwall {

// colors.toml の「key = "値"」の並びを読む（表や配列は使っていない。後ろの # からはコメント）
QHash<QString, QString> ParseColorsToml(const QString& text);

// 色の上に載せる文字の色。a と b のうち、bg との明るさの比が大きい方（omarchy-gtk-tint の on() と同じ）
QColor OnColor(const QColor& bg, const QColor& a, const QColor& b);

// hyprctl getoption decoration:rounding -j の出力から丸み（px）を読む。読めなければ -1
int ParseRounding(const QByteArray& json);

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool omarchy READ omarchy NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor onAccent READ onAccent NOTIFY changed)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor view READ view NOTIFY changed)
    Q_PROPERTY(QColor raised READ raised NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor red READ red NOTIFY changed)
    Q_PROPERTY(QColor onRed READ onRed NOTIFY changed)
    Q_PROPERTY(QColor green READ green NOTIFY changed)
    Q_PROPERTY(QColor yellow READ yellow NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(int smallRadius READ smallRadius NOTIFY changed)

public:
    // stateHome は XDG_STATE_HOME（空なら環境から。無ければ ~/.local/state）
    explicit Theme(const QString& stateHome = {}, QObject* parent = nullptr);

    // 丸みを訊くコマンド（既定は hyprctl getoption decoration:rounding -j）。テスト用に替えられる。空なら訊かない
    void setRoundingCommand(const QStringList& command);

    QString colorsPath() const { return m_colorsPath; }
    bool omarchy() const { return m_omarchy; }
    bool dark() const { return m_dark; }
    QColor accent() const { return m_accent; }
    QColor onAccent() const { return m_onAccent; }
    QColor background() const { return m_background; }  // 窓の地
    QColor view() const { return m_view; }               // 一覧・入力欄・横の欄
    QColor raised() const { return m_raised; }           // 見出しの帯・カード・ダイアログ・ポップオーバー
    QColor foreground() const { return m_foreground; }   // 文字
    QColor border() const { return m_border; }           // 境目の線
    QColor red() const { return m_red; }                 // 削除・エラー
    QColor onRed() const { return m_onRed; }
    QColor green() const { return m_green; }             // 成功
    QColor yellow() const { return m_yellow; }           // 警告
    // パネル・ダイアログ・ボタンの丸み。Hyprland に訊けなければ -1（Material の既定の形にする）
    int radius() const { return m_radius; }
    // 小さい部品（入力欄・チェックなど）の丸み。min(4, 丸み)
    int smallRadius() const { return m_radius < 0 ? 4 : std::min(4, m_radius); }

    // 今すぐ読み直す（見張りの知らせは 0.1 秒まとめてからこれを呼ぶ）
    void reload();

Q_SIGNALS:
    void changed();

private:
    void watch();
    void applyDefaults();
    void queryRounding();

    QString m_colorsPath;
    QStringList m_watchPaths;  // colors.toml と、それを入れているフォルダー（current/theme・current）
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QStringList m_roundingCommand;
    QProcess* m_rounding = nullptr;

    bool m_omarchy = false;
    bool m_dark = false;
    QColor m_accent, m_onAccent, m_background, m_view, m_raised, m_foreground, m_border, m_red, m_onRed, m_green,
        m_yellow;
    int m_radius = -1;
};

}  // namespace pixwall
