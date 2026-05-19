#ifndef PCMFLOWOPUS_H
#define PCMFLOWOPUS_H

// Umbrella header for PCMFlowOpus.
//
// Including this single header gives the user the public API surface of
// the optional Opus codec add-on for PCMFlow:
//
//   - OpusEncoder : 16-bit PCM frame -> raw Opus packet  (implements PCMSink)
//   - OpusDecoder : raw Opus packet  -> 16-bit PCM frame (implements PCMSource)
//
// Ogg/Opus container support is intentionally not included in the v0.1.x
// surface; see SPEC.md "Deferred features".

#include "pcmflowopus_version.h"
#include "OpusEncoder.h"
#include "OpusDecoder.h"

#endif // PCMFLOWOPUS_H
