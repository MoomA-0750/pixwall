#!/usr/bin/env bash
# SwiftUI・UIKit・AppKit のコードから「Liquid Glass 以前」「古い SwiftUI」の書き方を探す。見つかれば 1 で終わる。
# 使い方: check_apple_ui.sh [検査するディレクトリかファイル]   例外は同じ行に「// apple-ui-allow: 理由」
# 色・形・文字の正本のファイル（…Theme / Colors / Tokens / Styles .swift）は直書きを許す。
# 見ない場所はリポジトリ直下の .apple-ui-ignore に1行1つの正規表現で書く。
set -u
root="${1:-.}"
fail=0

ignore_re='/(build|\.build|DerivedData|Pods|Carthage|third_party|vendor|SourcePackages)/'
ign_file="$(git -C "$( [ -d "$root" ] && echo "$root" || dirname "$root")" rev-parse --show-toplevel 2>/dev/null)/.apple-ui-ignore"
if [ -f "$ign_file" ]; then
  extra=$(grep -vE '^[[:space:]]*(#|$)' "$ign_file" | paste -sd'|')
  [ -n "$extra" ] && ignore_re="$ignore_re|$extra"
fi

if [ -f "$root" ]; then
  case "$root" in *.swift|*.plist) files=("$root") ;; *) echo 'apple-ui: 対象外のファイル'; exit 0 ;; esac
else
  mapfile -t files < <(grep -rlE --include='*.swift' '^[[:space:]]*(@preconcurrency )?import (SwiftUI|UIKit|AppKit)' "$root" 2>/dev/null | grep -vE "$ignore_re")
fi
swift=(); plists=()
for f in "${files[@]}"; do case "$f" in *.plist) plists+=("$f") ;; *) swift+=("$f") ;; esac; done
[ -d "$root" ] && mapfile -t plists < <(grep -rlE --include='*.plist' 'UIDesignRequiresCompatibility' "$root" 2>/dev/null | grep -vE "$ignore_re")
if [ "${#swift[@]}" -eq 0 ] && [ "${#plists[@]}" -eq 0 ]; then echo 'apple-ui: SwiftUI・UIKit・AppKit のファイルなし'; exit 0; fi

report() { # $1=説明 $2=grep -E の式 $3=値の正本のファイルを除くか(1/0)
  local hits
  [ "${#swift[@]}" -eq 0 ] && return
  hits=$(grep -HnE "$2" "${swift[@]}" 2>/dev/null \
    | grep -v 'apple-ui-allow' \
    | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|\*|/\*)' \
    | { if [ "$3" = 1 ]; then grep -vE '/[^/]*(Theme|Colors?|Tokens?|Styles?|Palette|Typography)[^/]*\.swift:'; else cat; fi; })
  if [ -n "$hits" ]; then printf '\n## %s\n%s\n' "$1" "$hits"; fail=1; fi
}

# --- Liquid Glass から外れる書き方 ---
if [ "${#plists[@]}" -gt 0 ]; then
  hits=$(grep -HnA1 'UIDesignRequiresCompatibility' "${plists[@]}" | grep -E '<true/>|YES' || true)
  [ -n "$hits" ] && { printf '\n## UIDesignRequiresCompatibility で Liquid Glass を切っている（SDK 27 でビルドすると無視される。外して直す）\n%s\n' "$hits"; fail=1; }
fi
report 'Material の板を浮いた操作に敷いている → .glassEffect(…) / .buttonStyle(.glass)（内容の層の背景なら apple-ui-allow）' \
  '\.background\((in: *)?\.?(ultraThin|thin|regular|thick|ultraThick|bar)Material' 0
report 'バーに色を塗っている（Liquid Glass のバーは塗らない。目立たせるのは中身の tint）' \
  '\.toolbarBackground\((Color|\.[a-z]+[A-Z]?[a-z]*,)|UINavigationBarAppearance\(\)|UITabBarAppearance\(\)|\.barTintColor *=|configureWithOpaqueBackground' 0
report 'ぼかし（UIBlurEffect / NSVisualEffectView）で浮いた操作を作っている → UIGlassEffect / NSGlassEffectView' \
  'UIBlurEffect\(|NSVisualEffectView\(' 0
report 'UIBarButtonItem の .done → .prominent（iOS 26 で改名）' \
  'style: *\.done|\.style *= *\.done' 0
report '角丸を layer で直書き → cornerConfiguration（UIKit 26 / AppKit 27）か glass の形' \
  'layer\??\.cornerRadius *=' 1

# --- 古い SwiftUI ---
report 'NavigationView → NavigationStack / NavigationSplitView' \
  '(^|[^A-Za-z])NavigationView[[:space:]]*[({]' 0
report 'NavigationLink(destination:) / isActive: → NavigationLink(value:) + navigationDestination' \
  'NavigationLink\((destination:|isActive:)|NavigationLink\([^)]*isActive:' 0
report 'ObservableObject 系 → @Observable と @State / @Bindable / @Environment' \
  '(:|,)[[:space:]]*ObservableObject([^A-Za-z]|$)|@Published|@StateObject|@ObservedObject|@EnvironmentObject|\.environmentObject\(' 0
report '.foregroundColor → .foregroundStyle' '\.foregroundColor\(' 0
report '.cornerRadius() → .clipShape(.rect(cornerRadius:)) / ConcentricRectangle / .glassEffect(in:)' '\.cornerRadius\(' 0
report '.accentColor → .tint' '\.accentColor\(' 0
report '.navigationBarTitle → .navigationTitle' '\.navigationBarTitle\(' 0
report 'Text + Text → 補間 Text("…\(…)")（iOS 26 で非推奨。訳すと語順が崩れる）' 'Text\([^)]*\)[[:space:]]*\+[[:space:]]*Text\(' 0
report 'PreviewProvider → #Preview' ':[[:space:]]*PreviewProvider' 0
report '.tabItem → TabView { Tab(…) }' '\.tabItem[[:space:]]*\{' 0
report 'onChange の 1 引数の閉包（iOS 17 で非推奨）→ { old, new in } か { }' \
  '\.onChange\(of:[^)]*\)[[:space:]]*\{[[:space:]]*[A-Za-z_]+[[:space:]]+in' 0
report 'navigationBarItems / navigationBarLeading・Trailing → toolbar の topBarLeading・Trailing' \
  '\.navigationBarItems\(|\.navigation(BarLeading|BarTrailing)([^A-Za-z]|$)' 0
report '.edgesIgnoringSafeArea → .ignoresSafeArea' '\.edgesIgnoringSafeArea\(' 0
report 'value の無い .animation() → .animation(_:value:) か withAnimation' '\.animation\(\.[A-Za-z]+(\([^)]*\))?\)|\.animation\(nil\)' 0
report 'Alert(…) / ActionSheet(…) → .alert / .confirmationDialog' '(^|[^A-Za-z.])(Alert|ActionSheet)\((title|message):' 0
report 'GeometryReader で大きさを測るだけ → onGeometryChange / containerRelativeFrame（使い道が違えば apple-ui-allow）' 'GeometryReader[[:space:]]*\{' 0
report 'DispatchQueue.main → @MainActor / Task / MainActor.run' 'DispatchQueue\.main\.' 0

# --- 値の直書き（正本ファイルでは許す）---
report '色の直書き → アセットカタログの色・.primary/.secondary・.tint・Color(.systemBackground) など' \
  'Color\((red|hue|white|\.sRGB|\.displayP3|hex)[:,]|UIColor\((red|white|hue|displayP3Red):|NSColor\((red|calibratedRed|srgbRed|deviceRed|white|calibratedWhite):|#colorLiteral' 1
report '文字の大きさの直書き → .font(.body/.headline…)（Dynamic Type が効く）' \
  '\.font\(\.system\(size:|UIFont\.systemFont\(ofSize:|NSFont\.systemFont\(ofSize:|\.font\(\.custom\([^)]*size: *[0-9]+\)' 1

# --- Liquid Glass の使い方の外れ（ファイル単位）---
for f in "${swift[@]}"; do
  n=$(grep -oE '\.glassEffect\(' "$f" | wc -l)
  if [ "$n" -ge 2 ] && ! grep -qE 'GlassEffectContainer' "$f"; then
    printf '\n## glassEffect が %s 個あるのに GlassEffectContainer が無い（ガラス同士は混ざらず、描画も重い）\n%s\n' "$n" "$f"; fail=1
  fi
done

if [ "$fail" = 0 ]; then echo 'apple-ui: 問題なし'; fi
exit "$fail"
