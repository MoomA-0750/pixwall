// Hyprland に、どの画面がほかの窓で隠れているかを訊く。Hyprland は全画面の窓の後ろの背景にも
// 描き終えた知らせ（frame callback）を返すので、それだけでは止められないため
#pragma once

#include <functional>
#include <set>
#include <string>

namespace pixwall {

// hyprctl -j monitors と -j clients の JSON から、隠れている画面の名前（eDP-1 など）を決める。
// 隠れている: 画面が消えている（dpms）、今の作業スペースに全画面の窓がある、
// または作業領域（バーを除いた所）のうち窓に覆われていない部分が 5% 以下（core の IsCovered）
std::set<std::string> CoveredMonitors(const std::string& monitorsJson, const std::string& clientsJson);

// Hyprland の知らせ（.socket2.sock）を受け、窓・作業スペースが変わったら少し待ってから訊き直す。
// Hyprland でなければ（HYPRLAND_INSTANCE_SIGNATURE が無ければ）何もしない
class HyprlandWatcher {
public:
    using Callback = std::function<void(const std::set<std::string>&)>;
    bool Start(Callback callback);
    ~HyprlandWatcher();

    // 今の隠れている画面を訊く（.socket.sock に頼む）
    std::set<std::string> Query() const;

private:
    static int OnEvent(int fd, unsigned condition, void* data);
    static int OnSettled(void* data);

    std::string dir_;
    int fd_ = -1;
    unsigned source_ = 0;
    unsigned settle_ = 0;
    std::string buffer_;
    Callback callback_;
};

}  // namespace pixwall
