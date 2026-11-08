# Mac 版の試し方

MacBook Air M1 で試す。

## ビルドと単体テスト

```sh
cd macos
swift test          # 並べ方・待ち時間・止める条件
./build-app.sh      # build/pixwall.app を作る
```

## 動かす

`./install-app.sh` でビルドして `/Applications/pixwall.app` に入れ、入れたものから始める（動いていた pixwall は終わらせ、前に `~/Applications` に入れていたものは消す）。`build/` の中から直接起動しない（`~/Documents` の中から起動すると、書類フォルダへのアクセスの許可を求められる）。画像も `~/Library/Application Support/pixwall/` など、許可の要らない場所に置く。

```sh
./install-app.sh
D="$HOME/Library/Application Support/pixwall"; mkdir -p "$D"
cp ../samples/dot-64x64-30f.gif "$D/"
open /Applications/pixwall.app --args "$D/dot-64x64-30f.gif"
```

終えるときは `pkill -x pixwall`。

## 見ること

- デスクトップのアイコン・ウィジェットの後ろに出て、メニューバーと Dock の下まで描かれている
- Dock と、⌘Tab のアプリの切り替えに pixwall が出ない
- ドットがぼやけていない
- 起動したあと、システム設定 > 一般 > ログイン項目の「ログイン時に開く」に pixwall.app が入っている。ログアウトしてログインし直すと pixwall が始まる。設定画面で「ログイン時に起動」を切ると一覧から消える

- 動かしている間は OS の壁紙も1枚目になっている（`~/Library/Application Support/pixwall/desktop` に PNG と `original.json`、`wallpaper-store.plist`。`pkill -9 -x pixwall` で落とすと見える）。設定画面の「pixwall を終了」か ⌘Q で終えると、写真などの壁紙も含めて元に戻る（`~/Library/Application Support/com.apple.wallpaper/Store/Index.plist` のどの操作スペースも、pixwall の PNG を指していない）
- 操作スペースの切り替えは、Mission Control をアプリとして開いて（`com.apple.exposelauncher`）縮小図を押せばできる（送ったキー操作は届かなかった）。写真の操作スペースがあるまま起動し、そこへ移ってから終える。また Mission Control の「＋」で操作スペースを足してから終える。どちらも全部の操作スペースが写真に戻る
- 設定画面の「静止画を書き出す…」で、書き出す絵のつまみで選んだ枚が、画面の大きさの PNG に書かれる

## コマンドライン

`/Applications/pixwall.app/Contents/MacOS/pixwallctl`（ssh からでも、常駐側への知らせは届く）。使い方は `pixwallctl help`。試す前に `config.json` を写しておき、終わったら戻す。

- `status` が `running`。`set fit integer background custom backgroundColor "#203040"` で `~/Library/Application Support/pixwall/desktop` の PNG が描き直され、`export out.png --frame 2` の PNG の左上が (32, 48, 64)
- `pause` のあと 10 秒の CPU 時間（上の「隠れたときに止まるか」）が増えず、`status` が `paused`。`resume` で増える
- `set speed 9`・`set image nope.gif`・`export out.png --frame 999` は断られ（終わりの値 2・1・2）、`show` が変わらない
- 設定画面を開いたまま `set speed 2 fit fill` と `pause` をすると、設定画面の並べ方・速さ・一時停止が変わる（画面を撮れないときは `osascript` の System Events で部品の値を読む）。そのあと設定画面で何か変えても、`show` に `speed 2`・`fill` が残る
- `quit` で常駐側と設定画面が終わり、`status` が `stopped`

## 終了しても壁紙を残す

```sh
C=/Applications/pixwall.app/Contents/MacOS/pixwallctl
$C set keepWallpaperOnQuit true; $C quit
osascript -e 'tell application "System Events" to get picture of every desktop'   # pixwall の PNG のまま
open /Applications/pixwall.app; sleep 5; $C set keepWallpaperOnQuit false; $C quit   # 元の壁紙に戻り、desktop/ が空になる
```

2026-11-09 に M1 で確かめた（元は動く壁紙の DefaultDesktop.heic。オフで終えると、設定ファイルの控えから戻った）。設定画面の一番下に「終了しても壁紙を残す」と、終えたときにどうなるかの説明が出る

## リソースを測る

```sh
top -l 4 -s 5 -stats pid,command,cpu,mem -pid $(pgrep -fl MacOS/pixwall | grep -v -- --settings | cut -d' ' -f1)
```

設定画面を開いている間は、プロセスが2つになる（`pixwall --settings` が設定画面）。常駐側だけを測るときは、`pgrep -x pixwall` ではなく、`pgrep -fl MacOS/pixwall` から `--settings` の付いたものを除く（起動の引数で画像を渡したときも拾えるように）。メモリは `footprint -p <pid> | grep phys_footprint:` でも見られる。設定画面を開いて閉じたあとも、常駐側が開く前と同じ（11MB ほど）であること。

目安は `SPEC.md` の「常駐の目安」。`samples/dot-64x64-30f.gif`（64×64・30フレーム・100ms）で測る。

## 隠れたときに止まるか

30 秒の CPU 時間で見る（`top` の IDLEW はタイマーで起きた回数を数えないので使えない）。

```sh
p=$(pgrep -fl MacOS/pixwall | grep -v -- --settings | cut -d' ' -f1); a=$(ps -o cputime= -p $p); sleep 30; ps -o cputime= -p $p; echo "前: $a"
```

- ほかの窓を小さくしておく: 増える（動いている）
- ほかのアプリの窓を Window > Fill か、タイトルバーのダブルクリックで画面いっぱいにする: 3 秒ほどでほとんど増えなくなる（止まる）
- その窓を元の大きさに戻すか、デスクトップをクリックする: また増える
- その窓を全画面（別の操作スペース）にする: 増えない

## 言語を変えて開く

- 設定画面は Mac の言語の並びで日本語か英語になる。並びを変えずに試すときは `/Applications/pixwall.app/Contents/MacOS/pixwall --settings -AppleLanguages "(en)"`（日本語は `"(ja)"`）
