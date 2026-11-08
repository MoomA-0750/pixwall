// Omarchy の壁紙に付いていく（switchBy が omarchy のとき）。
// Omarchy は選んだ壁紙を ~/.local/state/omarchy/current/background というリンクで指す（壁紙のセレクター
// omarchy-theme-bg-set も、テーマを替えたときも、ln -nsf でこのリンクを付け替える）。
// pixwall は、そのリンクが指す静止画と同じフォルダーの下の pixwall/ にある同じ名前の GIF（か WebP）を出す。
// リンクが GIF そのものを指していれば、それを出す。組が無ければ何も出さず、Omarchy の静止画に任せる
#pragma once

#include <string>

#include "config.h"

namespace pixwall {

// ~/.local/state/omarchy/current（XDG_STATE_HOME を見る）。Omarchy が無くてもパスは返す
std::string OmarchyCurrentDir();

// background のリンク（か、ファイル）linkPath が指す壁紙に組む画像のパス。無ければ空
std::string OmarchyImage(const std::string& linkPath);

// 今出すものを mainSlot に入れた設定。Omarchy で切り替えるときは、今の Omarchy の壁紙に組む画像と、
// その画像の並べ方（omarchySlots）。ほかは Config::Resolved と同じ
Config ResolveNow(const Config& saved, int minute, bool dark);

}  // namespace pixwall
