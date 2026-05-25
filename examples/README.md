# Examples

> 日本語版: [README.ja.md](README.ja.md)

Sketches under this directory:

- **EspNowTransceiver** — Headline use case for PCMFlowOpus. A single firmware that records from the mic, Opus-encodes 20 ms voice frames at 24 kbps, and broadcasts them over ESP-NOW; on the receive side it decodes incoming Opus packets and pipes them through PCMFlow to the speaker. Two boards flashed with the same firmware can half-duplex talk to each other.

  - **Target**: M5Stack Core2 (default profile). Any M5Unified-compatible board with `M5.Mic` + `M5.Speaker` should work.
  - **Operation**: hold Button A to talk; release to listen. Listens continuously while not transmitting.
  - **Footprint**: ~1.4 MB flash (21 % of 6 MB), ~55 KB RAM (1 %). The bulk of flash is libopus.

More examples can be added as needed (single-direction sender / receiver demos, WebSocket bridge to a browser, etc.).
