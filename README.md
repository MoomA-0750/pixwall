# pixwall

ドット絵の GIF などの動く画像を、Windows と Mac の壁紙、Android のライブ壁紙として動かすアプリ。

- **無用にリソースを使わない**: 絵が変わる瞬間だけ描く。ほかの窓に隠れている・画面が消えている・ロック中・省電力中は止める
- **常駐中にタスクバーや Dock にアイコンを出さない**
- **画面はそれぞれの OS のネイティブ UI で描く**（Windows は WinUI 3、Mac は SwiftUI / AppKit、Android は Jetpack Compose）

ステータス: Mac 版の最小の形が動く（2026-10-17。1つの GIF をアイコンの後ろで動かし、Dock に出さない）。Windows 版は最小の形のコードまで（to-fu で未確認）。設定画面はまだない。

## 構成

| 場所 | 中身 |
|---|---|
| `windows/` | Windows 版（常駐: C++ / Win32 / DirectComposition、設定画面: C# / WinUI 3） |
| `macos/` | Mac 版（Swift、常駐と設定画面を1つのアプリで） |
| `android/` | Android 版（Kotlin。ライブ壁紙は OpenGL ES、設定画面は Jetpack Compose） |
| `samples/` | 試す・測るための画像 |
| `SPEC.md` | 今どう動くべきか（正本） |
| `docs/DESIGN.md` | 仕組みと、OS ごとの作り方 |
| `docs/decisions.md` | なぜそうしたか、試してやめたこと |

## 作る・試す場所

- Windows 版: to-fu（Windows 11）でビルドして試す
- Mac 版: MacBook Air M1（Xcode）でビルドして試す
- Android 版: Linux の PC でビルドし、Pixel 3（Android 12）で試す
