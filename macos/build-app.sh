#!/bin/sh
# pixwall.app を build/ に作る。Xcode のプロジェクトは持たず、SwiftPM でビルドして包む
set -eu
cd "$(dirname "$0")"

swift build -c release
app=build/pixwall.app
rm -rf "$app"
mkdir -p "$app/Contents/MacOS"
cp "$(swift build -c release --show-bin-path)/pixwall" "$app/Contents/MacOS/pixwall"
cp Resources/Info.plist "$app/Contents/Info.plist"
# 画面の文字の訳。キーは日本語の文で、en.lproj に英語を置く（ja.lproj はキーのまま出すための空の表）
mkdir -p "$app/Contents/Resources"
cp -R Resources/en.lproj Resources/ja.lproj "$app/Contents/Resources/"
codesign --force --sign - "$app"
echo "$app"
