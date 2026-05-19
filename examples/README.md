# Examples

Sketches under this directory:

- **EspNowTransceiver** — Headline use case for PCMFlowOpus. A single firmware that records from the mic, Opus-encodes 20 ms voice frames, and broadcasts them over ESP-NOW; on the receive side it decodes incoming Opus packets and pipes them through PCMFlow to an I2S DAC. Two boards flashed with the same firmware can half-duplex talk to each other.

  **Status: placeholder.** Will be filled in once `OpusEncoder` / `OpusDecoder` are functional.

More examples will be added as the codec and surrounding tooling come online (single-direction sender / receiver demos, WebSocket bridge to a browser, etc.).
