#!/usr/bin/env python3
"""Generate test audio fixtures for PCMFlowOpus tests.

Mirrors the role of `tools/gen_test_audio.py` in the parent PCMFlow
repo: produces WAV + Opus fixtures (and embed-able `.h` files of C
arrays) under each test's `input/` directory.

STATUS: scaffolding stub. Not implemented yet — will be filled in
once the encoder/decoder lands and we know exactly what fixtures the
test sketches consume. The intended outputs are:

  tests/opus_decoder/input/
    opus_fixtures.h           — sine_440_8k_mono_24kbps.opus packets (C array)
    sine_440_8k_mono_24kbps.opus
    sine_440_16k_stereo_32kbps.opus
    ...

Generation is driven by ffmpeg (must be on PATH). ffmpeg's Opus
encoder is independent of our own libopus build, which is exactly
what we want for golden-fixture purposes.
"""

from __future__ import annotations

import sys


def main() -> int:
    print("gen_test_audio.py — not implemented yet (scaffolding stub).",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
