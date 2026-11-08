# pixwall

ドット絵の GIF などの動く画像を、Windows・Mac・Linux（GNOME・Hyprland）の壁紙、Android のライブ壁紙として動かすアプリ。

- **無用にリソースを使わない**: 絵が変わる瞬間だけ描く。ほかの窓に隠れている・画面が消えている・ロック中・省電力中は止める
- **常駐中にタスクバーや Dock にアイコンを出さない**
- **画面はそれぞれの OS のネイティブ UI で描く**（Windows は WinUI 3、Mac は SwiftUI / AppKit、Android は Jetpack Compose、Linux は GTK4・libadwaita と、Omarchy などに向けた QML 版）

ステータス: 試作中。Mac・Windows・Android・Linux で動き、それぞれに設定画面がある。今どう動くかは `SPEC.md`。

## 構成

| 場所 | 中身 |
|---|---|
| `windows/` | Windows 版（常駐: C++ / Win32 / DirectComposition、設定画面: C# / WinUI 3） |
| `macos/` | Mac 版（Swift、常駐と設定画面を1つのアプリで） |
| `android/` | Android 版（Kotlin。ライブ壁紙は OpenGL ES、設定画面は Jetpack Compose） |
| `linux/` | Linux 版（常駐: C++ / Wayland の layer-shell、GNOME は Shell の拡張機能。設定画面: GTK4・libadwaita の版と QML（Qt Quick）の版、選び役 `pixwall-settings` がどちらかを開く） |
| `samples/` | 試す・測るための画像 |
| `SPEC.md` | 今どう動くべきか（正本） |
| `docs/DESIGN.md` | 仕組みと、OS ごとの作り方 |
| `docs/decisions.md` | なぜそうしたか、試してやめたこと |

## 作る・試す場所

- Windows 版: to-fu（Windows 11）でビルドして試す
- Mac 版: MacBook Air M1（Xcode）でビルドして試す
- Android 版: Linux の PC でビルドし、Pixel 3（Android 12）で試す
- Linux 版: GNOME はこの Linux の PC（`linux/install.sh`）、Hyprland は cramgent（Omarchy）で試す。QML 版の設定画面は `linux/install.sh --qt` で足す（`docs/linux-trial.md`）
