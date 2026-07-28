# 画面・ボタン操作フロー

`screens.md`、`buttons.md`、C++実装、およびPenpotファイル
`S49MK2 Controller`の`Shinonome12 Layouts`ページを基にしたフローです。

## 表記

- 実線: 仕様として定義されている画面遷移または処理
- 点線: Penpot Prototypeだけに設定されている遷移
- 「設計済み」: Penpotに画面はあるが、C++では未実装
- 「未定義」: ボタンは定義されているが、遷移先画面の仕様がない

## 仕様上の画面・ボタンフロー

```mermaid
flowchart TD
    START([アプリ起動]) --> HOME["SCR-001<br/>SEQTRAK Controller<br/>実装済み"]

    HOME -->|"BTN-001 Play"| PLAY{"S49 MK2と<br/>SEQTRAKが接続済み?"}
    PLAY -->|Yes| LOG["SCR-012<br/>MIDI LOG<br/>実装済み"]
    PLAY -->|No| ERROR["スルーを開始せず<br/>未接続機器をStatus表示"]
    LOG -->|"BTN-159 Prev."| HOME
    ERROR --> HOME

    HOME -->|"BTN-002 Sound Select"| VAR["SCR-002<br/>Variation 01<br/>実装済み"]
    VAR -->|"BTN-010～020<br/>トラック選択・ジョグ押下"| TRACK{"選択トラック／<br/>Track Type"}
    TRACK -->|"DX"| DX["SCR-005<br/>DX Sound Category<br/>設計済み"]
    TRACK -->|"SAMPLER"| SAMPLER["SCR-006<br/>SAMPLER Sound Category<br/>設計済み"]
    TRACK -->|"Drum"| DRUM["SCR-003<br/>Drum Sound Category<br/>設計済み"]
    TRACK -->|"Synth"| SYNTH["SCR-004<br/>Synth Sound Category<br/>設計済み"]
    TRACK -->|"DrumKit"| DRUMSET["SCR-008<br/>DrumSet<br/>設計済み"]

    DRUM -->|"BTN-030～044<br/>カテゴリ選択"| LIST["SCR-007<br/>Sound List<br/>設計済み"]
    SYNTH -->|"BTN-050～064<br/>カテゴリ選択"| LIST
    DX -->|"BTN-070～084<br/>カテゴリ選択"| LIST
    SAMPLER -->|"BTN-090～104<br/>カテゴリ選択"| LIST
    LIST -->|"BTN-110～124<br/>Sound選択"| AUDITION["SEQTRAKへSoundを設定<br/>C3を0.5秒送信して試聴"]
    AUDITION --> LIST
    DRUMSET -->|"BTN-130～137<br/>Type／パート選択"| DRUMSET

    HOME -->|"BTN-003 Setting"| SETTINGS["SCR-009<br/>Settings<br/>画面表示実装済み"]
    SETTINGS -->|"BTN-150 S49MK2"| S49["S49 MK2設定<br/>画面仕様は未定義"]
    SETTINGS -->|"BTN-151 SERTRAK"| SEQ["SEQTRAK設定<br/>画面仕様は未定義"]
    SETTINGS -->|"BTN-152 Controller"| CTRL["Controller設定<br/>画面仕様は未定義"]
    SETTINGS -->|"BTN-153 Key Split"| SPLIT["SCR-010<br/>Key Split<br/>設計済み"]
    SETTINGS -->|"BTN-154 Set CC/PC"| CCPC["SCR-011<br/>Set CC/PC<br/>設計済み"]
    SETTINGS -->|"BTN-160 Prev."| HOME
    SPLIT -->|"BTN-155 OK<br/>確定"| SETTINGS
    SPLIT -->|"BTN-156 Cancel<br/>破棄"| SETTINGS
    CCPC -->|"BTN-157 OK<br/>確定"| SETTINGS
    CCPC -->|"BTN-158 Cancel<br/>破棄"| SETTINGS

```

`Play`はMIDIスルーを開始し、SCR-012 `MIDI LOG`へ遷移します。

## 共通の操作フロー

```mermaid
flowchart LR
    INPUT{"入力"} -->|"ジョグ回転／左右"| MOVE["選択項目を移動"]
    INPUT -->|"ジョグ押下"| EXEC["選択中の項目を実行"]
    INPUT -->|"Function Button 1～3"| DIRECT["ホームの対応ボタンを直接実行"]
    MOVE --> DRAW["LCDを再描画"]
    EXEC --> ACTION["画面遷移／値選択／処理実行"]
    DIRECT --> ACTION
    ACTION --> DRAW
```

SCR-002の現在のC++実装では、ジョグ押下後も別画面へは遷移せず、同一画面内で
`TrackSelect` → `TrackTypeSelect` → `TrackDetail`とモードが変わります。
`TrackDetail`ではKnob 1がVolume、Knob 2がPanを変更します。

## 現在のC++実装フロー

```mermaid
stateDiagram-v2
    [*] --> ControllerHome

    state ControllerHome {
        [*] --> HomeSelection
        HomeSelection --> HomeSelection: ジョグ回転／選択移動
        HomeSelection --> HomeSelection: Setting／将来画面のため現状維持
    }

    ControllerHome --> MidiLog: Play／MIDIスルーON
    MidiLog --> ControllerHome: Prev.
    ControllerHome --> Settings: Setting
    Settings --> ControllerHome: Prev.
    Settings --> KeySplit: Key Split
    Settings --> SetCcPc: Set CC/PC
    KeySplit --> Settings: OK／Cancel
    SetCcPc --> Settings: OK／Cancel
    ControllerHome --> TrackSelect: Sound Select
    TrackSelect --> TrackSelect: ジョグ回転／トラック選択
    TrackSelect --> TrackDetail: DXまたはSAMPLERでジョグ押下
    TrackSelect --> TrackTypeSelect: その他のトラックでジョグ押下
    TrackTypeSelect --> TrackTypeSelect: ジョグ回転／Type選択
    TrackTypeSelect --> TrackDetail: ジョグ押下
    TrackDetail --> TrackDetail: Knob 1／Volume変更
    TrackDetail --> TrackDetail: Knob 2／Pan変更
```

現状のC++で画面IDとして実装されているのは`ControllerHome`、
`SoundSelect`、`Settings`、`KeySplit`、`SetCcPc`、`MidiLog`です。
カテゴリ、Sound List、DrumSetへの遷移は未実装です。

## Penpot Prototype遷移

Penpotで確認できたClick → Navigate toのみを抜き出した図です。

- 緑・`✅`: C++で遷移を実装・検証済み
- 灰・`⬜`: 未実装

```mermaid
flowchart LR
    HOME["SEQTRAK Controller"]
    VAR["Variation 01"]
    SETTINGS["Settings"]
    SPLIT["Key Split"]
    CCPC["Set CC / PC"]
    LOG["✅ MIDI LOG"]
    DS["Drum Set"]
    DSC["Drum Sound Category"]
    SSC["Synth Sound Category"]
    DxSC["DX Sound Category"]
    SaSC["Sampler Sound Category"]
    SLIST["Sound List"]

    HOME -->|"✅ Play"| LOG
    HOME -->|"✅ Sound Select"| VAR
    HOME -->|"✅ Setting"| SETTINGS
    VAR -->|"⬜ Next"| DS
    VAR -->|"⬜ Next"| DSC
    VAR -->|"⬜ Next"| SSC
    VAR -->|"⬜ Next"| DxSC
    VAR -->|"⬜ Next"| SaSC
    DS -->|"⬜ KICK/SNARE/CLAP/HAT 1/HAT 2/PERC 1/PERC 2"| DSC
    DSC -->|"⬜ KICK/SNARE/CLAP/HAT 1/HAT 2/PERC 1/PERC 2"| SLIST
    SSC -->|"⬜ Bass/KeyBoard"| SLIST
    DxSC -->|"⬜ Bass/KeyBoard"| SLIST
    SaSC -->|"⬜ Vocal/SFX"| SLIST
    SLIST -->|"⬜ Next"| DS
    SLIST -->|"⬜ Next"| DSC
    SLIST -->|"⬜ Next"| SSC
    SLIST -->|"⬜ Next"| DxSC
    SLIST -->|"⬜ Next"| SaSC
    SETTINGS -->|"✅ Prev."| HOME
    SETTINGS -->|"✅ Key Split"| SPLIT
    SETTINGS -->|"✅ Set CC / PC"| CCPC
    SPLIT -->|"✅ OK"| SETTINGS
    SPLIT -->|"✅ Cancel"| SETTINGS
    CCPC -->|"✅ OK"| SETTINGS
    CCPC -->|"✅ Cancel"| SETTINGS
    LOG -->|"✅ Prev."| HOME

    linkStyle 0,1,2,18,19,20,21,22,23,24,25 stroke:#2e7d32,stroke-width:3px
    linkStyle 3,4,5,6,7,8,9,10,11,12,13,14,15,16,17 stroke:#808080,stroke-width:2px
    classDef implemented fill:#d7f5df,stroke:#2e7d32,stroke-width:3px,color:#1b5e20
    class LOG implemented
```

Settings内の図示されていないその他のボタンには、Penpot Prototypeリンクは
設定されていません。
