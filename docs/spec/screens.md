# 画面一覧

## 画面遷移

```text
アプリ起動
└─ SEQTRAK Controller
   ├─ Play → MIDIスルーを開始してMIDI LOGへ移動
   │  └─ Prev. → SEQTRAK Controller
   ├─ Sound Select → S49 MK2 LCD — Variation 01 — Shinonome12
   │  ├─ Prev. → SEQTRAK Controller
   │  ├─ DX／SAMPLER → 対応するSound Category
   │  └─ Drum／Drum Kit／Synth → 対応するSound Category／Drum Kit
   │     └─ 各展開先のPrev. → Variation 01
   └─ Setting → Settings
      └─ Key Split → Key Split（実装完了）
      └─ Set CC → Set CC（一部実装）
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
- 選択トラックのTrack Type
- 右LCDに全11トラックのCategory／Sound一覧

### 操作

- ジョグ回転: トラックまたは設定値を選択する
- ジョグ押下: 選択したトラックの詳細へ進む
- Prev.: SEQTRAK Controllerへ戻る
- Volume／Pan表示は廃止し、設定済みCategory／Soundを表示する
- ヘッダー右のOK: 保持している全トラックのSoundプリセットをCC0／CC32／
  Program ChangeでSEQTRAKへ一括送信する

### トラック／Track Type確定
- Track:DXの場合
  DX Sound Category — Shinonome12 へ展開する
- Track:SAMPLERの場合
  SAMPLER Sound Category — Shinonome12 へ展開する
- TrackType:Drumの場合
　Drum Sound Category — Shinonome12　へ展開する
- TrackType:Drum Kitの場合
  Drum Kit — Shinonome12 へ展開する
- TrackType:Synthの場合
  Synth Sound Category — Shinonome12 へ展開する

## SCR-003: Drum Sound Category — Shinonome12

- 状態: 実装済み（カテゴリーボタン、選択、Sound List遷移）
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、カテゴリボタン3列 × 5段
- 用途: DrumのSound Categoryを選択する
- Prev.: Variation 01へ戻る
- カテゴリー: Kick、Snare、Rim、Clap、Snap、Closed HiHat、Open HiHat、
  Shaker / Tambourine、Ride、Crash、Tom、Bell、Conga / Bongo、World、SFX

###　Nextボタン
- 選択されたカテゴリをパラメータとしてSound List — Shinonome12を開く

## SCR-004: Synth Sound Category — Shinonome12

- 状態: 実装済み（カテゴリーボタン、選択、Sound List遷移）
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、カテゴリボタン3列 × 5段
- 用途: SynthのSound Categoryを選択する
- Prev.: Variation 01へ戻る
- カテゴリー: Bass、Synth Lead、Piano、Keyboard、Organ、Pad、Strings、
  Brass、Woodwind、Guitar、World、Mallet、Bell、Rhythmic、SFX

###　Nextボタン
- 選択されたカテゴリをパラメータとしてSound List — Shinonome12を開く

## SCR-005: DX Sound Category — Shinonome12

- 状態: 実装済み（カテゴリーボタン、選択、Sound List遷移）
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、カテゴリボタン3列 × 5段
- 用途: DXのSound Categoryを選択する
- Prev.: Variation 01へ戻る
- カテゴリー: Bass、Synth Lead、Piano、Keyboard、Organ、Pad、Strings、
  Brass、Woodwind、Guitar、World、Mallet、Bell、Rhythmic、SFX

###　Nextボタン
- 選択されたカテゴリをパラメータとしてSound List — Shinonome12を開く

## SCR-006: SAMPLER Sound Category — Shinonome12

- 状態: 実装済み（カテゴリーボタン、選択、Sound List遷移）
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、カテゴリボタン3列 × 5段
- 用途: SAMPLERのSound Categoryを選択する
- Prev.: Variation 01へ戻る
- カテゴリー: Vocal Count、Vocal Phrase / Chant、Singing Vocal、
  Robotic Vocal / Effect、Riser、Laser / Sci-Fi、Impact、
  Noise / Distorted Sound、Ambient / Soundscape、SFX、Scratch、
  Nature / Animals、Hit / Stab / Musical Instrument Sound、Percussion、
  Recorded Sound

###　Nextボタン
- 選択されたカテゴリをパラメータとしてSound List — Shinonome12を開く

## SCR-007: Sound List — Shinonome12

- 状態: 実装済み（一覧表示、選択、SEQTRAKへのBank Select／Program Change送信）
- サイズ: 左右LCD各480 × 272 px
- レイアウト: ヘッダー28 px、左右各10件、合計20件単位のページ表示
- 用途: 選択したカテゴリ内のSoundを選択する
- Soundデータ: `SEQTRAK_data_list_En_D0.pdf`から抽出したDrum 855件、
  Synth 1,077件、DX 100件、SAMPLER 392件
- ジョグ回転: Prev.とカテゴリー内の全Soundを移動する
- ジョグ押下: 選択Soundを対象トラックへ設定する
- 通常トラック: CC0／CC32／Program Change送信後、Variation 01へ戻る
- Drum Kitパート: Part番号に応じたMIDI CH 1～7とMSB `0x20`～`0x26`を
  使用し、設定後はDrum Kitへ戻って対象パートにSound名を表示する
- Prev.: 選択元のSound Categoryへ戻る

## SCR-008: Drum Kit — Shinonome12

- 状態: 画面実装済み（Type／パートの選択表示まで。設定処理は未実装）
- サイズ: 480 × 272 px
- レイアウト: ヘッダー28 px、ボタン2段、フッター
- 用途: Drumパートと設定値を選択する
- Prev.: Variation 01へ戻る
- ジョグ回転: Prev.、Type、KICK～PERC 2の選択を移動する
- ジョグ押下: Prev.選択時はVariation 01へ戻る。KICK～PERC 2選択時は
  Drum Sound Categoryへ進む。Typeの設定処理は今後実装する
- 各パートには設定済みSound名を表示する
- ヘッダー右のOK: Variation 01へ戻り、対象トラックのCategory／Sound表示を
  `Drum Kit`にする

### TODO: Drum Kitトラック設定

- 各パートのSound設定はData List記載のMIDI CH 1～7、Bank Select MSB
  `0x20`～`0x26`、LSB、Program Changeで実装済み
- SEQTRAKのトラック自体をDrum Kit型へ切り替える外部MIDI手順はData Listから
  特定できていない。実機解析後にDrum KitのOK処理へ追加する

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
- Set CC
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
- Drum Kitボタン: 9 ZoneのDrum Kitプリセットを読み込む

#### Drumプリセット

- Zone 01: C1～B1、CH12、Transpose 0、Light Guide消灯
- Zone 02～08: C2～B2を7分割、CH1～7、各開始音をC4へ移調
- Zone 09: C3～B3、CH8、開始音をC4へ移調
- Zone 10: C4～B4、CH9、開始音をC4へ移調
- Zone 11: C5～B5、CH10、開始音をC4へ移調
- Zone 12: C6～B6、CH11、開始音をC4へ移調
- Zone 13: C7～G9、CH13、Transpose 0、Light Guide消灯
- Drum用Zoneの色は隣接Zoneで異なる色にする

#### Drum Kitプリセット

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

## SCR-011: Set CC

- 状態: 現行仕様完了（確認済みコントロールのCC設定は完了。
  未解析の操作子／メッセージ種別は将来拡張のTODO）
- サイズ: 480 × 272 px
- レイアウト: 左右LCD各480 × 272 px、ヘッダー28 px、行ピッチ22 px
- 用途: S49 MK2の確認済みCC割り当てを設定する
- 左LCD: Knob 1～8、Mod Wheel
- 右LCD: Button 1～8
- ヘッダー: OK、Cancelボタン
- Type: CC固定（青）
- 各行: Control名、Type、3桁の値入力欄
- 番号入力範囲: 000～127
- 番号のデフォルト値: CC 000
- 操作方法: ジョグの上下クリックで行、左右クリックで左右LCDを移動し、
  ジョグ回転で選択中のCC番号を変更する

### 左列

| 操作子 | 選択可能Type |
|---|---|
| Knob 1～8 | CC |
| Mod Wheel | CC |

### 右列

| 操作子 | 選択可能Type |
|---|---|
| Button 1～8 | CC |

### 過去の解析・テスト状況

- Knob 1～8／Button 1～8: HID `0xA1`のCC割り当てとMIDI出力を確認済み
- Mod Wheel: HID `0xA2`によるCC割り当てとMIDI出力を確認済み
- Prog／Note: HID形式が未確認のため、選択・保存・送信機能から除外

### TODO: 実機確認後に追加する操作子

- Program Change／Note: Button／KnobへのHID割り当て形式を解析・実機確認後、
  選択肢と保存・送信処理を追加する
- Touch Strip: CCイベントを観測済みだが、タイミングと割り当てを再検証する
- Pedal A／B: HID `0xA3`のレポート形式のみ判明。ペダル実機では未検証
- 未確認操作子は画面へ表示せず、ブリッジ変換による代替実装も行わない

### TODO: パネルキーのTransport機能

- S49 MK2のPlay／Stopキー押下を検出し、SEQTRAKへMIDI System Real-Timeを
  直接送信する
- Play: Start (`FA`)、Stop: Stop (`FC`)
- 再開操作が必要な場合はContinue (`FB`)を使用する
- 外部MIDIクロック同期時はTiming Clock (`F8`)を継続送信する
- SEQTRAK側のMIDI Syncが`MIDI(Auto)`であることを前提条件とする
- Start／Stop用のCCはSEQTRAKの受信仕様に存在しないため、Set CCの割り当て
  対象には含めない
- CCへのブリッジ変換ではなく、SEQTRAKが受信可能なTransportメッセージを
  直接送信する独立機能として実装する

### 番号入力

- CC番号を000～127で入力する

### OK

- 編集内容を`s49mk2_control_assignments.conf`へ保存し、次回起動／画面表示時に
  復元する
- Knob／ButtonをHID `0xA1`、Mod WheelをHID `0xA2`でS49 MK2へ反映する
- 設定後はSettings画面へ戻る

### Cancel

- 編集中の変更を破棄する
- Settings画面へ戻る

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
