# Vendored Opus codec — credits and license

PCMFlowOpus vendors **libopus**, the reference Opus implementation from
the **Xiph.Org Foundation**, into [`src/external/opus/`](opus/). We are
grateful to the Opus authors and Xiph.Org for releasing this work under
permissive terms.

## Acknowledgement

**libopus** — Opus reference encoder and decoder. Developed under the
joint IETF / Xiph.Org codec working group. The full author list is
preserved upstream at <https://github.com/xiph/opus/blob/main/AUTHORS>
and in the `AUTHORS` file copied into `src/external/opus/`.

Neither the Opus authors nor Xiph.Org are affiliated with PCMFlowOpus.
Issues with the codec itself should be reported upstream to
<https://github.com/xiph/opus>, not to PCMFlowOpus.

## Vendored files

| Path in this directory | Upstream project | Upstream license | Version note |
|------------------------|------------------|------------------|--------------|
| `opus/` | [xiph/opus](https://github.com/xiph/opus) | BSD-3-Clause | Pulled verbatim from upstream tag (see [`UPSTREAM.lock`](UPSTREAM.lock)). |

Vendored files are **kept unmodified** so that upstream license headers,
copyright notices, and revision banners remain intact. To update the
vendored tree, replace the contents of `opus/` with the latest upstream
tag — see [`UPSTREAM.lock`](UPSTREAM.lock) and `tools/sync_opus.py`.

The build-configuration file [`opus_config.h`](opus_config.h) is the only
file in this directory that is **hand-written by PCMFlowOpus**. It
replaces the output that upstream's `./configure` script would normally
generate, fixing MCU-friendly defaults (fixed-point, no float API).

## License — BSD-3-Clause

```
Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic,
                    Jean-Marc Valin, Timothy B. Terriberry,
                    CSIRO, Gregory Maxwell, Mark Borgerding,
                    Erik de Castro Lopo

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

- Neither the name of Internet Society, IETF or IETF Trust, nor the
  names of specific contributors, may be used to endorse or promote
  products derived from this software without specific prior written
  permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

The full, authoritative license text is reproduced at the top of every
file in `opus/` and in `opus/COPYING` once the tree is populated by the
sync script.

## Opus Patent License

Opus is also covered by a separate **royalty-free patent license** issued
by Xiph.Org, Microsoft, Skype, Broadcom, and others. The text is
included upstream as `opus/PATENTS`. PCMFlowOpus inherits this license
unchanged — downstream users do not need to do anything special.
