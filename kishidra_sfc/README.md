# キシドラ！ SFC 版（地上世界）

Safari 版（Godot 4 Web 版）キシドラ！の地上世界を、スーパーファミコン実機で動く ROM に逆移植したものです。
ゲームのルール・数値・文章・絵・音はすべて Safari 版のデータ（このリポジトリの Web ビルド）から変換しています。

- ROM: LoROM / FastROM / 4MB / バッテリー付き SRAM 32KB / NTSC
- 出力: `build/kishidra.sfc`

## 入っているもの

| 項目 | 内容 |
|---|---|
| エリア | アーケディア城〜灼熱の洞窟、魔王の城（10 面＋金策「魔王城の地下」）、月見の島（裏ボス ペコラータ）、蜃気楼の斜塔（18 WAVE） |
| 武器・盾 | N〜SR+ の全武器・全盾（スキル抽選、強化スロット、進化、フエルスロット・スロットリセット） |
| モンスター | 地上世界の全雑魚・全ボス（魔王の 3 形態、ペコラータ覚醒、ミミッくんの肉ガードで SR 宝箱） |
| 装備 | MAIN / SUB / THIRD / 盾。THIRD は Safari 版の RENTAL 枠で、持っている武器から自由に選べる |
| ガチャ | 剣・槍・槌・盾の混合ガチャのみ（剣 30%・槍 30%・槌 30%・盾 10%、N 70%・R 27%・SR 3%、1 回 1000 コイン、1 回 / 10 回） |
| 蜃気楼の斜塔 | ランキングなし。最終到達 WAVE（制覇・ターン数込みの最高記録）を表示 |
| 図鑑 | モンスター（93）・武器（197）・盾（43）・必殺技（13）・スキル（50）。モンスターは戦闘の絵（未発見はシルエット）と戦闘曲つき |
| その他 | チュートリアル、カカシ（ガードの練習）、宝の部屋（チケット）、道具屋、鍛冶屋（強化・進化・素材の合成）、難易度 5 段階、王さまのお話・エンディング |
| BGM | オープニング（タイトル・マップ）、雑魚戦 1、ボス戦 1（翼龍ドラゴス）、魔王戦（第 1・第 2 形態）、魔王最終形態、ペコラータ戦、勝利、敗北 |
| SE | 各種（バリエーションは 1 種類ずつ） |

入っていないもの: フレンド、ピックアップ・広告・必殺武器ガチャ、排出一覧、ランキング、地下・海・空の世界、図鑑の蜃気楼カード。
蜃気楼の斜塔は Safari 版では金土日祝だけ出ますが、SFC には時計がないので魔王の城クリア後はいつでも挑めます。

## 操作

- マップ: 十字キーでエリアを選ぶ、A で決定、Y / X / START でメニュー
- バトル: Y / X / A で MAIN / SUB / THIRD の武器のスキル、B でガード、R で肉、←→ で狙う敵、START でポーズ。
  画面下はスマホ版と同じ 4 列のパネル（武器のタブ・スキルのカードと ST の札・NEXT・ガード / 肉のボタン、
  HP バーと 10 マスの ST）で、各列の左上にボタン（Y / X / A / B / R）が出ます。
  スマホ版と同じく、技の名前（勇者の上）、必殺技の帯と白いフラッシュ、左上のコンボ（n HIT!、10 HIT から「必殺 x倍」）、
  WAVE n / WAVE CLEAR! / CLEAR、赤く光る WARNING、ST+n・ST -n・「次の技 …」が出ます。START のメニューは
  「続ける」「離脱する」（もう一度 A で離脱）
- メニュー: A 決定、B もどる、SELECT で持ち物の並び替え、←→ でページ送り
- 図鑑: L / R でタブ、SELECT で種類（武器・必殺技・スキル）、A でくわしく、カードは ←→ でとなり

### デバッグ（地図の画面で）

- L を 3 回: 全部のエリアと STAGE を開く（蜃気楼の斜塔・魔王城の地下も出る。元に戻せない）
- R を 3 回: デバッグメニュー（好きな武器・盾をもらう〈武器はスキルを選べる〉、道具を全部もらう、
  武器・盾を全種類もらう、図鑑を全部うめる、全部のエリアと STAGE を開く）

## ビルド

必要なもの: Python 3、[cc65](https://cc65.github.io/)（cc65 / ca65 / ld65。`CC65_HOME` を設定するか PATH に入れる）。

```
python build.py rom
```

`data/gen/` に変換済みのデータが入っているので、ROM を作るだけならこれだけで足ります。

ROM の大きさは環境変数 `ROM_MB` で選べます（既定は 4）。`ROM_MB=8 python build.py rom` で ExLoROM 8MB
（`ROM_MB=6` で 6MB）。先頭 4MB は 4MB の ROM と同じ並びで、その先（8MB はバンク $01〜$7D、6MB は $01〜$3F。SlowROM）には BGM と
DMA で読む絵だけを置きます。ExLoROM が動くのは snes9x 1.54 以降（RetroArch の snes9x コアを含む）、bsnes v115 / ares、
FXPak Pro / SD2SNES（ファームウェア 1.8.0 以降）、MiSTer。Mesen2、Super EverDrive、snes9x 1.50〜1.53（6MB なら可）、
Wii・ゲームキューブ版 snes9x（6MB まで）、bsnes-mercury では動きません。セーブ（SRAM）の形はどの大きさでも同じです。

### データの作り直し（Safari 版から変換し直すとき）

追加で必要なもの: Pillow、numpy、ffmpeg、gcc（BRR 変換プログラムのため）。
Safari 版の Web ビルド（`index.pck` などがあるフォルダ）は既定でこのフォルダの 1 つ上を見ます。別の場所にあるときは環境変数 `KISHIDORA_WEB` で指定します。

```
python build.py data     # 表・文章・フォント・絵
python build.py audio    # BGM と SE（SPC700 ドライバを含む）
python build.py rom
```

## 構成

- `src/asm/` 65816 のエンジン（起動、NMI、DMA、文字表示、圧縮展開、SRAM、サウンドの転送）
- `src/c/` ゲーム本体（cc65）。`battle_core.c` / `battle_hit.c` が Safari 版 battle_core.gd の移植、`battle_view.c` が戦闘画面、
  `map.c` `menu.c` `gacha.c` `forge.c` `story.c` `rewards.c` が冒険部分、`save.c` `adventure.c` がセーブとルール、
  `loadout.c` が装備の共通ルール（その装備で出せる必殺技 `sp_reach`、武器 1 本で必殺技が出せるか `solo_ok_sk`
  〈表は `src/asm/loadout.s` が ROM から読む。必殺10連の武器の一覧も同じ所から `solo_pool_id`〉と持ち物のその印 `INV_SOLO`、
  特殊効果のようす `fx_state`）。
  `ovl.h` は戦闘と画面が交代で使う作業領域 `ovl[1536]`
- `tools/make_data.py` `tools/make_gfx.py` `tools/make_audio.py` 変換スクリプト、`tools/audio/` SPC700 ドライバとアセンブラ・BRR 変換
- `src/text/ui.txt` 画面の文章（id と文）。make_data.py は `ui.txt` を読んでから `ui_*.txt` を名前の順に読む
  （画面の組ごとに 1 つずつ足せる。同じ id はどのファイルでも 1 回だけ）。数は `{n}` `{m}` `{k}`
- `tools/romplace.py` ROM のデータの置き場所。変換スクリプトはバンクを決めず、`data/gen` のファイルと
  `data/gen/place/*.json`（ラベル・大きさ・種類）を書く。`build.py` は 2 回リンクする（1 回目はデータなしで
  コードと固定の領域の大きさを測り、残りの場所にデータを置いて 2 回目）。置いた場所は `build/kishidra.place.txt`。
  アセット・文字列・BGM の表は `TABLES`（$C0 の先頭）。アセンブラでは ROM のデータのバンクを `.bank()` で書く
  （8 ビットは `#<.bank(x)` / `.byte <.bank(x)`。`^label` はコードのバンク $81〜$9F で 0 になる）。
  `lda f:label` で読むデータは `TABLES` か固定の領域に置くか、manifest で `"long": true`（$A0 以降に置かれる）。
  ビルドのときに `f:` と `^` の使い方を調べる。同じ中身のアセット・文字列は 1 つだけ置く
- `tools/harness/` テスト用（libretro コアで ROM を動かしてスクリーンショット・メモリ・音を保存する `runrom`、セーブ作成 `mksave.py`（セーブの形 v2。`--v1` で前の形）、
  処理時間の内訳 `prof.py`、文字のはみ出し・枠かぶり・色の混ざり・重なりの検査 `textcheck.py`
  〈どちらも `PROFILE=1 python build.py rom` で作る計測用 ROM が必要。計測用 ROM は NMI の転送が終わった走査線も
  $7F:DBFA（最大）と $7F:DA00〜（直近 32 フレーム）に残す〉、文字列の幅 `textwidth.py` など）
- テストの道具（`tools/harness/`）: `runrom` のスクリプトで WRAM を書く・読む `poke` / `pokew` / `pokel` / `peek` /
  `waitfor`（アドレスはリンクの記号で `sv+16` のように書ける。記号は build.py が書く `build/kishidra.lbl` から）、
  2 つの ROM で同じ操作をして画面・音・メモリを比べる `abtest.py`（`t/reg1.txt` が画面、`t/reg2.txt` が音の一巡り。
  セーブはスクリプトの `#!mksave` 行から作る）、録音を少しの時間のずれを許して区間ごとに比べる `wavcmp.py`、
  2 つの ROM が読むデータ（アセット・文字列・フォント・敵の能力・SPC・BGM を流す順）を比べる `romdata.py`、
  2 つの data/gen の絵をアセットごとに比べ、変わったスプライトのポーズを描いて並べる `gfxdiff.py`（`-o` で PNG）、
  置き場所のテスト `test_romplace.py`（`--build` で偽のデータを足してビルド。4MB と 8MB）、ラグの計測 `lag.py`、
  ROM のヘッダー・バンク $00 の写し・チェックサムと、snes9x・bsnes / ares・FXPak Pro・Mesen2 がどの形と読むかの検査
  `romcheck.py`（build.py もできた ROM に同じ検査をする）、エミュレーターやフラッシュカートが ExLoROM / ExHiROM の
  6MB・8MB を読めるかを画面の色（緑 = 読める、赤 = 読めない）で見る試験 ROM `probe/mkprobe.py`（`build/probe/` に作る）と
  それを libretro コアで動かす `probe/runprobe.py`、
  走査線ごとのスプライト数 `oamlines.py`、バンク・WRAM の予算 `budget.py`、
  セーブ v2 のテスト `savetest.py`（v1 からの移行・スロットの交代・こわれたスロットからの戻り）、
  装備の共通ルールと乱数のテスト `loadtest.py`（`tools/data/loadout_model.py` の Python のモデル〈Safari 版の
  必殺技の探索・特殊効果のようす〉と、TEST_HOOKS の ROM のテスト用の命令の答えを比べる）、
  文字と窓の道具のテスト `kitcheck.py`（TEST_HOOKS の ROM で `t/s6_*.txt` を動かす: `{k}`、行の間隔 `text_lh`、
  あとから開いた窓の下で閉じる窓、名前の入力 `name_input`、3 行の確認 `mk_confirm3`、数字のスプライト `mk_num_spr`）。
  `runrom` は `gcc -O2 -I/usr/include/libretro-common -o runrom runrom.c -ldl` で作る（tools/harness の道具は runrom がないときと runrom.c のほうが新しいときに作り直す）。
  `XDEFS="TEST_HOOKS …" python build.py rom` でテストだけの定義を入れた ROM を作る
  （TEST_HOOKS: `sys.c` のテスト用の命令箱 `dbg_cmd`。製品の ROM は変わらない。`t/se_all.txt` は BGM を止めて
  効果音を 1 つずつ鳴らして録音する）
- NMI の転送（OAM・パレット・BG マップ・転送キュー）は 1 フレーム 4864 バイトまでで、V ブランク内に収まる
  （残りは次のフレーム）。勇者・雑魚のポーズとスキルのアイコンの絵は、それを描く OAM と同じ NMI で送る
  （絵と並びがずれたフレームが出ない）。戦闘のメッセージ・コンボ・カードの文字は BG3 マップと同じ NMI で送る
  （古い文字が新しい色で、新しい文字が古い色で出るフレームがない）。転送キューはリング
- BGM のストリーム（SPC700 のリングに BRR ブロックを送る）は、どの画面でも V ブランクを待つ間に送る
  （リングが 0.76 秒を切ったときだけ、そのフレームの分を待つ前に送る）。戦闘のループが 1 フレームに収まりやすい。
  ドライバの空き数（ポート 2）はブロックの連続受信中は更新されないので、最後のブロックから 32 ライン空けて読む。
  起動後の効果音サンプルの転送も待ち時間に行う（タイトルで約 3 秒）
- ダメージ計算の百分率（`mul_pct`）と数字の桁分解はハードウェアの乗除算器で行う（`src/asm/math.s`、結果は C と同一）。
  乱数（`rnd`、範囲の乱数 `rnd_below`、`rnd_range` が使う乗算 `rnd_mul8`）も math.s（前の C と同じ列）
- 戦闘パネルのスプライト（アイコン・コストの札）は OAM の 96〜127 番に置き、変わったときだけ書き直す

フォントは東雲フォント（パブリックドメイン、`tools/font/SHINONOME_COPYRIGHT.txt`）と、戦闘パネルの小さい文字に
美咲ゴシック第2（フリーソフトウエア、`tools/font/MISAKI_COPYRIGHT.txt`）を使っています。
