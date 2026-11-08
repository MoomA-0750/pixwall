# pixwall のエージェント向けメモ

- 返答は日本語で行う

## 仕様の管理

- このリポジトリは試作しながら仕様を詰める。仕様の正本は`SPEC.md`
- 動きを変えたら、同じcommitで`SPEC.md`を直す。履歴は書かず、今の状態に書き換える
- 判断の理由、試してやめたことは`docs/decisions.md`に日付付きで追記する
- `SPEC.md`の各項目には「確定」か「仮」を付ける。「確定」の項目には、外から見た動きを確かめる自動テストを付ける。自動テストが難しいものは、確かめる手順を書けば確定にしてよい
- 迷っていることは`SPEC.md`末尾の「未決事項」に書き、本人に確認する

## 作り方の決まり

- 画面はそれぞれの OS のネイティブ UI で描く（Windows は WinUI 3、Mac は SwiftUI / AppKit、Android は Jetpack Compose の Material 3 Expressive、Linux は GTK4・libadwaita と、Omarchy などに馴染ませる QML 版（Qt Quick の Material）。QML 版は C++ と Qt Quick で、GTK 版と別の実行ファイルにし、中身を共有する）。Web の画面や独自の描画の UI にしない
- Android の画面は android-m3-expressive スキルに従う。書くたびに `scripts/check_m3e.sh` がフックで走る
- Windows の常駐側（`windows/` の C++）に WinUI 3・.NET・WebView を入れない
- Windows 版は to-fu、Mac 版は MacBook Air M1 でビルドして試す。Android 版はこの Linux の PC でビルドし、USB でつないだ Pixel 3（Android 12）で試す（`docs/android-trial.md`）。Linux 版もこの PC でビルドする（QML 版の設定画面は Qt の開発用ファイルのある cramgent で組む）。GNOME の拡張機能は本物のデスクトップでは試さず、`linux/tests/gnome-headless/start.sh` の画面の無いシェルで試す（`docs/linux-trial.md`）
- Mac の画面（SwiftUI / AppKit）は CC の `apple-native-ui` スキルの書き方に従う（Liquid Glass と今の SwiftUI）。`.claude/settings.json` のフックが、書いた `.swift` を `scripts/check_apple_ui.sh` で検査して外れを差し戻す。正しい理由で使う行は同じ行に `// apple-ui-allow: 理由`
