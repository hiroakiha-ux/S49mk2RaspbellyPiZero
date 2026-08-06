# ボタン一覧

ボタンIDは、表示名が変わっても仕様とプログラムから同じボタンを参照できるようにするための識別子です。

| ボタンID | 画面 | 表示 | 実行内容 | 実装状態 |
|---|---|---|---|---|
| BTN-001 | SEQTRAK Controller | Play | MIDIスルーを有効にしてSCR-012へ移動する | 実装済み |
| BTN-002 | SEQTRAK Controller | Sound Select | SCR-002へ移動する | 実装済み |
| BTN-003 | SEQTRAK Controller | Setting | Setting画面へ移動する | 実装済み |
| BTN-004 | Variation 01 | Prev. | SEQTRAK Controllerへ戻る | 実装済み |
| BTN-010～020 | Variation 01 | KICK～SAMPLER | 対象トラックを選択する | 実装済み |
| BTN-021 | Sound Category | Prev. | Variation 01へ戻る | 実装済み |
| BTN-022 | Drum Kit | Prev. | Variation 01へ戻る | 実装済み |
| BTN-023 | Variation 01 | OK | 全トラックのSoundプリセットを一括送信する | 実装済み |
| BTN-024 | Drum Kit | OK | Drum Kitを確定してVariation 01へ戻る | 現行仕様完了 |
| BTN-030～044 | Drum Sound Category | PDF記載のカテゴリ名 | Sound Listへ移動する | 実装済み |
| BTN-050～064 | Synth Sound Category | PDF記載のカテゴリ名 | Sound Listへ移動する | 実装済み |
| BTN-070～084 | DX Sound Category | PDF記載のカテゴリ名 | Sound Listへ移動する | 実装済み |
| BTN-090～104 | SAMPLER Sound Category | PDF記載のカテゴリ名 | Sound Listへ移動する | 実装済み |
| BTN-110～124 | Sound List | Sound名 | 対象トラックへSoundを設定する | 実装済み |
| BTN-130 | Drum Kit | Type | Drum KitのTypeを選択する | 選択表示実装済み |
| BTN-131 | Drum Kit | KICK | Drum Sound Categoryへ移動する | 実装済み |
| BTN-132 | Drum Kit | SNARE | Drum Sound Categoryへ移動する | 実装済み |
| BTN-133 | Drum Kit | CLAP | Drum Sound Categoryへ移動する | 実装済み |
| BTN-134 | Drum Kit | HAT 1 | Drum Sound Categoryへ移動する | 実装済み |
| BTN-135 | Drum Kit | HAT 2 | Drum Sound Categoryへ移動する | 実装済み |
| BTN-136 | Drum Kit | PERC 1 | Drum Sound Categoryへ移動する | 実装済み |
| BTN-137 | Drum Kit | PERC 2 | Drum Sound Categoryへ移動する | 実装済み |
| BTN-150 | Settings | S49MK2 | S49 MK2設定画面へ移動する | 未実装 |
| BTN-151 | Settings | SERTRAK | SEQTRAK設定画面へ移動する | 未実装 |
| BTN-152 | Settings | Controller | Controller設定画面へ移動する | 未実装 |
| BTN-153 | Settings | Key Split | Key Split設定画面へ移動する | 実装完了 |
| BTN-154 | Settings | Set CC | CC設定画面へ移動する | 現行仕様完了 |
| BTN-155 | Key Split | OK | Key Split設定を確定する | 実装完了 |
| BTN-156 | Key Split | Cancel | 変更を破棄してSettingsへ戻る | 実装完了 |
| BTN-157 | Set CC | OK | CC設定を確定してSettingsへ戻る | 現行仕様完了 |
| BTN-158 | Set CC | Cancel | CC設定の変更を破棄してSettingsへ戻る | 現行仕様完了 |
| BTN-159 | MIDI LOG | Prev. | SCR-001へ戻る | 実装済み |
| BTN-160 | Settings | Prev. | SEQTRAK Controllerへ戻る | 実装済み |

## Penpot Prototypeリンク

以下はPenpotファイル`S49MK2 Controller`の`Shinonome12 Layouts`ページから取得した実際のリンク情報です。
この表にないボタンのPrototypeリンクは未設定です。

| ボタンID | リンク元レイヤー | Trigger | Action | 遷移先 |
|---|---|---|---|---|
| BTN-001 | `Action Button / 01 / Play` | Click | Navigate to | MIDI LOG |
| BTN-002 | `Action Button / 02 / Sound Select` | Click | Navigate to | S49 MK2 LCD — Variation 01 — Shinonome12 |
| BTN-003 | `Action Button / 03 / Setting` | Click | Navigate to | Settings |
| BTN-153 | `Settings Button / 01-2 / Key Split` | Click | Navigate to | Key Split |
| BTN-154 | `Settings Button / 01-3 / Set CC / PC` | Click | Navigate to | Set CC/PC |
| BTN-155 | `Header Action / OK / Button` | Click | Navigate to | Settings |
| BTN-156 | `Header Action / Cancel / Button` | Click | Navigate to | Settings |
| BTN-157 | `Interactive Button / OK / Link Source / Shinonome12` | Click | Navigate to | Settings |
| BTN-158 | `Interactive Button / Cancel / Link Source / Shinonome12` | Click | Navigate to | Settings |
| BTN-159 | `Header Action / Prev / Button` | Click | Navigate to | SEQTRAK Controller |
| BTN-160 | `Header Action / Prev / Button` | Click | Navigate to | SEQTRAK Controller |

### Prototypeリンクの記述ルール

- `Trigger`: Penpotで操作を開始する条件
- `Action`: Penpotで実行するPrototypeアクション
- `遷移先`: Navigate toのDestinationとなるキャンバス名
- Prototypeを変更した場合は、この表も同時に更新する
- プログラム上の処理とPrototype遷移は別仕様として扱う

## BTN-001: Play

### 条件

- S49 MK2が接続されている
- SEQTRAKが接続されている

### 成功

- S49 MK2から受信したMIDI信号をSEQTRAKへ送る
- `MIDI thru: ON`と表示する

### 失敗

- MIDIスルーを開始しない
- 未接続の機器をStatusに表示する

## BTN-002: Sound Select

### 実行

- `S49 MK2 LCD — Variation 01 — Shinonome12`へ移動する

## BTN-003: Setting

### 実行

- Setting画面へ移動する

### 備考

- Setting画面の内容は今後決定する
