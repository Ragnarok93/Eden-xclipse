# Agent instructions: Ragnarok93/Eden-xclipse

These instructions govern AI-assisted development, Codex automation, and human-assisted
engineering **in this fork**. The fork maintainer authorizes AI tools for code review,
implementation, debugging, research, testing, documentation, and experiment orchestration.
The fork-specific guidance in `CLAUDE.md` and `docs/policies/AI.md` aligns with
these instructions. **Do not infer permission to submit AI-assisted contributions
to upstream Eden or its community**; respect upstream rules when interacting with them.

The primary objective is a correct, stable, high-performance Eden Android emulator on
**Samsung Exynos 2400 / Xclipse 940**, especially the Galaxy S25 FE (SM-S731U).
Prioritize correctness, frametime consistency, thermal sustainability, and reproducible
on-device evidence ahead of speculative peak-FPS improvements.

## Scope, priorities, and invariants

1. **Correctness first.** Do not trade image fidelity, guest memory correctness,
   synchronization correctness, game compatibility, or crash safety for FPS without
   an explicit task-specific decision. Track visual and functional regressions.
2. **Capability-driven Vulkan.** Query the active physical device, driver, feature
   structures, extensions, formats, subgroup properties, limits, and pipeline-cache
   UUID. Never hard-code capabilities from the reference phone, force unsupported
   features, or use a Vulkan 1.4-only mechanism on a Vulkan 1.3 driver without a
   validated compatible alternative.
3. **Constrain Xclipse work.** Prefer targeted policies and proven driver workarounds
   over global renderer rewrites. Preserve the default path on non-Xclipse GPUs.
   Experimental paths must have explicit guards and a safe fallback.
4. **Preserve emulator architecture.** Keep Eden's renderer, scheduler, semaphore,
   cache, shader-recompiler, and native/Android lifecycle contracts intact unless
   the task expressly requires changing them. Prefer isolated, testable changes.
5. **Optimize measurable bottlenecks.** Focus on shader translation/compilation,
   pipeline scheduling and cache identity, synchronization and queue waits, descriptor
   handling, BCn/BPTC texture decoding, image transfer/blits, memory pressure,
   NCE/CPU behavior, and presentation pacing, as the evidence warrants.
6. **Keep instrumentation honest and inexpensive.** Normal builds must not pay
   significant hot-path costs for detailed diagnostics. Use bounded counters,
   histograms, and optional event capture; do not add per-frame string formatting,
   unbounded allocations, or intrusive tracing to performance trials.
7. **Honor licenses and ownership.** Preserve SPDX headers, applicable open-source
   licenses, and attribution. Never commit games, keys, firmware, private saves,
   credentials, signing secrets, or proprietary test assets.

The S25 FE capability audit in `docs/s25fe-vulkan-capability-audit.md` is a
**historical regression fixture**, not an immutable specification. Its documented
Android 17 / Samsung SPAL 25.3.3 / Vulkan 1.3.304 combination must be confirmed
against the **live** driver before relying on a device-specific property.

## Repository map

- `src/video_core/renderer_vulkan/`: Vulkan rasterization, scheduling, pipelines,
  image operations, descriptor management, and presentation.
- `src/video_core/vulkan_common/`: device capabilities, Xclipse policy/probes,
  low-overhead telemetry, and memory-pressure instrumentation.
- `src/common/correctness_telemetry.h`: correctness fault counters.
- `src/core/perf_stats.*`: emulator/game performance statistics.
- `src/android/app/src/main/`: Android frontend, native JNI, configuration, and
  emulation lifecycle. Use `NativeLibrary`, `NativeConfig`, `NativeInput`, and
  `EmulationActivity` as integration references.
- `src/tests/video_core/`: focused Vulkan and Xclipse correctness/policy tests.
- `.github/workflows/xclipse-android-debug.yml`: current diagnostic Android
  build and policy-test workflow.
- `.github/workflows/xclipse-pgo.yml`, `docs/android-pgo.md`, and
  `tools/pgo_profile.py`: experimental PGO training, validation, and profile use.

Read the relevant implementation and tests before changing an interface. Inspect
nearby upstream/fork fixes when useful, but validate portability and licensing
before applying them. Do not claim to have searched, built, or tested something
without corresponding evidence.

## Codex automated build, debugging, and optimization lab

Implement the Eden automation lab as a **host-driven, evidence-based system**,
adapting the design patterns of GameNative's `Codex-Automated-Testing` branch.
Do not copy GameNative's Wine/XServer assumptions into Eden.

The intended layers are:

1. **Python host orchestrator:** pinned build dispatch, artifact verification,
   explicit ADB serial, exclusive device lease, profiles, trials, evidence, recovery,
   comparisons, and report generation. Proposed package: `tools/eden_lab/`.
2. **Debug-only Android instrumentation bridge:** bounded requests; isolated
   benchmark configuration, saves, and shader caches; session lifecycle; guest
   input playback; run ownership; exact restoration and durable recovery journal.
   Prefer non-exported, instrumentation-owned controls to production intents.
3. **Native C++ telemetry bridge:** timestamped, non-destructive snapshots of
   `XclipseTelemetry`, correctness counters, `PerfStats`, renderer activity,
   Vulkan waits, and native fault diagnostics. Use explicit session/run IDs.
4. **Analysis and experiment engine:** reproducible baseline/candidate comparisons,
   diagnostic attribution, regression gates, and reviewable optimization proposals.

Build this in independently verifiable slices: transport/configuration safety;
native telemetry; isolated workload and deterministic replay; performance
comparison and diagnostics; then automated candidate iteration. Do not report
real gameplay performance from a build-only or configuration-only acceptance test.

### Agent experiment loop

For each change:

1. Record the baseline commit, target device/OS/driver, exact workflow/toolchain,
   configuration, game title/update/mods, workload checkpoint, cache policy, and
   relevant source/binary hashes. Distinguish requested settings, persisted readback,
   effective runtime policy, and confirmed loaded binary identity.
2. State one falsifiable hypothesis and predicted telemetry changes. Keep a trial
   scoped to one policy/component; document unavoidable interactions.
3. Implement the smallest reviewable patch, run relevant host/unit/policy tests,
   and request the established optimized Android diagnostic build.
4. Confirm build and signing provenance before any device deployment. Reproduce
   a fixed workload with matching baseline/candidate controls, repeated cold/warm
   trials, cache isolation, and thermal/frequency observations.
5. Analyze game FPS, emulation speed, renderer submissions, and **displayed cadence
   as distinct channels**. Separate native source frames from frame generation;
   do not infer game FPS from SurfaceFlinger/display callbacks.
6. Inspect correctness checks, crashes, device loss, memory pressure, GPU wait
   attribution, shader/pipeline histograms, and frame-time tails. Treat missing,
   truncated, or inaccessible metrics as **unavailable**, not zero or success.
7. Classify outcomes as improvement, regression, inconclusive, or invalid. Reject
   incomplete/mismatched campaigns and do not auto-promote a candidate based on
   one good run or average FPS alone.
8. Preserve a concise experiment report with hypothesis, source diff, tests,
   device evidence, caveats, and next action. Checkpoint verified progress to Git.

Use a minimum of three comparable cold and warm trials per variant as an initial
screening rule; increase samples when variance or thermals obscure the outcome.
Never claim statistically established causality from a few runs.

### Data isolation, deployment, and recovery

- Read-only source inspection and mock/unit tests are safe defaults. Before
  installing/updating APKs or changing device-wide settings, confirm that the
  user has authorized that class of action in the current session.
- Always select the intended device by explicit ADB serial. Refuse operations if
  an unrelated game session is running; never kill a user's active game to make
  room for an automated test.
- Use benchmark-only profiles, working saves/NAND, shader-cache directories, and
  controller state. Do not alter original game saves, production caches, accounts,
  or active per-game settings. Reject a workload if isolation is not provable.
- Keep a host lease and an app-private transaction lock. Journal original settings
  and exact hashes before mutation; verify readback, restore on exit, and block
  further writes when recovery is pending or validation fails.
- Preserve the existing upgradeable debug package identity, signing certificate,
  and monotonically increasing version code. Use matching app/instrumentation
  artifacts from the same source revision. **Never uninstall or clear data** to
  repair an update, signing, or version mismatch.
- Do not silently change thermal policies, CPU governors, root/system properties,
  global caches, or external files. Require explicit, scoped authorization.
- Capture app logcat, structured native telemetry, Android process/thermal/memory
  information, and optional bounded Perfetto traces when available. Do not assume
  privileged counters, GPU timestamps, or tombstones can always be collected.

### Telemetry interpretation

- Treat Eden runtime pipeline-map, disk shader cache, and Vulkan driver cache
  metrics as separate sources; never relabel one as another.
- Keep pipeline creation time, asynchronous queue residence, blocked waits, GPU
  submission CPU cost, and actual GPU execution time separate. Vulkan CPU submit
  timestamps are **not** proof of GPU completion.
- Treat `PerfStats` game FPS, system FPS, frametime, and emulation speed according
  to their definitions. Do not let lab sampling destructively reset statistics
  used by the overlay or other consumers; add a non-resetting snapshot interface.
- Keep correctness probes independent of diagnostics verbosity. Preserve feature
  and pipeline-policy cache identity whenever changes alter shader generation
  or pipeline state.
- Compare performance with detailed event logging disabled unless explicitly
  measuring its overhead. Use bounded high-detail traces for diagnosis, not as
  the default performance evidence.

## Validation gates

For any change, run the narrowest relevant tests first, then broader gates:

- Focused Vulkan/Xclipse tests under `src/tests/video_core/`.
- Python unit tests for host tooling; current PGO tests can be run with:
  `python3 -m unittest discover -s tools/tests -p 'test_pgo*.py' -v`.
- Relevant CMake/CTest build and `ctest --output-on-failure` where configured.
- The policy gate in `xclipse-android-debug.yml` and the matching
  RelWithDebInfo Android artifact build before describing a candidate as build-tested.
- Device-only assertions (driver behavior, rendering, thermals, frame pacing,
  stability) require actual physical-device evidence.

Never silence failing tests, substitute synthetic screenshots for game output,
fabricate hashes, or report a CI status as passed without checking the result.
State what is verified, what is hypothesized, and what remains untested.

## Git workflow and progress continuity

- Work on the designated Xclipse feature branch or a task-specific branch rooted
  in it. Check the remote head before writes; preserve unrelated changes.
- Commit cohesive, passing increments and push regular checkpoints to this fork,
  with a short message describing the actual behavior changed.
- Keep a living automation progress checklist/report under `docs/` when
  implementing multiple slices. Record completed gates, current blockers,
  exact artifact/commit identifiers, and next verifiable steps so subsequent
  Codex sessions can resume without reconstructing the investigation.
- Do not force-push, rewrite shared history, merge to `main`, publish a release,
  or modify upstream repositories without explicit instruction.
- When a build, driver property, test, or root cause is uncertain, investigate
  systematically; ask for guidance if a consequential choice cannot safely be
  resolved from repository evidence. Report partial results rather than implying
  unperformed work is finished.

**Definition of done:** the code change is reviewed against the stated hypothesis,
the applicable automated gates pass, evidence is retained, device-only claims are
backed by real-device runs, user data is preserved, and a reproducible Git checkpoint
identifies exactly what was verified.
