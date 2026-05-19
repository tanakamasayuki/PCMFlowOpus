# PCMFlowOpus 仕様

> English: [SPEC.md](SPEC.md)

## 1. スコープ

**PCMFlowOpus** は [PCMFlow](https://github.com/tanakamasayuki/PCMFlow) 向けのオプション Opus コーデックアドオン。**リアルタイム双方向音声 / パケット無線**(VoIP、ESP-NOW トランシーバ、WebSocket / UDP 音声リンクなど)に焦点を絞る。

責務:

- 16-bit PCM フレーム(通常 8/16/24/48 kHz の 20 ms、mono または stereo)を **生 Opus パケットへエンコード**
- 生 Opus パケットを 16-bit PCM フレームへ**デコード**。PLC(パケットロス補償)・FEC(前方誤り訂正)対応

いずれも **1 パケット単位**(~40–200 byte 程度、最大 1275 byte)で動作する。バイト列のフレーミングは呼び出し側の責任 — ESP-NOW フレーム、UDP データグラム、RTP ペイロード、WebSocket バイナリメッセージ等がそのまま運搬媒体になる。

このライブラリは PCMFlow パイプラインへ**インタフェースレベルで接続するだけ**。PCMFlow 本体(リングバッファ、フォーマット変換、ゲイン、出力デバイス連携)は再実装しない。

## 2. 非目標

- **Ogg/Opus コンテナ**(`.opus` / `.ogg` ファイル)— 初版対象外。§「将来対応」参照。
- **音声ファイル再生 / 出力デバイス制御** — PCMFlow の責務。
- **`Stream` / ファイルシステム / ネットワークアダプタ** — PCMFlow の `ByteStream` / `ByteSink` 側の責務。
- **Jitter buffer / RTP パーサ / ネットワーク転送** — 呼び出し側の責任。PCMFlowOpus はコーデックであってスタックではない。
- **コーデックの再実装** — 上流 Xiph.Org のリファレンス実装(`xiph/opus`)を無改変で利用。

## 3. 主目的ユースケース:ESP-NOW トランシーバ

設計の動機となるユースケース。ESP-NOW は 1 パケット最大 250 byte ペイロードで、Opus 20 ms 音声パケットがちょうど良く収まる:

| ビットレート | 20 ms フレームあたり |
|--------------|----------------------|
| 16 kbps(狭帯域音声)| ~40 B |
| 24 kbps(標準音声)| ~60 B |
| 32 kbps(広帯域音声)| ~80 B |
| 64 kbps(音楽寄り音声)| ~160 B |

参考に、生 16 kHz mono 16-bit PCM は 20 ms あたり 640 B なので、Opus を被せると **10–15 倍**圧縮できる。多ノード同報(ESP-NOW broadcast)も実用に。

エンド-to-エンドのトランシーバ用サンプルは [examples/EspNowTransceiver/](examples/EspNowTransceiver/) に同梱(マイク→Opus エンコード→ESP-NOW broadcast / ESP-NOW 受信→Opus デコード→I2S DAC、すべて 1 スケッチに収める)。

## 4. 公開 API(計画)

2 クラス。両方とも **1 Opus パケット単位**で動作する。

### 4.1 `OpusEncoder` — `PCMSink` を実装

16-bit interleaved PCM を受け、Opus パケットを出力する。

```cpp
OpusEncoder enc;
enc.begin({16000, 1, 16},                    // 入力 PCM: rate, channels, bits
          OpusApplication::Voip,             // Voip / Audio / LowDelay
          /*bitrate_bps=*/24000);

uint8_t pkt[400];
const size_t n = enc.encodeFrame(pcm20ms,    // 1 Opus フレーム分(16 kHz/20 ms なら 320 samples)
                                 pkt, sizeof(pkt));
// pkt[0..n) を ESP-NOW / UDP / WebSocket で送信
```

公開オプション:
- `setBitrate(bps)` — 実行時変更可
- `setComplexity(0..10)` — CPU vs 品質
- `setFrameDuration(ms)` — 2.5 / 5 / 10 / 20 / 40 / 60
- `setDtx(bool)` — 無音区間の不送信(DTX)
- `setInbandFec(bool)` — デコーダ側 FEC とペアで利用
- `setPacketLossPerc(0..100)` — エンコーダ側に想定ロス率を伝え、in-band FEC の積極度を制御

### 4.2 `OpusDecoder` — `PCMSource` を実装

Opus パケットを受け、16-bit interleaved PCM を出力する。

```cpp
OpusDecoder dec;
dec.begin({16000, 1, 16});                   // 出力 PCM: rate, channels, bits

int16_t pcm[320];

// 通常経路
size_t frames = dec.decodePacket(pkt, n, pcm, 320);

// パケットロス時 — SILK PLC で 20 ms を補間
frames = dec.decodePacket(nullptr, 0, pcm, 320);

// FEC 冗長 — 現在パケットの in-band FEC から前回パケットを復元
frames = dec.decodePacketFec(pkt, n, pcm, 320);
```

`OpusEncoder::format()` / `OpusDecoder::format()` が示すのは PCM 側のフォーマット。ネットワーク/パケット側は不透明バイト列。

### 4.3 PCMFlow パイプラインへの接続

`OpusDecoder` は `PCMSource` を実装するので、`PCMFlow::setInputSource(dec)` で PCMFlow のリングバッファ / フォーマット変換 / 出力デバイス連携につながる。呼び出し側がパケットを `decodePacket(...)` で供給し、PCMFlow 側は `pump()` / `readFrames()` で PCM を消費する、という二段構造。

`OpusEncoder` は `PCMSink` を実装。録音タスクから 16-bit PCM を `writeFrames(...)` で押し込むと、内部で 1 Opus フレーム分のサンプルが溜まったらエンコードし、コールバックまたは指定 `ByteSink` にパケットを出す。

詳細シグネチャとエラー列挙は実装と同時に [src/](src/) のヘッダで確定する。

## 5. PCM 入出力フォーマット

- サンプルレート: **8000 / 12000 / 16000 / 24000 / 48000 Hz**(Opus のネイティブレート)
- ビット深度: **16-bit signed**
- チャネル数: **1 (mono) または 2 (stereo)**
- フレーム長: 呼び出し側が 2.5 / 5 / 10 / 20 / 40 / 60 ms から選択。**20 ms が VoIP デフォルト**

別レートでの出力(例: ESP32-S3 で I2S DAC を 44.1 kHz 駆動)が必要なら、PCMFlow のリサンプラに任せる。PCMFlowOpus は Opus のネイティブレートしか扱わない。

## 6. メモリ・フットプリント目標

実装後に計測する暫定目標値:

| 項目 | 目標 |
|------|------|
| Flash(エンコーダ + デコーダ、fixed-point)| ≤ 180 KB |
| Flash(デコーダのみ、リンク時破棄) | ≤ 100 KB |
| RAM、エンコーダ永続状態 | ~20–30 KB |
| RAM、デコーダ永続状態 | ~15–20 KB |
| 呼び出しごとのスクラッチ | 上記に含む |

ビルド構成(手書きの [src/external/opus_config.h](src/external/opus_config.h) で供給するので `./configure` は不要):

- `FIXED_POINT=1`(FPU 非依存、コード縮小)
- `OPUS_BUILD=1`
- `DISABLE_FLOAT_API=1`
- `VAR_ARRAYS=1`(GCC など VLA 対応)/ なければ `USE_ALLOCA=1`
- `HAVE_LRINTF=1`(対応ツールチェイン)
- `OPUS_ARM_MAY_HAVE_NEON=0`(既定 off、ターゲット毎に再考)

これらは sync スクリプトの対象外で、ユーザが触る唯一の vendor 領域ファイル。

## 7. リポジトリ構成

```
PCMFlowOpus/
├─ README.md / README.ja.md
├─ SPEC.md   / SPEC.ja.md
├─ CHANGELOG.md
├─ LICENSE
├─ library.properties        # Arduino IDE
├─ library.json              # PlatformIO
├─ keywords.txt              # Arduino IDE シンタックスハイライト
├─ src/
│  ├─ PCMFlowOpus.h          # 集約ヘッダ
│  ├─ OpusEncoder.h/.cpp     # PCMSink、opus_encoder を pImpl で隠蔽
│  ├─ OpusDecoder.h/.cpp     # PCMSource、opus_decoder を pImpl で隠蔽
│  ├─ pcmflowopus_version.h  # tools/bump_version.py が生成
│  └─ external/
│     ├─ LICENSE_opus.md     # クレジット + BSD-3-Clause 全文
│     ├─ UPSTREAM.lock       # xiph/opus の tag と SHA256
│     ├─ opus_config.h       # 手書き、./configure 生成物の代替
│     └─ opus/               # xiph/opus の必要サブセットを verbatim
├─ examples/
│  └─ EspNowTransceiver/     # 主目的ユースケースのサンプル
├─ tests/
│  ├─ README.md / README.ja.md
│  ├─ conftest.py
│  ├─ pyproject.toml
│  ├─ smoke/
│  ├─ opus_encoder/
│  ├─ opus_decoder/
│  ├─ roundtrip/             # encode → decode で元波形と比較
│  ├─ external_source/       # PCMFlow::setInputSource 経路の結合
│  └─ tools/gen_test_audio.py
├─ tools/
│  ├─ bump_version.py        # 親 PCMFlow のものをそのまま流用
│  └─ sync_opus.py           # 未実装、§「リリースフロー」参照
└─ .github/
   └─ workflows/             # 未実装、§「リリースフロー」参照
```

## 8. Vendor している上流 {#vendor-している上流}

初版で取り込む上流プロジェクトは 1 つだけ:

| プロジェクト | 上流 | ライセンス | 役割 |
|--------------|------|------------|------|
| **opus** | [xiph/opus](https://github.com/xiph/opus) | BSD-3-Clause | Opus コーデック本体(エンコーダ + デコーダ) |

Vendor ルール:

1. GitHub の tag tarball(`codeload.github.com/xiph/opus/tar.gz/refs/tags/<tag>`)から取得。これは `opus-codec.org` の公式 tarball と同一だがスクリプトで機械的に扱える。
2. MCU 向けエンコード/デコードに必要なファイルのみ抽出。除外: `tests/`、`doc/`、`autotools/`、`configure*`、`Makefile.am`、`win32/`、`.github/`、`silk/float/`、`dnn/`(1.5+ で追加された機械学習強化、本構成では未使用)。
3. Vendor ファイルは**無改変**で保持。上流のライセンスヘッダ・バージョン銘を残す。`./configure` が生成する構成は手書きの `src/external/opus_config.h` で供給する。
4. `src/external/UPSTREAM.lock` に tag と SHA256 を記録し、再現性を担保。CI で「lock と実体に差分なし」を検証する。

ライセンス: BSD-3-Clause。クレジットと全文は [src/external/LICENSE_opus.md](src/external/LICENSE_opus.md)。

## 9. リリースフロー {#リリースフロー}

PCMFlowOpus は上流追随の自動化レベルとして **L1 — 通知のみ** を採用する。PCMFlowOpus 自体のリリースは完全自動だが、上流 `libopus` の取り込みは**意図的に手動**にする。

### L1 — 通知のみ(採用)

[`.github/workflows/upstream-check.yml`](.github/workflows/upstream-check.yml) が週次で動作。`tools/sync_opus.py --check-upstream` を呼び、GitHub API で `xiph/opus` の tag を取得して最新の非 pre-release tag と [`src/external/UPSTREAM.lock`](src/external/UPSTREAM.lock) の `tag` フィールドを比較する。新 tag があれば Issue を起票(または既存を更新):

```
chore: bump vendored libopus to v<new>
```

本文には上流リリースノートへのリンクと次のアクションを記載。メンテナはローカルで以下を実行:

```sh
python tools/sync_opus.py --apply                # 最新の非 pre-release tag
python tools/sync_opus.py --apply --tag v1.6.1   # 特定 tag を指定
```

スクリプトは `codeload.github.com` から tarball を取得し SHA256 を検証、`src/external/opus/`(`.gitkeep` を保護)を一旦消してから §7 の抽出ルールで再展開、`UPSTREAM.lock` を更新する。メンテナはローカルでテスト → commit → release ワークフロー(§「最終リリースのタグ付け」)を起動する。

**なぜ L2(自動 PR + host pytest + ESP32 ビルドチェック)ではないか:**
- 上流のリリース頻度の体感がまだない(libopus は年数回程度だがペースは変動)
- host pytest では ESP32 ランタイム回帰や音質回帰を捕まえられず、CI 緑が false confidence になる
- 上流リリースノートは bump ごとに 1 回読む必要がある(`dnn/` 追加、配置変更、ライセンス差分)。merge ボタンの瞬間より、その読み合わせは別の場で行うほうがよい
- L1 の検出ロジックは L2 でもそのまま使う。L1 → L2 は「`--apply` ステップを足し、pytest を回し、`gh issue create` を `peter-evans/create-pull-request` に差し替える」だけの加算的変更

**L2 への昇格を検討するタイミング:** L1 の Issue が年 6 件を超えてきて「apply 押して pytest 回す手間を省きたい」と感じたら L2 へ。それまでは人間ペースを維持する。

### なぜ L3(完全自動リリース)を採らないか

仮に L2 を採用したとしても、host pytest 緑だけでは以下を保証できない:

- 音質回帰の不在(440 Hz sine 通過 ≠ コーデック忠実性)
- ESP32 実機ランタイム正常性(host CI は Xtensa を走らせない)
- ファイル抽出 glob の安定性(上流の配置変更で静かに空ヒットになり得る)
- 新規ファイルのライセンス整合(特に 1.5+ の `dnn/` 配下)

人がリリースノートを読む工程を 1 回挟む価値が自動化の手間に十分見合う。

### 最終リリースのタグ付け

上流追随とは独立に、**PCMFlowOpus** 本体のリリースは完全自動: バージョン bump と Arduino Library Manager 用 tag は `tools/bump_version.py`(親 PCMFlow から流用)で生成し、[`.github/workflows/release.yml`](.github/workflows/release.yml)(これも親から移植)が駆動する。`version=` と CHANGELOG の `Unreleased` 節は同時に動く、親と同じ流儀。上流同期コミット(または他の変更)を merge した後、メンテナが `workflow_dispatch` で起動する。

## 10. テスト {#テスト}

親 PCMFlow と同じ規約:

- pytest-embedded + Arduino CLI バックエンド
- 2 プロファイル:`lang-ship:host`(ロジック・大サイズ fixture)と `esp32:esp32:esp32`(実機検証)
- 機能ごとのディレクトリに `<feature>.ino` / `sketch.yaml` / `test_<feature>.py` / `input/` fixture
- アサーション形式は `EXPECT_TRUE` / `EXPECT_EQ` / `EXPECT_NEAR` マクロ、Serial 経由の `TEST done N/M` プロトコル

Opus 固有のテスト設計:

| テストディレクトリ | 検査対象 | 戦略 |
|---|---|---|
| `opus_encoder/` | 出力パケットが要求ビットレート/チャネルの正当な Opus パケットか | 作ったパケットを `OpusDecoder` で復号し、ヘッダ・フレーム形状を確認 |
| `opus_decoder/` | 既知 Opus パケットを復号すると元波形に近づくか | ffmpeg で 440 Hz sine を Opus 化 → C 配列に埋め込み → 復号 → ピーク振幅 ±10 % 以内 |
| `roundtrip/` | encode → decode で元波形に近い波形が得られるか | RMS 誤差が閾値以下。PLC 経路(`decodePacket(nullptr, 0, ...)`)が想定通り無音/快音を返す |
| `external_source/` | `OpusDecoder` が `PCMFlow::setInputSource()` 経由で機能するか | 親 FLAC テストと同じハーネス |

Opus は**ロッシー**なので、PCMFlow `flac_decoder/` のニアリ完全一致ではなく**緩めの許容差**を使う。

## 11. バージョニング

SemVer (`major.minor.patch`)を `library.properties`、`library.json`、`src/pcmflowopus_version.h` で同期管理。PCMFlow のバージョンとも、同梱 libopus のバージョン(`src/external/UPSTREAM.lock` に別記録)とも**独立**。新しい libopus 取り込みは API 変更を伴わない限り `patch` bump。

## 12. 将来対応 {#将来対応}

忘れないようここに集約。v0.1.x には含めない:

- **Ogg/Opus コンテナ**(読み書き)。デスクトップ相互運用(Audacity / mpv / メール添付)用途専用。`xiph/opusfile` + `xiph/ogg`(読み)、`xiph/libopusenc`(書き)を vendor に追加。需要が出たら `PCMFlowOpusOgg.h` の opt-in ヘッダとして実装する。
- **OpusEncoder の repacketization**(複数フレームを 1 パケットへ結合)— `opus_repacketizer` API。非リアルタイム保存向け。VoIP には不要。
- **Bit-exact reference 用 float-point ビルド** — VoIP 優先のため初版対象外。

## 13. ライセンス

PCMFlowOpus: **MIT**([LICENSE](LICENSE))
Vendor している libopus: **BSD-3-Clause**(Xiph.Org)。[src/external/LICENSE_opus.md](src/external/LICENSE_opus.md) 参照。
