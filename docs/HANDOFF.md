# 引き継ぎ（今の状態。2026-11-19）

## 目的
pixwall の Android 版（壁紙）と Wear OS 版（時計の文字盤）の作業を進める。今の山場は、再起動後に壁紙が黒い件の直しと、スマホから時計へ GIF を送る機能の、端末での確認。

## できたこと（android ブランチ）
- 68931d0: Android で再起動の直後に画像が読めなくても、黒いまま残さず読み直す。試験は通った。SPEC は【仮】（Pixel 3 では確かめていない）
- 8fc9f89: スマホの設定画面から、GIF を時計へ送る（Data Layer）。ビルドと M3E の検査は通った。SPEC は【仮】（届くかはまだ確かめていない）
- a733226: 「自由に置く」の形を本人が了承したことを記録した
- ここまでは origin/android へ push 済み。その後の、引き継ぎ・decisions・android-trial を書いた commit は、push していない（夜間モードで push はしない決まり。朝の確認一覧に載せた）
- to-fu で、スマホのエミュレーター（pixwall_phone15）と時計のエミュレーター（pixwall_watch）を組にした。スマホに「Sent.」と出るところまでは確かめた

## 残っていること
- 時計へ GIF を送る試験（保留中）: 時計には届いていない。to-fu の adb の窓口が外から止められ続けて、組の通り道（forward・reverse）が切れるため。経緯は `docs/decisions.md` の 2026-11-18
- 再起動後に黒い件の直しを、Pixel 3 で確かめる（保留中）
- 「自由に置く」を端末と Wear OS のエミュレーターで確かめる。時計の実機で GIF のコマの間合いを見る。設定画面のプレビューを実機で見る（どれも端末の保留が解けてから）

## 次のセッションが最初にやること
1. phase-board の確認一覧（下の「本人の判断待ち」）を見て、保留が解けたものがあるか確かめる
2. 解けていなければ、端末の要らない作業だけを進める。保留中の試験は始めない
3. Unity を止められると本人が言ったら、時計へ送る試験を再開する:
   1. to-fu で `pixwall_phone15` と `pixwall_watch` を起こす。`computer_launch_app` で、本人のログインのセッションから起こす（ssh から起こすと、ssh が切れたときに止まる）
   2. `adb -s emulator-5560 forward tcp:5601 tcp:5601` と `adb -s emulator-5570 reverse tcp:5601 tcp:5601` を張る
   3. 組が切れていたら、時計で `am force-stop com.google.android.gms` を実行して繋ぎ直させる
   4. スマホの pixwall の設定画面で GIF を選び、「Send to watch」を押す
   5. 時計の logcat に `pixwall: received <名前>` が出て、`files/gifs` に増え、config.json で選ばれているかを見る
   6. 届いたら、SPEC の「時計へ送る」を【確定】にし、`docs/android-trial.md` を直して commit する。確認一覧の項目を done にする

## 本人の判断待ち（phase-board の check に記録済み）
- to-fu の adb が止められ続けて、組が切れる件: Unity を止められるようになるまで保留
- スマホから時計へ GIF を送る機能を、組にした環境で確かめる: 上の件の保留が解けるまで保留
- 再起動後に黒い件の直しを Pixel 3 で確かめる: 本人の指示で保留
- 夜間に push しなかった commit を push するか（2026-11-19 の朝の確認一覧）

## 触ってはいけないもの・注意
- Pixel 3: 接続対応・再起動・実機の試験は、本人の指示で保留。再起動すると繋がらなくなるため
- to-fu の Pixel_10_Pro_Fold（emulator-5554）: Kite と Camp が使っている。Kanade_Fold・wave_watch も、ほかの作業のもの
- to-fu の Unity と unibridge: ほかのセッションが使っている。止めない
- adb を張り直す見張りは動かさない（本人の指示）。`%TEMP%` の `pixwall-pair-keep.ps1` は止めてある
- kite の毎晩の試験（1:00〜4:30 ごろ）の時間は、to-fu のエミュレーターを使わない
- main と android を一つにする案は、本人が了承していない。入力欄に薄く出る文（サジェスト）は、本人の指示として扱わない
- `SPEC.md` の 68931d0・8fc9f89 の項目は、端末で確かめるまで【仮】のまま

## to-fu のエミュレーター
- pixwall_phone15: android-35 の Play 版、pixel_8、RAM 3GB。Play にサインイン済み。Wear OS by Google で pixwall_watch と組にしてある。pixwall の debug 版と、見本の `/sdcard/Download/dot-64x64-30f.gif` が入っている
- pixwall_watch: Wear OS 6。pixwall の時計の debug 版が入っている
- 2台とも今は止まっている。AVD は残してある
- pixwall_phone（android-36.1）: surfaceflinger が落ち続けて使えなかったので、本人の了承を得て消した
- adb は SDK の `%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe`（37）。sdkmanager・avdmanager を使うときは、`JAVA_HOME` に Android Studio の jbr を指す

## 詳しい資料
- 仕様: `SPEC.md`。判断の経緯と、試してやめたこと: `docs/decisions.md`
- Android・Wear の試し方: `docs/android-trial.md`。ほかの OS は `docs/*-trial.md`
