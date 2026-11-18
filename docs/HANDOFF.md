# 引き継ぎ（今の状態。2026-11-18）

## できたこと（android ブランチ、origin へ push 済み）
- 68931d0: Android で再起動の直後に画像が読めなくても、黒いまま残さず読み直す。SPEC は【仮】（Pixel 3 で未確認）
- 8fc9f89: スマホの設定画面から GIF を時計へ送る（Data Layer）。SPEC は【仮】（届くことを未確認）
- a733226: 「自由に置く」の形を本人が了承したことを記録

## 保留していること（phase-board の check に記録済み）
- **Pixel 3 の接続対応・再起動・実機の試験**: 本人の指示で保留。再起動すると繋がらなくなるため
- **時計へ GIF を送る試験**: to-fu でスマホ（pixwall_phone15）と時計（pixwall_watch）を組にし、スマホに「Sent.」までは出た。時計には届いていない
  - 原因: to-fu の adb の窓口が外から止められ続け、組の通り道（forward・reverse）が切れる。候補の Unity は unibridge とその中のセッションが使っていて、今は止められない
  - Unity を止められるまで保留。adb を張り直す見張りは動かさない
  - 再開の手順:
    1. 2台を起こす
    2. `adb -s emulator-5560 forward tcp:5601 tcp:5601` と `adb -s emulator-5570 reverse tcp:5601 tcp:5601` を張る
    3. スマホの pixwall で「Send to watch」を押す
    4. 時計の logcat に `received …` が出て、`files/gifs` に増えるかを見る

## to-fu のエミュレーター
- pixwall_phone15: android-35 の Play 版、pixel_8。Play にサインイン済み。Wear OS by Google で pixwall_watch と組にしてある
- pixwall_watch: 時計
- 2台とも今は止めてある。AVD は残してある
- pixwall_phone（android-36.1）: 2026-11-18 に本人の了承を得て消した。surfaceflinger が落ち続け、使えなかったため
- Pixel_10_Pro_Fold（5554）は Kite と Camp が使うので、触らない
