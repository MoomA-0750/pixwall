// 設定（config.json）。Mac 版の Config と同じ形で読み書きする。
// 足りない項目・読めない値は既定の値にする（壊れていても起動できる）
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core.h"

namespace pixwall {

// 出す画像を何で切り替えるか。設定の JSON にもこの名前で書く（none / time / appearance）
enum class SwitchBy { None, Time, Appearance };

// 1つの画像と、その並べ方・位置・大きさ。JSON では Config の上の階層と同じ名前の項目に書く
struct Slot {
    std::string image;  // 空なら選んでいない
    Placement placement;
    bool operator==(const Slot&) const = default;
};

constexpr int kMinutesPerDay = 24 * 60;

// 決めた時刻から出す画像。JSON では Slot の項目に start（"HH:MM"）を足して書く
struct TimeSlot {
    int start = 0;  // 始まる時刻。0 時からの分（0〜1439）
    Slot slot;
    bool operator==(const TimeSlot&) const = default;
};

// 設定画面で編集するもの（どの画像と並べ方か）
struct SlotRef {
    enum Kind { Main, Time, Light, Dark } kind = Main;
    int index = 0;  // Time のときの timeSlots の番号
    bool operator==(const SlotRef&) const = default;
};

struct Config {
    int version = 1;
    std::string image;  // 動かす画像のパス（UTF-8）。空なら選んでいない
    Fit fit = Fit::Integer;
    double position = 0.5;  // 画面に合わせる・埋めるときの位置（0 で上・左、1 で下・右）
    // 自由に置くときの横・縦の位置（0〜1）と大きさ（画面に合わせたときを 1 とする。0.1〜3）。Placement を見る
    double freeX = 0.5;
    double freeY = 0.5;
    double freeSize = 1;
    SwitchBy switchBy = SwitchBy::None;
    std::vector<TimeSlot> timeSlots;  // 時刻で切り替えるときの、時刻ごとの画像と並べ方（書いた順）
    // ダーク・ライトで切り替えるときの画像と並べ方。無ければ image と並べ方を使う
    std::optional<Slot> lightSlot;
    std::optional<Slot> darkSlot;
    // 余白の色。false なら画像の1枚目の左上の1ドットの色、true なら backgroundColor（選んでいれば）
    bool customBackground = false;
    int backgroundColor = -1;  // 自分で選んだ余白の色（0xRRGGBB）。-1 ならまだ選んでいない。自動にしても覚えておく
    double speed = 1;  // 速さの倍率（0.25〜4）
    bool pauseOnLowPower = true;
    bool launchAtLogin = true;
    bool operator==(const Config&) const = default;

    Placement placement() const {
        Placement p(fit, position);
        p.freeX = freeX;
        p.freeY = freeY;
        p.freeSize = freeSize;
        return p;
    }

    // 切り替えないときに出す画像と並べ方（上の階層の image・fit・position・freeX・freeY・freeSize）
    Slot mainSlot() const { return Slot{image, placement()}; }
    void setMainSlot(const Slot& slot);

    // 時刻で切り替えるときに、今（0 時からの分）出す timeSlots の番号。無ければ -1。
    // 始まる時刻が今以前のうち一番遅いもの。無ければ前の日から続く、一番遅く始まるもの。同じ時刻なら後に書いたもの
    int ActiveTimeIndex(int minute) const;
    // 今（0 時からの分・ダークモードか）出す画像と並べ方。切り替える先が無ければ mainSlot
    Slot ActiveSlot(int minute, bool dark) const;
    // 今出すものを mainSlot に入れた設定。常駐側はこれで描く
    Config Resolved(int minute, bool dark) const;
    // 時刻で切り替えるとき、今から次に切り替わる時刻までの分（1〜1440）。切り替えないなら -1
    int MinutesToNextSwitch(int minute) const;
    // 切り替え方を変える。初めて切り替えるときは、今の画像と並べ方から始める（時刻は 6:00 と 18:00、ダーク・ライトは両方）
    void SwitchTo(SwitchBy mode);
    // 無いもの（番号が範囲の外・まだ選んでいないダーク・ライト）は mainSlot
    Slot GetSlot(SlotRef ref) const;
    // 番号が範囲の外なら何もしない
    void SetSlot(SlotRef ref, const Slot& slot);
    // 設定に出てくる画像のパス（重なりなし、出てくる順）
    std::vector<std::string> AllImages() const;
};

// "H:MM" か "HH:MM"（0:00〜23:59）を 0 時からの分にする。形が違えば -1
int ParseClock(std::string_view s);
// 0 時からの分を "HH:MM" にする
std::string FormatClock(int minutes);

Config ParseConfig(std::string_view json);
// "#RRGGBB" を 0xRRGGBB にする。形が違えば -1
int ParseHexColor(std::string_view s);
// Mac 版と同じく、キーを名前順に並べ、字下げして書く
std::string FormatConfig(const Config& config);

}  // namespace pixwall
