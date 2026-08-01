# ボタン一覧

ボタンIDは、表示名が変わっても仕様とプログラムから同じボタンを参照できるようにするための識別子です。

| ボタンID | 画面 | 表示 | 実行内容 | 実装状態 |
|---|---|---|---|---|
| BTN-001 | SEQTRAK Controller | Play | MIDIスルーを有効にしてSCR-012へ移動する | 実装済み |
| BTN-002 | SEQTRAK Controller | Sound Select | SCR-002へ移動する | 実装済み |
| BTN-003 | SEQTRAK Controller | Setting | Setting画面へ移動する | 実装済み |
| BTN-010～020 | Variation 01 | KICK～SAMPLER | 対象トラックを選択する | 実装済み |
| BTN-030～044 | Drum Sound Category | PDF記載のカテゴリ名 | カテゴリを選び、Sound Listへ移動する | 未実装 |
| BTN-050～064 | Synth Sound Category | PDF記載のカテゴリ名 | カテゴリを選び、Sound Listへ移動する | 未実装 |
| BTN-070～084 | DX Sound Category | PDF記載のカテゴリ名 | カテゴリを選び、Sound Listへ移動する | 未実装 |
| BTN-090～104 | SAMPLER Sound Category | PDF記載のカテゴリ名 | カテゴリを選び、Sound Listへ移動する | 未実装 |
| BTN-110～124 | Sound List | Sound名 | Soundを選択する | 未実装 |
| BTN-130 | DrumSet | Type | DrumSetのTypeを選択する | 未実装 |
| BTN-131 | DrumSet | KICK | KICKパートを選択する | 未実装 |
| BTN-132 | DrumSet | SNARE | SNAREパートを選択する | 未実装 |
| BTN-133 | DrumSet | CLAP | CLAPパートを選択する | 未実装 |
| BTN-134 | DrumSet | HAT 1 | HAT 1パートを選択する | 未実装 |
| BTN-135 | DrumSet | HAT 2 | HAT 2パートを選択する | 未実装 |
| BTN-136 | DrumSet | PERC 1 | PERC 1パートを選択する | 未実装 |
| BTN-137 | DrumSet | PERC 2 | PERC 2パートを選択する | 未実装 |
| BTN-150 | Settings | S49MK2 | S49 MK2設定画面へ移動する | 未実装 |
| BTN-151 | Settings | SERTRAK | SEQTRAK設定画面へ移動する | 未実装 |
| BTN-152 | Settings | Controller | Controller設定画面へ移動する | 未実装 |
| BTN-153 | Settings | Key Split | Key Split設定画面へ移動する | 実装完了 |
| BTN-154 | Settings | Set CC/PC | CC/PC設定画面へ移動する | 実装済み |
| BTN-155 | Key Split | OK | Key Split設定を確定する | 実装完了 |
| BTN-156 | Key Split | Cancel | 変更を破棄してSettingsへ戻る | 実装完了 |
| BTN-157 | Set CC/PC | OK | CC/PC設定を確定してSettingsへ戻る | 遷移のみ実装 |
| BTN-158 | Set CC/PC | Cancel | CC/PC設定の変更を破棄してSettingsへ戻る | 遷移のみ実装 |
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
