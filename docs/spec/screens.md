# 画面一覧

## 画面遷移

```text
アプリ起動
└─ SEQTRAK Controller
   ├─ Play → MIDIスルーを開始してMIDI LOGへ移動
   │  └─ Prev. → SEQTRAK Controller
   ├─ Sound Select → S49 MK2 LCD — Variation 01 — Shinonome12
   └─ Setting → Settings
      └─ Key Split → Key Split（実装完了）
      └─ Set CC/PC → Set CC/PC（将来実装）
```

## SCR-001: SEQTRAK Controller

- 状態: 実装済み
- サイズ: 480 × 272 px
- フォント: Shinonome 12を想定
- 初期表示: アプリ起動時

### 表示

- `SEQTRAK controller`
- `Using KOMPLETE KONTROL S49 MK2`
- S49 MK2の接続状態
- SEQTRAKの接続状態
- MIDIスルーの状態

### ボタン

- Play
- Sound Select
- Setting

### 操作

- ジョグ回転: ボタンの選択を移動する
- ジョグ押下: 選択中のボタンを実行する
- Function Button 1～3: 対応するボタンを直接実行する

## SCR-002: S49 MK2 LCD — Variation 01 — Shinonome12

- 状態: 実装済み
- サイズ: 480 × 272 px
- 用途: SEQTRAKのトラックを選択し、音色設定へ進む

### 表示

- KICK
- SNARE
- CLAP
- HAT1
- HAT2
- PERC1
- PERC2
- SYNTH1
- SYNTH2
- DX
- SAMPLER
- 選択トラックのTrack Type、Volume、Pan

### 操作

- ジョグ回転: トラックまたは設定値を選択する
- ジョグ押下: 選択したトラックの詳細へ進む

### Nextボタン
- Track:DXの場合
  DX Sound Category — Shinonome12 へ展開する
- Track:SAMPLERの場合
  SAMPLER Sound Category — Shinonome12 へ展開する
- TrackType:Drumの場合
　Drum Sound Category — Shinonome12　へ展開する
- TrackType:DrumKitの場合
  DrumSet — Shinonome12 へ展開する
- TrackType:Synthの場合
  Synth Sound Category — Shinonome12 へ展開する

## SCR-003: Drum Sound Category — Shinonome12

- 状態: 設計済み
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、カテゴリボタン3列 × 5段
- 用途: DrumのSound Categoryを選択する

###　Nextボタン
- 選択されたカテゴリをパラメータとしてSound List — Shinonome12を開く

## SCR-004: Synth Sound Category — Shinonome12

- 状態: 設計済み
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、カテゴリボタン3列 × 5段
- 用途: SynthのSound Categoryを選択する

###　Nextボタン
- 選択されたカテゴリをパラメータとしてSound List — Shinonome12を開く

## SCR-005: DX Sound Category — Shinonome12

- 状態: 設計済み
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、カテゴリボタン3列 × 5段
- 用途: DXのSound Categoryを選択する

###　Nextボタン
- 選択されたカテゴリをパラメータとしてSound List — Shinonome12を開く

## SCR-006: SAMPLER Sound Category — Shinonome12

- 状態: 設計済み
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、カテゴリボタン3列 × 5段
- 用途: SAMPLERのSound Categoryを選択する

###　Nextボタン
- 選択されたカテゴリをパラメータとしてSound List — Shinonome12を開く

## SCR-007: Sound List — Shinonome12

- 状態: 設計済み
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、空ラベルボタン3列 × 5段、右側に縦スクロール領域
- 用途: 選択したカテゴリ内のSoundを選択する

- 選択されたサウンドをSEQTRAKに設定して、C3ノートを0.5秒送ってサンプルを鳴らす。

## SCR-008: DrumSet — Shinonome12

- 状態: 設計済み
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、ボタン2段、フッター
- 用途: Drumパートと設定値を選択する

### ボタン

- Type
- KICK
- SNARE
- CLAP
- HAT 1
- HAT 2
- PERC 1
- PERC 2

### フッター

- Ch　　　表示しない（トラックでチャンネルは決まっているため）
- Note　　表示しない（８音のノートは決まっているため）
- Vol　　　選択されたエレメントのボリューム
- Pan　　　選択されたエレメントのパン

## SCR-009: Setting

- 状態: 一部実装（画面表示、項目選択、定義済み画面への遷移）
- サイズ: 480 × 272 px
- Penpotキャンバス名: Settings
- レイアウト: ヘッダー28 px、ボタン3列
- 用途: 接続やMIDIルーティングなどの設定を行う

### ヘッダー

- Prev.: SEQTRAK Controllerへ戻る

### ボタン

- S49MK2
- Key Split
- Set CC/PC
- SERTRAK
- Controller

## SCR-010: Key Split

- 状態: 実装完了（画面表示、全項目編集、プリセット、実機設定送信、
  OK確定／Cancel破棄）
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、本文17行、行ピッチ14 px
- 用途: S49 MK2の鍵盤分割を設定する

### ヘッダー操作

- OK: 設定を確定する
- Cancel: 変更を破棄して戻る

### 1行目

- ラベル: Zone数：
- 入力範囲: 1～16
- デフォルト値: 1
- Drumボタン: 13 ZoneのDrumプリセットを読み込む
- DrumSetボタン: 9 ZoneのDrumSetプリセットを読み込む

#### Drumプリセット

- Zone 01: C1～B1、CH12、Transpose 0、Light Guide消灯
- Zone 02～08: C2～B2を7分割、CH1～7、各開始音をC4へ移調
- Zone 09: C3～B3、CH8、開始音をC4へ移調
- Zone 10: C4～B4、CH9、開始音をC4へ移調
- Zone 11: C5～B5、CH10、開始音をC4へ移調
- Zone 12: C6～B6、CH11、開始音をC4へ移調
- Zone 13: C7～G9、CH13、Transpose 0、Light Guide消灯
- Drum用Zoneの色は隣接Zoneで異なる色にする

#### DrumSetプリセット

- Zone 01: C1～B1、CH4、開始音をC4へ移調
- Zone 02: C2～B2、CH1、開始音をC4へ移調
- Zone 03: C3～B3、CH8、開始音をC4へ移調
- Zone 04: C4～B4、CH9、開始音をC4へ移調
- Zone 05: C5～B5、CH10、開始音をC4へ移調
- Zone 06: C6～B6、CH11、開始音をC4へ移調
- Zone 07: C7～B7、CH5、開始音をC4へ移調
- Zone 08: C8～B8、CH6、開始音をC4へ移調
- Zone 09: C9～G9、CH7、開始音をC4へ移調
- 全Zoneに色を付け、隣接Zoneで異なる色にする

### 2～17行目

- 表示形式: `Zone01:C1-G9 CH:1 Trans:0(C1) Color:■`
- 操作方法：ジョグダイヤルの上下クリックで行、左右クリックで列を選択し、
　　ジョグダイヤルの回転で選択中の値を変更する
　　上下移動では列位置を保持し、左右端ではカーソルを循環させない
　　Zone数によって、対象外のZoneはnot enable状態にする。
- Zone番号: 01～16　
- Key Range: Zone 1の開始はC1固定。各Zoneの開始は直前Zoneの終了の
  半音上とし、重複を許可しない。最終Zoneの終了はMIDIノート127（G9）固定
- Key Range初期値: C1～G9　
- 音名表記: 標準MIDIノート番号（C1 = 24、G9 = 127）
- MIDI Channel初期値: 1
- Transpose入力範囲: -64～+63半音
- Transpose表示形式: `0(C4)`、`+1(C#4)`、`-1(B3)`のように、
  移調量と各Zoneの開始キーを基準にした移調後の音名を併記する
- Transpose初期値: 0（括弧内は各Zoneの開始キー）
- Color: カラースウォッチで表示する

### OK

- Zoneの設定を確定し、S49 MK2へHID `0xA4` Key Zoneレポートを送信する
- 設定後はSettings画面へ戻る

### Cancel

- 編集中の変更を破棄する
- Settings画面へ戻る

## SCR-011: Set CC/PC

- 状態: 一部実装（画面表示とOK／Cancel遷移）
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、2列、行ピッチ22 px
- 用途: S49 MK2のCC/PC割り当てを設定する
- ヘッダー左: 現在のPage番号
- ヘッダー右: OK、Cancelボタン
- デフォルトPage: 1
- デフォルトType: CC
- 各行の右端: 3桁の番号入力欄
- 番号入力範囲: 000～127
- 番号のデフォルト値: 000

### 左列

| 操作子 | 選択可能Type |
|---|---|
| Knob 1～8 | CC、PC |
| Pedal A | CC、PC |
| Pedal B | CC、PC |

### 右列

| 操作子 | 選択可能Type |
|---|---|
| Button 1～8 | CC、PC、Note |
| Touch Strip | CC |

### 番号入力

- TypeがCCの場合: CC番号を入力する
- TypeがPCの場合: Program番号を入力する
- TypeがNoteの場合: MIDI Note番号を入力する
- Typeを切り替えても、各Typeの番号を個別に保持する

### OK

- Page 1～4とグローバル操作子の設定を確定する
- 設定後はSettings画面へ戻る

### Cancel

- 編集中の変更を破棄する
- Settings画面へ戻る

### Page

- Knob 1～8とButton 1～8はPage 1～4ごとに設定する
- Pedal A、Pedal B、Touch Stripはグローバル設定とする

## SCR-012: MIDI LOG

- 状態: 実装済み
- サイズ: 480 × 272 px
- 配置: SEQTRAK Controllerの下
- レイアウト: ヘッダー28 px、ログ一覧、右側スクロール領域
- 用途: S49 MK2からSERTRAKへ送信するMIDI信号を時系列表示する

### ヘッダー

- Prev.: 前の画面へ戻る
- タイトル: MIDI LOG

### ログ項目

- MIDI Channel
- NOTE ON
- NOTE OFF
- Note番号またはNote名
- Velocity
- Control Change番号と値
- Program Change番号

### 表示例

```text
CH TYPE      DATA
01 NOTE ON   C3 V:100
01 NOTE OFF  C3 V:000
01 CC 074    V:064
01 PC 010
```

### ボタン
- Prev.: ジョグ押下またはFunction Button 1でSCR-001へ戻る
