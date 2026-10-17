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
codesign --force --sign - "$app"
echo "$app"
