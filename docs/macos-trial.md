# Mac 版の試し方

MacBook Air M1 で試す。

## ビルドと単体テスト

```sh
cd macos
swift test          # 並べ方・待ち時間・止める条件
./build-app.sh      # build/pixwall.app を作る
```

## 動かす

`.app` は `~/Applications` に置いてから起動する（`~/Documents` の中から起動すると、書類フォルダへのアクセスの許可を求められる）。画像も `~/Library/Application Support/pixwall/` など、許可の要らない場所に置く。

```sh
rm -rf ~/Applications/pixwall.app && cp -R build/pixwall.app ~/Applications/
D="$HOME/Library/Application Support/pixwall"; mkdir -p "$D"
cp ../samples/dot-64x64-30f.gif "$D/"
open ~/Applications/pixwall.app --args "$D/dot-64x64-30f.gif"
```

終えるときは `pkill -x pixwall`。

## 見ること

- デスクトップのアイコン・ウィジェットの後ろに出て、メニューバーと Dock の下まで描かれている
- Dock と、⌘Tab のアプリの切り替えに pixwall が出ない
- ドットがぼやけていない

## リソースを測る

```sh
top -l 4 -s 5 -stats pid,command,cpu,mem -pid $(pgrep -x pixwall)
```

目安は `SPEC.md` の「常駐の目安」。`samples/dot-64x64-30f.gif`（64×64・30フレーム・100ms）で測る。
