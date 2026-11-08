// GNOME の壁紙（org.gnome.desktop.background）に、今の画像の1枚目を描いた PNG を入れる・元に戻す。
// Mac・Windows と同じく、概観・ロック画面・壁紙をぼかすパネル（blur-my-shell の静的なぼかし）でも同じ絵になるように。
// 入れる前の壁紙は dir/original.ini に覚えておき、戻すときは pixwall の PNG を指しているところだけを戻す
// （動かしている間に本人が替えた壁紙は残す。落ちて戻せなかったときは、次に入れるときも前の覚え書きを使う）
#pragma once

#include <functional>
#include <string>

namespace pixwall {

// ~/.local/share/pixwall/desktop（PNG と覚え書きを置く）
std::string GnomeWallpaperDir();

// key（画像・並べ方・色・大きさをまとめた文字列）の絵を GNOME の壁紙にする。前と同じ key で、まだ pixwall の
// PNG を指していれば何もしない。render は渡したパスに PNG を書く。GNOME の設定が無ければ false
bool ApplyGnomeWallpaper(const std::string& dir, const std::string& key,
                         const std::function<bool(const std::string& png)>& render);

// 入れる前の壁紙に戻し、PNG と覚え書きを消す。覚え書きが無ければ何もしない
bool RestoreGnomeWallpaper(const std::string& dir);

}  // namespace pixwall
