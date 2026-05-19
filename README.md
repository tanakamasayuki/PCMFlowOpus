# PCMFlowOpus

> 日本語版: [README.ja.md](README.ja.md)

Optional **Opus** codec add-on for [PCMFlow](https://github.com/tanakamasayuki/PCMFlow), aimed at **real-time two-way voice over packet radio / network** — VoIP, ESP-NOW transceivers, WebSocket / UDP voice links.

Wraps the upstream Xiph.Org `libopus` reference codec and exposes it through PCMFlow's `PCMSource` / `PCMSink` interfaces. Split off from the parent library because the vendored codec tree is large and upstream releases land frequently.

See [SPEC.md](SPEC.md) for the full specification.

---

## Status

**Pre-release / scaffolding.** Repository layout, documentation, and the vendor/release plan are in place. The encoder / decoder implementations and the upstream-sync tooling are not yet wired in. See [CHANGELOG.md](CHANGELOG.md).

---

## What's inside

| Class | Direction | Carrier | Interface |
|-------|-----------|---------|-----------|
| `OpusEncoder` | PCM → Opus packet | raw packets (RTP / ESP-NOW / UDP / WebSocket) | `PCMSink` |
| `OpusDecoder` | Opus packet → PCM | raw packets | `PCMSource` (+ PLC / FEC) |

Both work on **one packet at a time** (~40–200 byte typical for voice). Container formats (Ogg/Opus `.opus` files) are intentionally **out of scope for the initial release** — see [SPEC §"Deferred features"](SPEC.md#deferred-features). For local file playback, MP3/FLAC via parent PCMFlow already covers the common case.

---

## Headline use case — ESP-NOW transceiver

ESP-NOW carries up to 250 byte payloads. A 20 ms Opus voice packet at 24 kbps is ~60 byte → comfortably fits, with room for headers. Raw 16 kHz mono 16-bit PCM at the same 20 ms is 640 byte — Opus compresses **10–15×**, so half-duplex (or broadcast) voice between multiple ESP32 nodes becomes practical.

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

// Called from the ESP-NOW recv ISR / task: feed packet into decoder
void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
    dec.decodePacket(data, len, /*pcm_out=*/nullptr, /*max_frames=*/0);
    // PCMFlow will pump() and play the decoded PCM on the I2S/DAC side
}
```

End-to-end transceiver sketch (mic ↔ ESP-NOW ↔ DAC) lives in [examples/EspNowTransceiver/](examples/EspNowTransceiver/) (placeholder for now).

---

## Dependencies

- **[PCMFlow](https://github.com/tanakamasayuki/PCMFlow)** — provides `PCMSource`, `PCMSink`, `ByteStream`, `ByteSink`, and the playback pipeline. Declared via `depends=PCMFlow` in `library.properties`.
- **libopus** — vendored verbatim under [src/external/opus/](src/external/) (see below). No other dependencies in the initial release.

---

## Target platforms

Same posture as PCMFlow: `architectures=*` in `library.properties`, with the same realistic constraints — 32-bit MCU, tens of KB of SRAM, ~100 KB free flash.

Practical targets: ESP32 / ESP32-S3 / ESP32-C3 / ESP32-C6 / ESP32-P4, RP2040 / RP2350, Teensy 4.x, STM32 F4+, nRF52.

AVR (Uno / Mega / Nano) is **not supported**. SAMD21-class is unlikely to fit.

Provisional footprint (encoder + decoder, fixed-point): **Flash ~150–180 KB, RAM ~35–50 KB**. Decoder-only builds drop the flash side substantially when the encoder is unreferenced and the linker discards it. Detailed measurements live in [SPEC.md §6](SPEC.md).

---

## Vendored upstream library

PCMFlowOpus vendors a single upstream project into [src/external/](src/external/):

| Project | Upstream | License | Role |
|---------|----------|---------|------|
| **opus** | [xiph/opus](https://github.com/xiph/opus) | BSD-3-Clause | Reference Opus codec (encoder + decoder) |

Vendored files are **kept verbatim** (no in-tree edits). Build-time configuration that would normally be produced by `./configure` is supplied by a hand-written [src/external/opus_config.h](src/external/opus_config.h) (fixed-point, MCU-friendly defaults).

License credits: [src/external/LICENSE_opus.md](src/external/LICENSE_opus.md)
Pinned versions: [src/external/UPSTREAM.lock](src/external/UPSTREAM.lock)

### Updating upstream

Vendored sources are kept in sync with **Xiph.Org GitHub tags** (the source of the official `opus-codec.org` tarballs). The intended workflow:

```sh
# Check for newer tags upstream (no changes made)
python tools/sync_opus.py --check-upstream

# Apply: re-fetch tarball, overwrite src/external/opus/, rewrite UPSTREAM.lock
python tools/sync_opus.py --apply
```

> The `sync_opus.py` script and its companion CI workflow are **not implemented yet**. The automation level (L1 = notify-only / L2 = auto-PR) will be decided before the first release — see [SPEC.md §"Release workflow"](SPEC.md#release-workflow) for the design.

---

## Tests

```sh
cd tests
uv run --env-file .env pytest                  # host (default)
uv run --env-file .env pytest --profile=esp32  # real ESP32 hardware
```

See [tests/README.md](tests/README.md). Same conventions as the parent PCMFlow test suite (pytest-embedded + Arduino CLI, `lang-ship:host` + `esp32:esp32:esp32` profiles).

---

## License

PCMFlowOpus itself: **MIT** ([LICENSE](LICENSE)).

Vendored libopus: **BSD-3-Clause** (Xiph.Org). Full attribution and license text: [src/external/LICENSE_opus.md](src/external/LICENSE_opus.md).
