#ifndef PCMFLOWOPUS_OPUS_CONFIG_H
#define PCMFLOWOPUS_OPUS_CONFIG_H

// Hand-written replacement for the `config.h` that libopus's autotools
// `./configure` step would normally generate. PCMFlowOpus vendors libopus
// as a verbatim source subset (see UPSTREAM.lock) and supplies the
// configuration here instead.
//
// This file is intentionally tracked in git and is NOT touched by
// tools/sync_opus.py.
//
// Conventions:
//   - Embedded targets, fixed-point math, no float API.
//   - VLA on GCC-class compilers (Arduino default); alloca otherwise.
//   - No SIMD optimizations enabled by default; revisit per target as
//     measured ESP32-S3 / RP2350 / etc. performance comes in.
//
// STATUS: skeleton. Final flags will be finalized when the build is wired up.

// --- Package identity (libopus expects these from autoconf) ----------
#define PACKAGE_NAME    "libopus (PCMFlowOpus vendored)"
#define PACKAGE_VERSION "vendored"
#define OPUS_BUILD      1

// --- Math mode ------------------------------------------------------
// Fixed-point: no FPU dependence, smaller code, slightly lower quality
// at very high bitrates. Required for our MCU footprint targets.
#define FIXED_POINT       1
#define DISABLE_FLOAT_API 1

// --- VLA / alloca ---------------------------------------------------
// libopus uses VAR_ARRAYS extensively; GCC (Arduino default) supports
// VLA so we enable it. Toolchains without VLA should switch this to
// USE_ALLOCA.
#if defined(__GNUC__)
#  define VAR_ARRAYS 1
#else
#  define USE_ALLOCA 1
#endif

// --- libc features --------------------------------------------------
// Most modern toolchains targeting our practical MCUs provide these;
// override per-target if a build fails on the symbol.
#define HAVE_LRINTF 1
#define HAVE_LRINT  1

// --- Architecture intrinsics ---------------------------------------
// libopus tests these with `#if defined(OPUS_*_MAY_HAVE_*)`, which is
// true whenever the macro is defined REGARDLESS of value. To keep the
// SIMD paths disabled the macros must be LEFT UNDEFINED, not defined
// to zero. Equivalent of autoconf's `/* #undef OPUS_*_MAY_HAVE_* */`.
// (Enable selectively per-target by uncommenting and pairing with the
//  corresponding RTCD / PRESUME macros.)
//
// #define OPUS_ARM_MAY_HAVE_NEON
// #define OPUS_ARM_MAY_HAVE_NEON_INTR
// #define OPUS_X86_MAY_HAVE_SSE
// #define OPUS_X86_MAY_HAVE_SSE2
// #define OPUS_X86_MAY_HAVE_SSE4_1
// #define OPUS_X86_MAY_HAVE_AVX2

// --- Feature gating ------------------------------------------------
// CELT analysis improves audio mode quality; SILK is the speech side
// (mandatory for our VoIP target). Keep both on by default; let the
// linker discard unused encode paths in decoder-only builds.
#define ENABLE_HARDENING 1

#endif // PCMFLOWOPUS_OPUS_CONFIG_H
