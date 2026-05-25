# Examples

> English: [README.md](README.md)

このディレクトリに含まれるスケッチ:

- **EspNowTransceiver** — PCMFlowOpus の主目的ユースケース。1 つのファームウェアにマイク録音→Opus エンコード(20 ms / 24 kbps)→ESP-NOW 同報送信と、ESP-NOW 受信→Opus デコード→PCMFlow 経由でスピーカ出力の両方を実装。同じファームを 2 台に焼くと、ボタン操作で半二重音声トランシーバになる。

  - **対象ボード**: M5Stack Core2(デフォルトプロファイル)。`M5.Mic` + `M5.Speaker` を備える M5Unified 対応ボード(M5AtomEcho / M5StickC Plus 等)でも動作するはず。
  - **操作**: Button A を押している間は送信、離している間は受信。送信していない時は常時受信モード。
  - **フットプリント**: Flash 約 1.4 MB(6 MB 中 21 %)、RAM 約 55 KB(1 %)。Flash のほとんどは libopus 本体。

需要に応じてサンプルは追加していく(片方向の送信専用/受信専用デモ、WebSocket ブリッジでブラウザに繋ぐデモ等)。
