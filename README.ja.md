# PCMFlowOpus

> English: [README.md](README.md)

[PCMFlow](https://github.com/tanakamasayuki/PCMFlow) 用のオプション **Opus** コーデックアドオン。**リアルタイム双方向音声(VoIP / ESP-NOW トランシーバ / WebSocket / UDP 音声リンク)** にフォーカスしている。

Xiph.Org の `libopus` リファレンス実装を vendor し、PCMFlow の `PCMSource` / `PCMSink` インタフェース経由で接続する。親 PCMFlow から分離している理由は、libopus の vendor ツリーが大きく上流の更新頻度も高いため、独立リポで追随する方が運用しやすいから。

詳細は [SPEC.ja.md](SPEC.ja.md) を参照。

---

## ステータス

**プレリリース / 雛形整備中。** リポジトリ構成・ドキュメント・vendor / リリース計画は整っている。エンコーダ / デコーダ本体と上流同期スクリプトは未実装。[CHANGELOG.md](CHANGELOG.md) 参照。

---

## 構成

| クラス | 方向 | 担い手 | インタフェース |
|--------|------|--------|----------------|
| `OpusEncoder` | PCM → Opus パケット | 生パケット(RTP / ESP-NOW / UDP / WebSocket) | `PCMSink` |
| `OpusDecoder` | Opus パケット → PCM | 生パケット | `PCMSource`(+ PLC / FEC) |

両方とも **1 パケット単位**(音声で通常 40〜200 byte)で動作。ファイル系の Ogg/Opus(`.opus`)は**初版では対象外** — [SPEC §「将来対応」](SPEC.ja.md#将来対応) 参照。ローカルファイル再生は親 PCMFlow の MP3 / FLAC で足りる。

---

## 主目的ユースケース — ESP-NOW トランシーバ

ESP-NOW は 1 パケット最大 250 byte。Opus 20 ms / 24 kbps なら ~60 byte、ヘッダ含めて十分収まる。同じ 20 ms の生 16 kHz mono 16-bit PCM は 640 byte なので **10〜15 倍圧縮**。複数ノード間の半二重音声や同報通信が実用域に入る。

```cpp
#include <PCMFlow.h>
#include <PCMFlowOpus.h>
#include <esp_now.h>

OpusEncoder enc;
OpusDecoder dec;
PCMFlow audio;

void setup() {
    audio.setOutputFormat({16000, 1, 16});
    audio.setInputSource(dec);

    dec.begin({16000, 1, 16});
    enc.begin({16000, 1, 16}, OpusApplication::Voip, /*bitrate=*/24000);

    esp_now_init();
    esp_now_register_recv_cb(onEspNowRecv);
}

// ESP-NOW 受信コールバック: パケットをデコーダへ
void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
    dec.decodePacket(data, len, /*pcm_out=*/nullptr, /*max_frames=*/0);
    // PCMFlow の pump() が PCM を取り出して I2S/DAC へ流す
}
```

エンドツーエンドのトランシーバスケッチ(マイク↔ESP-NOW↔DAC)は [examples/EspNowTransceiver/](examples/EspNowTransceiver/) に同梱予定。

---

## 依存

- **[PCMFlow](https://github.com/tanakamasayuki/PCMFlow)** — `PCMSource` / `PCMSink` / `ByteStream` / `ByteSink` / 再生パイプラインを提供。`library.properties` の `depends=PCMFlow` で宣言。
- **libopus** — [src/external/opus/](src/external/) に verbatim 取り込み(下記)。初版ではこれ以外の外部依存なし。

---

## 対応プラットフォーム

親 PCMFlow と同じ:`library.properties` は `architectures=*`、ただし実用ターゲットは 32-bit MCU + SRAM 数十 KB + Flash ~100 KB 程度。

実用例: ESP32 / ESP32-S3 / ESP32-C3 / ESP32-C6 / ESP32-P4、RP2040 / RP2350、Teensy 4.x、STM32 F4 以上、nRF52。

AVR(Uno / Mega / Nano)は**非対応**。SAMD21 級も実用は厳しい。

暫定フットプリント目安(エンコーダ + デコーダ、fixed-point): **Flash ~150–180 KB / RAM ~35–50 KB**。デコーダのみのビルドではエンコーダ側がリンク時に落ちて flash がかなり減る。詳細は [SPEC.ja.md §6](SPEC.ja.md)。

---

## Vendor している上流ライブラリ

PCMFlowOpus は上流プロジェクトを 1 つだけ [src/external/](src/external/) に vendor している:

| プロジェクト | 上流 | ライセンス | 役割 |
|--------------|------|------------|------|
| **opus** | [xiph/opus](https://github.com/xiph/opus) | BSD-3-Clause | Opus コーデック本体(エンコーダ + デコーダ) |

Vendor したファイルは**無改変**で保持(上流のライセンスヘッダ・バージョン銘を残すため)。本来 `./configure` が生成する `config.h` 相当は、手書きの [src/external/opus_config.h](src/external/opus_config.h) で供給する(fixed-point 既定、MCU 向け)。

ライセンス表記: [src/external/LICENSE_opus.md](src/external/LICENSE_opus.md)
固定バージョン: [src/external/UPSTREAM.lock](src/external/UPSTREAM.lock)

### 上流の更新

Vendor したソースは **Xiph.Org の GitHub tag**(`opus-codec.org` の公式 tarball と同一)を基準に同期する。自動化レベルは **L1 — 通知のみ**:週次の GitHub Actions ([`.github/workflows/upstream-check.yml`](.github/workflows/upstream-check.yml)) が新 tag を検出して Issue を起票する。実際の sync はメンテナがローカルで実行:

```sh
# 現在 / 最新の tag とリリースノート URL を表示(変更はしない)
python tools/sync_opus.py --check-upstream

# 反映: tarball を再取得し、src/external/opus/ を置き換え、UPSTREAM.lock を更新
python tools/sync_opus.py --apply                  # 最新の非 pre-release tag
python tools/sync_opus.py --apply --tag v1.6.1     # 特定 tag を指定
```

L1 を採用した理由(自動 PR / 自動リリースを採らない理由)は [SPEC.ja.md「リリースフロー」節](SPEC.ja.md#リリースフロー) 参照。

---

## テスト

```sh
cd tests
uv run --env-file .env pytest                  # host (デフォルト)
uv run --env-file .env pytest --profile=esp32  # 実機 ESP32
```

詳細は [tests/README.ja.md](tests/README.ja.md)。親 PCMFlow と同じ規約(pytest-embedded + Arduino CLI、`lang-ship:host` + `esp32:esp32:esp32` プロファイル)。

---

## ライセンス

PCMFlowOpus 本体: **MIT** ([LICENSE](LICENSE))。

Vendor している libopus: **BSD-3-Clause**(Xiph.Org)。クレジットと全文は [src/external/LICENSE_opus.md](src/external/LICENSE_opus.md)。
