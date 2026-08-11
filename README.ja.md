# S49 MK2 ⇄ SEQTRAK Raspberry Pi ブリッジ

[English](README.md) | [日本語](README.ja.md)

`KompleteControl_MK2` Pythonプロトコル調査
(`資料/KompleteControl_MK2-main_extracted/`) を、ヘッドレスのRaspberry Piで
動作するC++へ移植したプロジェクトです。USBハブ経由で次の機器を接続します。

- Native Instruments KOMPLETE KONTROL S49 MK2（LCD、Light Guide、ボタンLED、
  ノブ、ジョグホイール、鍵盤）
- YAMAHA SEQTRAK（11トラック・グルーヴボックス、MIDI入出力）

両機器間の中継・変換を提供します。ステップシーケンサーは内部エンジンの
雛形のみがあり、S49 MK2からのパターン編集や再生操作は未実装です。

## 参考動画

実際の動作は[YouTubeの参考動画](https://youtu.be/JYyXQgCoOQ0)で確認できます。

## ディレクトリ構成

```text
include/
  mk2_protocol.h       MK2 USB/HIDプロトコル定数
  seqtrak_protocol.h   SEQTRAK MIDI実装定数
  usb/                 HID (hidraw) + LCDバルク転送 (libusb)
  display/             RGB565キャンバス + LCDパケット生成
  midi/                ALSA rawmidi + MK2↔SEQTRAKルーター
  seq/                 ステップシーケンサーの内部エンジン雛形（操作機能は未実装）
  app/                 アプリケーションの結合とエントリーポイント
  util/                16進ダンプなどの共通ユーティリティ
src/                   include/に対応する.cpp実装
tests/                 動作確認用プログラム
```

プロトコルの詳細は、Python調査プロジェクトのソースと
`protocol.md`、および `SEQTRAK_data_list_En_D0.pdf` の「MIDI Data Format」
「MIDI Data Table」から抽出しています。各ヘッダーのコメントに出典と、
未確認または推定の値を記載しています。

## ビルド

Raspberry Pi本体、または aarch64/armhf クロス環境で実行します。

Raspberry Pi Zero WとRaspberry Pi 4で動作確認済みです。Raspberry Pi Zero 2 Wも
対象として想定していますが、実機では未確認です。

```bash
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev libasound2-dev
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

OS非依存部分（`mk2_protocol.h`、`seqtrak_protocol.h`、`display/*`、`seq/*`）は
macOSでも構文チェック済みです。`usb/*`（Linux `hidraw` + `libusb`）と
`midi/*`（ALSA）はLinux専用です。

## ハードウェアなしの検証

`tests/lcd_packet_selftest` はハードウェやRaspberry Piを必要とせず、
`BuildLcdPacket()` の出力を実機確認済みベクトルと比較します。

```bash
cmake -B build && cmake --build build --target lcd_packet_selftest
./build/lcd_packet_selftest   # ALL TESTS PASSED と表示されること
```

その他のテストは [`tests/README.md`](tests/README.md) を参照してください。

## 実行環境の設定

- **USB HID権限**: MK2のHIDインターフェースは `/dev/hidrawN` として認識されます。
  非rootユーザーで読み書きできるよう、例えば `/etc/udev/rules.d/99-mk2.rules` に
  次のudevルールを追加します。

  ```text
  SUBSYSTEM=="hidraw", ATTRS{idVendor}=="17cc", MODE="0660", GROUP="plugdev"
  ```

- **LCD用USBバルクアクセス**: インターフェース3（ベンダークラス）には
  カーネルドライバーがないため、`libusb` から直接利用できます。
  `plugdev` 用のudevルールを追加するか、rootで実行します。
- **MIDIデバイス**: MK2とSEQTRAKは `/dev/snd/midiC*D*` のALSA rawmidiデバイスとして
  認識される必要があります。`amidi -l` または `aconnect -l` で確認してください。

## 実行

```bash
./build/s49mk2_bridge            # 通常実行
./build/s49mk2_bridge --dry-run  # 読み取りのみの安全な確認
./build/s49mk2_bridge --help
```

起動時にMK2のHIDとLCDバルクエンドポイント、両方のMIDIポートを開き、
MK2↔SEQTRAK MIDIリレーを開始します。空の内部シーケンサークロックも動作しますが、
現在はステップを入力するUIやアプリ層の処理はありません。Ctrl-Cで安全に終了します。

### `--dry-run`

`--dry-run` ではデバイスの読み取りは行いますが、一切書き込みません。
LCDパケットは送信せず16進ダンプし、MIDIメッセージもMK2やSEQTRAKへ
送信せず内容を表示します。実機を操作する前の動作確認に利用してください。

### 推奨する導入手順

1. Mac上で `tests/lcd_packet_selftest` を実行します。
2. MK2だけをUSBハブ経由でPiに接続し、`lsusb` と `/dev/hidraw*` を確認します。
3. Pi上で `tests/hid_input_dump` を実行し、ノブ・ボタン・ジョグ入力を確認します。
4. MK2だけを接続したまま `./build/s49mk2_bridge --dry-run` を実行します。
5. SEQTRAKも接続し、`amidi -l` でデバイス名を確認してから再度 `--dry-run` を実行します。
6. 上記の確認後に `./build/s49mk2_bridge` を通常実行します。

## 未実装・最小構成の機能

- **ステップシーケンサー（操作機能は未実装）**: 各トラック16ステップ、
  1ステップ1音の内部エンジン雛形のみ実装されています。アプリから
  `StepSequencer::SetStep` を呼ぶ処理はなく、S49 MK2からパターンを
  入力・編集・再生・停止するUIもありません。そのため、現状ではユーザー機能として
  利用できません。スイング、発音確率、オートメーション、永続化も未実装です。
- **MK2 → SEQTRAKコントロール**: ノブとFunctionボタンをSEQTRAKのCCへ変換します。
  MK2自身のHID割り当て、Light Guide、ボタンLEDフィードバックなどは未実装です。
- **LCD UI**: 表示機能は開発中です。利用できる描画プリミティブは
  `display/lcd_canvas.h` を参照してください。
- **SysEx**: ルーターはSysExを透過中継します。SEQTRAK Parameter Change / Dump Requestの
  生成機能は一部のみ実装されています。
