# PCMFlowOpus Specification

> 日本語版: [SPEC.ja.md](SPEC.ja.md)

## 1. Scope

**PCMFlowOpus** is an optional Opus codec add-on for [PCMFlow](https://github.com/tanakamasayuki/PCMFlow), focused on **real-time two-way voice / packet-radio use cases** (VoIP, ESP-NOW transceivers, WebSocket / UDP voice links).

Responsibility:

- **Encode** 16-bit PCM frames (typically 20 ms at 8/16/24/48 kHz, mono or stereo) into raw Opus packets.
- **Decode** raw Opus packets back into 16-bit PCM frames, with packet loss concealment (PLC) and forward error correction (FEC).

Both sides operate on **one Opus packet at a time** (~40–200 byte typical, ≤ 1275 byte upper bound). The byte stream's framing is the caller's responsibility — natural carriers are ESP-NOW frames, UDP datagrams, RTP payloads, WebSocket binary messages, etc.

The library plugs into PCMFlow's pipeline at the interface level only. It does not reimplement PCMFlow internals (ring buffer, format conversion, gain, output handoff are owned by PCMFlow).

## 2. Non-goals

- **Ogg/Opus container** (`.opus` / `.ogg` files) — out of scope for the initial release. See §"Deferred features".
- **Audio file playback / device output** — owned by PCMFlow.
- **`Stream` / file system / network adapters** — owned by PCMFlow's `ByteStream` / `ByteSink`.
- **Jitter buffering / RTP parsing / network transport** — caller's responsibility. PCMFlowOpus is a codec, not a stack.
- **Reimplementing the codec** — uses the upstream Xiph.Org reference implementation (`xiph/opus`) verbatim.

## 3. Primary use case: ESP-NOW transceiver

The motivating use case. ESP-NOW carries up to 250 byte payloads, which comfortably fits a 20 ms Opus voice packet:

| Bitrate | Payload per 20 ms frame |
|---------|-------------------------|
| 16 kbps (narrowband voice)  | ~40 B  |
| 24 kbps (standard voice)    | ~60 B  |
| 32 kbps (wideband voice)    | ~80 B  |
| 64 kbps (music-grade voice) | ~160 B |

Compare to raw 16 kHz mono 16-bit PCM: 640 B per 20 ms. Opus compresses **10–15×**, so multiple-node broadcast over ESP-NOW becomes practical.

An end-to-end transceiver sketch lives in [examples/EspNowTransceiver/](examples/EspNowTransceiver/) (mic → Opus encode → ESP-NOW broadcast / ESP-NOW receive → Opus decode → I2S DAC, all in one sketch).

## 4. Public API (planned)

Two classes; both work on **one Opus packet at a time**.

### 4.1 `OpusEncoder` — implements `PCMSink`

Accepts 16-bit interleaved PCM and emits Opus packets.

```cpp
OpusEncoder enc;
enc.begin({16000, 1, 16},                    // input PCM: rate, channels, bits
          OpusApplication::Voip,             // Voip / Audio / LowDelay
          /*bitrate_bps=*/24000);

uint8_t pkt[400];
const size_t n = enc.encodeFrame(pcm20ms,    // exactly one Opus frame's worth (e.g. 320 samples @ 16 kHz/20 ms)
                                 pkt, sizeof(pkt));
// send pkt[0..n) over ESP-NOW / UDP / WebSocket
```

Options exposed:
- `setBitrate(bps)` — runtime change OK
- `setComplexity(0..10)` — CPU vs quality
- `setFrameDuration(ms)` — 2.5 / 5 / 10 / 20 / 40 / 60
- `setDtx(bool)` — discontinuous transmission (silence suppression)
- `setInbandFec(bool)` — pair with decoder's FEC
- `setPacketLossPerc(0..100)` — encoder side, hints to inband-FEC how aggressive to be

### 4.2 `OpusDecoder` — implements `PCMSource`

Accepts Opus packets and emits 16-bit interleaved PCM.

```cpp
OpusDecoder dec;
dec.begin({16000, 1, 16});                   // output PCM: rate, channels, bits

int16_t pcm[320];

// normal path
size_t frames = dec.decodePacket(pkt, n, pcm, 320);

// packet lost — synthesize 20 ms via SILK PLC
frames = dec.decodePacket(nullptr, 0, pcm, 320);

// FEC redundancy — recover the previous packet from the current one's in-band FEC
frames = dec.decodePacketFec(pkt, n, pcm, 320);
```

Both `OpusEncoder::format()` and `OpusDecoder::format()` describe the PCM side; the network/packet side is opaque bytes.

### 4.3 PCMFlow pipeline integration

`OpusDecoder` implements `PCMSource`, so it plugs into PCMFlow's ring buffer / format converter / output device chain via `PCMFlow::setInputSource(dec)`. The caller feeds packets into the decoder (`decodePacket(...)`); PCMFlow consumes the resulting PCM through `pump()` / `readFrames()` as usual.

`OpusEncoder` implements `PCMSink`. Source PCM (e.g. from a mic recording task) is pushed via `writeFrames(...)`; the encoder buffers exactly one Opus frame worth of samples internally, then emits a packet to the caller-supplied callback or `ByteSink`.

Detailed signatures and error enums are finalized in the headers under [src/](src/) once implemented.

## 5. PCM I/O format

- Sample rate: **8000 / 12000 / 16000 / 24000 / 48000 Hz** (Opus's native rates).
- Bit depth: **16-bit signed**.
- Channels: **1 (mono) or 2 (stereo)**.
- Frame duration: caller chooses 2.5 / 5 / 10 / 20 / 40 / 60 ms; **20 ms is the VoIP default**.

If the application needs a different output sample rate (e.g. ESP32-S3 I2S DAC running at 44.1 kHz), PCMFlow's resampler handles the conversion. PCMFlowOpus itself only deals in Opus-native rates.

## 6. Memory & footprint targets

Provisional, to be measured once integrated:

| Item | Target |
|------|--------|
| Flash (encoder + decoder, fixed-point) | ≤ 180 KB |
| Flash (decoder only, link-time discarded encoder) | ≤ 100 KB |
| RAM, persistent encoder state | ~20–30 KB |
| RAM, persistent decoder state | ~15–20 KB |
| Per-call scratch | included in above |

Build configuration (set via the hand-written [src/external/opus_config.h](src/external/opus_config.h) so no `./configure` is needed):

- `FIXED_POINT=1` (no FPU dependence; smaller code)
- `OPUS_BUILD=1`
- `DISABLE_FLOAT_API=1`
- `VAR_ARRAYS=1` on toolchains with VLA (GCC), else `USE_ALLOCA=1`
- `HAVE_LRINTF=1` where the toolchain provides it
- `OPUS_ARM_MAY_HAVE_NEON=0` (off by default; revisit per-target)

These flags live in `src/external/opus_config.h` and are **not** rewritten by the sync script.

## 7. Repository layout

```
PCMFlowOpus/
├─ README.md / README.ja.md
├─ SPEC.md   / SPEC.ja.md
├─ CHANGELOG.md
├─ LICENSE
├─ library.properties        # Arduino IDE
├─ library.json              # PlatformIO
├─ keywords.txt              # Arduino IDE syntax highlight
├─ src/
│  ├─ PCMFlowOpus.h          # umbrella header
│  ├─ OpusEncoder.h/.cpp     # PCMSink wrapper around opus_encoder
│  ├─ OpusDecoder.h/.cpp     # PCMSource wrapper around opus_decoder
│  ├─ pcmflowopus_version.h  # auto-generated by tools/bump_version.py
│  └─ external/
│     ├─ LICENSE_opus.md     # credits + BSD-3-Clause text
│     ├─ UPSTREAM.lock       # pinned tag + SHA256 for xiph/opus
│     ├─ opus_config.h       # hand-written, replaces ./configure output
│     └─ opus/               # vendored verbatim subset of xiph/opus
├─ examples/
│  └─ EspNowTransceiver/     # showcase sketch (mic↔ESP-NOW↔DAC)
├─ tests/
│  ├─ README.md / README.ja.md
│  ├─ conftest.py
│  ├─ pyproject.toml
│  ├─ smoke/
│  ├─ opus_encoder/
│  ├─ opus_decoder/
│  ├─ roundtrip/             # encode → decode → compare against original sine
│  ├─ external_source/       # PCMFlow::setInputSource integration
│  └─ tools/gen_test_audio.py
├─ tools/
│  ├─ bump_version.py        # mirrors parent PCMFlow's tooling
│  └─ sync_opus.py           # not implemented yet — see §"Release workflow"
└─ .github/
   └─ workflows/             # not implemented yet — see §"Release workflow"
```

## 8. Vendored upstream {#vendored-upstream}

Only one upstream project is vendored in the initial release:

| Project | Upstream | License | Role |
|---------|----------|---------|------|
| **opus** | [xiph/opus](https://github.com/xiph/opus) | BSD-3-Clause | Reference Opus codec (encoder + decoder) |

Vendoring rules:

1. Files are pulled from GitHub tag tarballs (`codeload.github.com/xiph/opus/tar.gz/refs/tags/<tag>`). This matches the official `opus-codec.org` release tarballs but is mechanically scriptable.
2. Only files needed for an MCU encode/decode build are kept. Excluded: `tests/`, `doc/`, `autotools/`, `configure*`, `Makefile.am`, `win32/`, `.github/`, `silk/float/`, `dnn/` (the ML-assisted enhancement code added in 1.5+; not used in our build config).
3. Vendored files are **never edited** in-tree. Upstream license headers and revision banners remain intact. Configuration that `./configure` would normally generate is supplied by the hand-written `src/external/opus_config.h`.
4. `src/external/UPSTREAM.lock` records the pinned tag and SHA256, so sync runs are reproducible and CI can assert "lockfile matches tree".

License: BSD-3-Clause. Full credits and license text: [src/external/LICENSE_opus.md](src/external/LICENSE_opus.md).

## 9. Release workflow {#release-workflow}

Two automation levels are under consideration. **The decision is deferred** until just before the first release.

### L1 — notify only

A weekly GitHub Actions job checks `git ls-remote --tags` for `xiph/opus` against `UPSTREAM.lock` and opens an Issue if a new tag exists. A maintainer runs `python tools/sync_opus.py --apply` locally, tests, commits, and tags.

Pros: minimal CI surface; every step is human-reviewed.
Cons: every upstream bump is manual labor.

### L2 — auto-PR

Same detection, plus the workflow runs `sync_opus.py --apply`, host pytest, and `arduino-cli compile --fqbn esp32:esp32:esp32` for the test sketches; if all green, opens a pull request. A maintainer reviews the diff and merges. Tag/release is then driven by the existing release workflow (mirrored from parent PCMFlow's `.github/workflows/release.yml`).

Pros: drift is caught weekly; no human step until the merge button.
Cons: host pytest cannot catch ESP32-runtime regressions or codec-quality regressions; reviewer must still read upstream release notes.

### Why not L3 (full auto-release)

Host pytest passing does not guarantee absence of audio-quality regression, ESP32 runtime correctness, stability of file-extraction globs against upstream layout changes, or license consistency of newly-added upstream files (especially `dnn/` in 1.5+). A human reading the upstream release notes once per bump is cheap insurance.

### Final release tagging

In all modes, version bumps and the Arduino Library Manager tag are produced by `tools/bump_version.py` (copied verbatim from parent PCMFlow), driven by a `release.yml` patterned on the parent. PCMFlowOpus's `version=` and `Unreleased` CHANGELOG section move together, identically to PCMFlow.

`sync_opus.py` and the upstream-sync workflow are **not implemented yet**.

## 10. Testing {#testing}

Same conventions as parent PCMFlow:

- pytest-embedded + Arduino CLI backend.
- Two profiles: `lang-ship:host` (logic, large fixtures) and `esp32:esp32:esp32` (real hardware verification).
- Per-feature test directory with `<feature>.ino`, `sketch.yaml`, `test_<feature>.py`, and `input/` fixtures.
- Assertions use the `EXPECT_TRUE / EXPECT_EQ / EXPECT_NEAR` macro family and the `TEST done N/M` Serial protocol.

Opus-specific test design:

| Test dir | Subject | Strategy |
|----------|---------|----------|
| `opus_encoder/` | encoder output is a valid Opus packet at the requested bitrate / channel layout | decode the produced packet with `OpusDecoder` and check format header |
| `opus_decoder/` | known Opus packets decode to roughly the original waveform | ffmpeg-generated fixture: encode a 440 Hz sine, embed as C array, decode, assert peak amplitude within ±10 % |
| `roundtrip/` | encode → decode produces a waveform close to the input | RMS error stays below threshold; PLC path (`decodePacket(nullptr, 0, ...)`) returns the expected silent/comfort-noise frame count |
| `external_source/` | `OpusDecoder` works as a `PCMSource` plugged into `PCMFlow::setInputSource()` | same harness as the parent FLAC test |

Opus is **lossy**, so tolerances are generous compared to the near-exact match used in PCMFlow's `flac_decoder/` tests.

## 11. Versioning

SemVer (`major.minor.patch`) maintained in `library.properties`, `library.json`, and `src/pcmflowopus_version.h`. **Independent of** the PCMFlow version and **independent of** the bundled libopus version (which is recorded separately in `src/external/UPSTREAM.lock`). A new libopus pickup is normally a `patch` bump unless it changes the public API surface.

## 12. Deferred features {#deferred-features}

Captured here so they aren't lost; not in v0.1.x:

- **Ogg/Opus container** (read and write). Useful only for desktop interop (Audacity / mpv / email attachment). Would add `xiph/opusfile` + `xiph/ogg` (read) and `xiph/libopusenc` (write) to the vendor set. Add as `PCMFlowOpusOgg.h` opt-in header when demand appears.
- **OpusEncoder repacketization** (combine multiple frames into a single packet) — `opus_repacketizer` API. Useful for non-realtime archival; not for VoIP.
- **Stereo bit-exact reference build** — float-point path. Out of scope while VoIP is the priority.

## 13. License

PCMFlowOpus: **MIT** ([LICENSE](LICENSE)).
Vendored libopus: **BSD-3-Clause** (Xiph.Org). See [src/external/LICENSE_opus.md](src/external/LICENSE_opus.md).
