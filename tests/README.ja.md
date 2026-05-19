# Tests

> English: [README.md](README.md)

PCMFlowOpus 自動テストスイート。親 [PCMFlow のテスト規約](https://github.com/tanakamasayuki/PCMFlow/tree/main/tests) を踏襲する:

- [pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/en/latest/) + Arduino CLI バックエンド
- 2 プロファイル:`lang-ship:host`(ロジック検証、大サイズ fixture、CI 高速)と `esp32:esp32:esp32`(実機検証、フットプリント計測)
- 機能ごとのディレクトリに `<feature>.ino` / `sketch.yaml` / `test_<feature>.py` / `input/` fixture
- アサーション形式は `EXPECT_TRUE` / `EXPECT_EQ` / `EXPECT_NEAR` マクロ、Serial 経由の `TEST done N/M` プロトコル

## ステータス

**雛形整備中。** smoke テストは動く形にしている。各コーデックのディレクトリはプレースホルダの sketch を置いてあり、Python 側のアサーションは現状 "TEST start" + "TEST done" のみ。本物のアサーションはエンコーダ/デコーダ実装と同時に入る。

## ディレクトリ構成

- `smoke/` — テンプレート smoke テスト(host プロファイル)。テスト基盤自体の動作確認。
- `opus_encoder/` — `OpusEncoder` の単体テスト(入力 PCM → 妥当な Opus パケット、フォーマット引き継ぎ)
- `opus_decoder/` — `OpusDecoder` の単体テスト(既知 Opus パケット → 想定 PCM、Opus はロッシーなので ±許容差)
- `roundtrip/` — エンドツーエンド encode → decode テスト。各種ビットレート / フレーム長で PCM ≈ PCM' を確認、PLC / FEC 経路も検査
- `external_source/` — `OpusDecoder` を `PCMFlow::setInputSource()` 経由でパイプラインに繋いだ結合テスト
- `tools/gen_test_audio.py` — 各テストの `input/` 配下に WAV + Opus fixture(と埋め込み用 `.h`)を生成。Opus 生成には `ffmpeg` が `PATH` に必要

## Opus 固有のテスト設計

Opus は FLAC と違い**ロッシー**なので許容差を緩める:

| テストディレクトリ | 検査内容 | 許容差 |
|---|---|---|
| `opus_decoder/` | 440 Hz sine 復号波形のピーク振幅 | エンコード振幅の ±10 % |
| `opus_decoder/` | デコーダヘッダの rate / channels / bits | 完全一致 |
| `opus_encoder/` | 出力パケットを復号した結果の rate / channels | ヘッダ往復で完全一致 |
| `roundtrip/` | encode → decode と元 sine の RMS 誤差 | ビットレートごとの閾値以下 |
| `roundtrip/` | `decodePacket(nullptr, 0, ...)` PLC 経路 | 想定フレーム数、出力は非一様だが有限 |

ground-truth は ffmpeg 生成の Opus fixture を使う。常に自前と独立した参照エンコーダがある状態にしておく。

## 実行

```sh
# host(デフォルト)
uv run --env-file .env pytest

# 実機 ESP32
uv run --env-file .env pytest --profile=esp32

# 個別テスト
uv run --env-file .env pytest opus_decoder/
```

前提(uv / arduino-cli / 各ボードコア)は[親 PCMFlow tests の README](https://github.com/tanakamasayuki/PCMFlow/tree/main/tests#prerequisites) を参照。
