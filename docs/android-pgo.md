# Automated Android PGO training

The experimental PGO workflow is based on `feature/xclipse-unified-v2-20261001`
commit `325c8e9b2d20876c30d3ee91ae1cb5530d5c4d87`. It is opt-in; normal builds
use `EDEN_PGO_MODE=OFF`.

## On the S25 FE

Install the **GENERATE** APK from the **Xclipse Android PGO** workflow. It uses
the same mainline debug application ID and signing key as existing diagnostic
builds, with an increasing version code. Stop emulation, open **Settings → Debug**,
and select **Run automated PGO profiling**. There are no games to select or play.

The suite runs in its own process and task. It skips normal app initialization,
does not access saves or shader caches, and does not initialize or rotate the
main application log. Close/cancel or a 180-second stage timeout terminates only
the training process. Orientation changes retain the running activity. Each
successful stage writes a separate profile before moving to the next stage.

Select **Export profiling results** when finished and provide the ZIP for the
profile-use build. Instrumentation has runtime overhead: use this APK for training,
and benchmark the later **USE** APK against an otherwise identical non-PGO build.

## What the corpus exercises

1. Production swizzle/unswizzle, varied sizes, pixel sizes, and block heights;
   every round trip is checked against its original pixels.
2. Production BC1–BC7 CPU decoder fallback, signed/unsigned variants, legal
   compressed fixtures and deterministic output checks.
3. Production compute IR construction, selected optimization passes, verification,
   and SPIR-V generation across 16 arithmetic variants.
4. System-driver headless Vulkan capability/policy initialization and production
   Vulkan wrapper shader/pipeline creation, cold and warm private pipeline caches.

The Vulkan stage creates pipelines, but does not submit GPU work. It cannot train
guest CPU/NCE, Maxwell instruction translation, complete texture-cache behavior,
graphics rendering, presentation, or app lifecycle. It does not measure gameplay
performance. Its manifest states these exclusions. PGO is applied only to
translation units represented by this corpus; unrelated CPU and renderer
submission code retain their original optimization policy. Even within those
units synthetic frequencies may differ from gameplay: improvements require
on-device comparison and are not assumed.

## Build and merge

The workflow pins NDK `28.2.13676358`, arm64-v8a, and optimized RelWithDebInfo
with LTO. Existing Clang IPO configuration chooses the LTO implementation.
It retains the Android architecture preset; it does not use runner `-mcpu=native`
or add unverified CPU feature requirements.

Generation uses Clang source instrumentation with atomic counters. The harness
itself is not instrumented. Counters reset between stages, followed by an explicit
LLVM profile flush, so completed stages are not counted repeatedly. Each export
contains source identity, compiler version, ABI, suite version, device/SoC,
durations, result details, raw profile lengths, and SHA-256 hashes.

Once the exported ZIP is committed at `pgo/training.zip` on this branch, dispatch
**Xclipse Android PGO** with `mode=USE` and `corpus=pgo/training.zip`. Committing
the corpus changes the Git commit but does not change the compiled-source digest.
The merger rejects incomplete stages, stale source, a different compiler/ABI,
unexpected entries, oversized archives, and hash mismatches. The pinned NDK's
`llvm-profdata` then validates and merges the raw profiles without `-sparse`.
Clang profile-out-of-date warnings are fatal during profile use.

The USE APK has no runtime instrumentation. Regenerate the corpus after compiled
source changes. Revert to a normal diagnostic APK to disable PGO.

## Acceptance and validation

- Run `python3 -m unittest discover -s tools/tests -p 'test_pgo*.py' -v`.
- Configure OFF, GENERATE, and USE; reject unsupported mode/toolchain and missing
  profile. Confirm flags are restricted to trained sources and runtime linkage
  only appears in GENERATE.
- Build the optimized Android training APK in Actions; run all four stages on
  the S25 FE; merge its export with the exact NDK; build and compare the USE APK.
- Hardware collection, cancellation, export, driver pipeline acceptance, and
  performance remain unverified until an on-device run succeeds.
