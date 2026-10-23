#!/usr/bin/env bash
# Wear OS の文字盤（android/wearface）に GIF を入れる。WFF は絵をぼかして拡大するので、
# 文字盤の座標（454×454。画面の画素と同じ）に収まる最大の整数倍まで先にニアレストネイバーで拡大し、
# 真ん中の位置・余白の色（1枚目の左上の1ドット）を watchface.xml に書く。ImageMagick を使う。
# 使い方: wear_face_gif.sh <画像.gif>
set -eu
src="$1"
size=454
res="$(dirname "$0")/../android/wearface/src/main/res"
xml="$res/raw/watchface.xml"

read -r w h n < <(magick identify -format '%W %H\n' "$src" | awk '{w=$1;h=$2;n++} END{print w,h,n}')
long=$(( w > h ? w : h ))
scale=$(( size / long )); [ "$scale" -lt 1 ] && scale=1
sw=$(( w * scale )); sh=$(( h * scale ))
x=$(( (size - sw) / 2 )); y=$(( (size - sh) / 2 ))
bg=$(magick "$src[0]" -format '%[hex:p{0,0}]' info: | cut -c1-6 | tr 'A-F' 'a-f')

magick "$src" -coalesce -sample "${sw}x${sh}!" -layers OptimizeFrame "$res/drawable-nodpi/pixwall.gif"
magick "$src[0]" -sample "${sw}x${sh}!" -background "#$bg" -gravity center -extent "${size}x${size}" "$res/drawable-nodpi/preview.png"

# 絵の位置と余白の色を書き換える（Fill は余白の1つだけ）
sed -i -E \
  -e "s|(<PartAnimatedImage name=\"gif\") x=\"[0-9]+\" y=\"[0-9]+\" width=\"[0-9]+\" height=\"[0-9]+\"|\1 x=\"$x\" y=\"$y\" width=\"$sw\" height=\"$sh\"|" \
  -e "s|(<Fill color=\")#ff[0-9a-fA-F]{6}|\1#ff$bg|" \
  "$xml"

# Play の審査と同じ数え方のメモリ（幅×高さ×4バイト×枚数）。動いている間の上限は 100MB
echo "${w}x${h} を ${scale} 倍の ${sw}x${sh} に（${n} 枚、余白 #$bg、メモリ約 $(( sw * sh * 4 * n / 1000000 ))MB）"
