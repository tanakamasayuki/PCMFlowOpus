# Tests

> 日本語版: [README.ja.md](README.ja.md)

Automated test suite for PCMFlowOpus. Mirrors the conventions of the parent [PCMFlow test suite](https://github.com/tanakamasayuki/PCMFlow/tree/main/tests):

- [pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/en/latest/) + Arduino CLI backend.
- Two profiles: `lang-ship:host` (logic verification, large fixtures, fast CI) and `esp32:esp32:esp32` (real hardware verification, footprint measurement).
- Per-feature subdirectory containing `<feature>.ino`, `sketch.yaml`, `test_<feature>.py`, and an `input/` directory of fixtures.
- Assertions use the `EXPECT_TRUE` / `EXPECT_EQ` / `EXPECT_NEAR` macros and the `TEST done N/M` Serial protocol.

## Status

**Scaffolding.** Smoke test is in place; per-codec test directories contain placeholder sketches that the Python side currently asserts only for "TEST start" + "TEST done". The real assertions land when the encoder / decoder implementations do.

## Directory layout

- `smoke/` — Template smoke test (host profile). Verifies the test infrastructure itself.
- `opus_encoder/` — Unit tests for `OpusEncoder` (input PCM → valid Opus packet, format parameters carry through).
- `opus_decoder/` — Unit tests for `OpusDecoder` (known Opus packet → expected PCM, ±tolerance because Opus is lossy).
- `roundtrip/` — End-to-end encode → decode tests. Verifies PCM ≈ PCM' under various bitrate / frame-duration settings, exercises PLC and FEC paths.
- `external_source/` — Integration test for `OpusDecoder` plugged into PCMFlow's `setInputSource()` pipeline.
- `tools/gen_test_audio.py` — Generates WAV + Opus fixtures (and embed-able `.h`) under each test's `input/` directory. Opus generation requires `ffmpeg` on `PATH`.

## Opus-specific test design

Opus is **lossy**, unlike FLAC. Test tolerances are correspondingly loose:

| Test dir | What we check | Tolerance |
|----------|---------------|-----------|
| `opus_decoder/` | Peak amplitude of a decoded 440 Hz sine | ±10 % of the encoded amplitude |
| `opus_decoder/` | Sample rate / channels / bits in the decoder header | exact match |
| `opus_encoder/` | Packet decodes back to the right rate / channels | exact match on header round-trip |
| `roundtrip/` | RMS error of encode → decode vs. original sine | below a per-bitrate threshold |
| `roundtrip/` | `decodePacket(nullptr, 0, ...)` PLC path | returns the expected frame count, output is non-uniform but finite |

ffmpeg is used to generate ground-truth Opus fixtures so we always have a reference encoder independent of our own.

## Running

```sh
# host (default)
uv run --env-file .env pytest

# real ESP32
uv run --env-file .env pytest --profile=esp32

# single test
uv run --env-file .env pytest opus_decoder/
```

See the [parent PCMFlow tests README](https://github.com/tanakamasayuki/PCMFlow/tree/main/tests#prerequisites) for prerequisites (uv, arduino-cli, board cores).
