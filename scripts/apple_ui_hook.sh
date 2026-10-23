#!/usr/bin/env bash
# CC の PostToolUse フック。書いた .swift / Info.plist を検査し、外れていれば CC へ返して直させる。
f=$(jq -r '.tool_input.file_path // empty')
[ -n "$f" ] && [ -f "$f" ] || exit 0
case "$f" in *.swift|*.plist) ;; *) exit 0 ;; esac
check="$(cd "$(dirname "$0")" && pwd)/check_apple_ui.sh"
[ -x "$check" ] || exit 0
if ! out=$("$check" "$f"); then
  printf 'Apple の今の書き方（Liquid Glass・新しい SwiftUI）から外れています（置き換え表は apple-native-ui スキル）:\n%s\n' "$out" >&2
  exit 2
fi
exit 0
