# EspNowTransceiver

> English: [README.md](README.md)

ESP-NOWの相手と符号化した音声を交換するサンプルです。

## 準備と実行

1. [サンプル入門ガイド](../../docs/GUIDE_EXAMPLES.ja.md)で導入条件と必要な機器を確認します。対応するボード・外部ライブラリはルートREADMEを参照します。
2. [EspNowTransceiver.ino](EspNowTransceiver.ino)を開き、実際のボードとポートを選びます。次のincludeで使う依存機能も準備してください。

   `<M5Unified.h>`, `<PCMFlow.h>`, `<PCMFlowDeviceM5.h>`, `<PCMFlowOpus.h>`, `<esp_now.h>`, `<esp_wifi.h>`, `<WiFi.h>`

3. 書き込んだらシリアルモニタを115200 baudへ合わせます。起動ログとソースの操作条件を確認します。

## 動作の流れ

ESP-NOWの相手と符号化した音声を交換する例です。詳細なAPIとデータの扱いは次の項目で確認します。

以下はソース内で最初に現れるAPIの順です。コールバック登録の位置は、実際の呼び出し順序とは異なります。

1. `mic.begin(...)` — 前提となる機器を準備してから初期化し、結果を返すAPIでは成否を確認します。
2. `audio.setInputSource(...)` — パイプラインが読む間ずっと有効なSourceを接続します。
3. `audio.setOutputFormat(...)` — 入力形式だけでなく、次の段階が受け取れる形式を選びます。
4. `audio.setBufferFrames(...)` — フレーム数で容量を決め、メモリと増える待ち時間の両方を考慮します。
5. `mic.readFrames(...)` — 実際に返ったフレーム数を使います。Byte数にはチャンネル数とサンプル幅も関係します。
6. `enc.encodeFrame(...)` — 対応する1Frameを符号化し、実際の符号化長を使います。
7. `dec.decodePacket(...)` — 実際のPacket長で復号し、Errorや出力量を確認します。
8. `audio.pump()` — 復号／変換を進め、得られたPCM フレームだけを取り出します。
9. `player.writeFrames(...)` — 受理量を確認し、残りを保持します。フレーム数とByte数は異なります。

変更する前に確認する定数: `kRate`, `kChannels`, `kFrame`, `kMaxPlayFrames`, `kBitrate`, `kRxRingSlots`, `kRxRingPktMax`。GPIOやチャンネル、容量などは用途に合わせます。

## APIの使い方と注意点

例はM5Unifiedのマイク／スピーカー、PCMFlow、PCMFlowDeviceを別々の段階として使います。周波数・チャンネル・フレーム時間を合わせます。圧縮Packet長は変動します。

EncoderのFECを有効にしただけでReceiverが欠落を回復するわけではありません。 Codecのbegin／encode／decodeを確認し、PCMのframe数と圧縮後のbyte数を区別します。送受信には同じESP-NOW チャンネルの2台を使い、ソースにある相手／Broadcastの設定を合わせます。

Push-to-talkの例では指定ボタンを押して送信します。録音・符号化・無線配送・復号・バッファ再生は別々に失敗し得るので、送信成功だけで音が出ると判断しません。Wi-Fiの受信コールバックを短くし、後の処理用にPacketをコピー／キューへ渡します。

Loopbackは1台でCodecを確認できますが、無線の配送は検証しません。

## 操作と出力の見方

この例が確認するボタン状態: `BtnA.isPressed()`。押下・解除・保持中を区別します。

出力書式の一部は次のとおりです。printfの書式記号は実行時の値に変わります。表示順や数値は実行条件に応じて変わるので、その行へ進む条件と併せて確認します。

```text
Ready. MAC=%s
```

## 確認と問題の切り分け

ESP-NOWの相手と符号化した音声を交換することを出力から確認します。まず例のままの入力で試し、その後に設定やデータ源を1つずつ変えます。

| 症状 | 最初に確認すること |
|---|---|
| 出力がない | ボード／依存ライブラリ、初期化結果、表示先とシリアル速度 |
| 開始するが途中で止まる | 待機条件、バッファの空き、相手や入力の準備、継続呼び出し |
| 値が不自然・不安定 | 長さと単位、形式、寿命、共有状態、部分的な処理結果 |

共通の考え方は[入門ガイド](../../docs/GUIDE_EXAMPLES.ja.md)、別の例は[サンプル一覧](../README.ja.md)へ進んでください。
