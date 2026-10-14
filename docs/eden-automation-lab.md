# Eden-Xclipse automated device lab: Phase 1

Target: Galaxy S25 FE (SM-S731U), Exynos 2400 / Xclipse 940, Android 17,
Samsung SPAL 25.3.3, Vulkan 1.3.304. These are **reference conditions**:
read the actual device and Vulkan driver before making compatibility claims.

Source inspiration: the separately maintained GameNative
`Codex-Automated-Testing` branch. This first slice implements only an
instrumentation request bridge, sandbox-only configuration transaction,
host-side ADB isolation/lease, and mockable unit tests. It does **not**
launch games, tune Xclipse policies, alter production emulator settings,
install APKs, capture frame metrics, or establish performance improvements.

## Source locations

- `tools/eden_lab.py`: standard-library-only host CLI, strict protocol,
  explicit device selection, idle guard, host lease, private request staging.
- `tools/tests/test_eden_lab.py`: host request/transport/authorization tests.
- `tools/eden_lab_artifacts.py`: read-only `aapt2` + `apksigner`
  inspection of the optimized application and matching instrumentation APK.
  Enforces package/runner target, versionCode and signing-certificate identity,
  emits local SHA256 provenance, and never installs or wipes data.
- `tools/tests/test_eden_lab_artifacts.py`: synthetic APK metadata
  validation/negative tests without a device or SDK.
- `tools/eden_lab_campaign.py`: offline fail-closed repeated cold/warm trial
  comparison. Validates driver and title identity, per-trial pinned binary and
  effective-policy hashes, independent game-frame timestamp provenance, and
  crash/correctness/thermal requirements. Never automatically promotes code.
- `tools/tests/test_eden_lab_campaign.py`: synthetic *validator* fixtures,
  not device-derived benchmarking evidence.
- `src/android/app/src/relWithDebInfo/java/org/yuzu/yuzu_emu/benchmark/`:
  debug-only `EdenLabSessionManager`; changes only
  `files/eden-lab/sandbox.json`.
- `src/android/app/src/androidTest/java/org/yuzu/yuzu_emu/benchmark/`:
  Android instrumentation entry; no exported production API.
- `src/android/app/build.gradle.kts`: RelWithDebInfo test build selection
  and matching instrumentation runner/dependencies.

## Host validation

From the repository root:

```bash
python3 -m unittest discover -s tools/tests -p 'test_eden_lab*.py' -v
python3 tools/eden_lab.py --help
python3 tools/eden_lab.py --serial SERIAL device-info
```

Do not claim these pass before actually executing them.

## APK signing and package acceptance

The manually dispatched `xclipse-android-debug.yml` workflow now generates
both optimized app and matching instrumentation APKs and fails closed if
`tools/eden_lab_artifacts.py` detects different signatures, version codes,
package IDs or instrumented package targets. The signed artifact manifest is
saved as `artifacts/eden-lab-artifact-attestation.json`.

For local validation, use the Android SDK's `aapt2` and `apksigner`:

```bash
python3 tools/eden_lab_artifacts.py APP.apk INSTRUMENTATION.apk \
  --aapt "$ANDROID_HOME/build-tools/36.0.0/aapt2" \
  --apksigner "$ANDROID_HOME/build-tools/36.0.0/apksigner"
```

An `--expected-cert-sha256` should be supplied against the **already trusted**
local debug certificate when it is known. Source-commit labels in this
manifest are claims supplied by CI; package metadata alone cannot
cryptographically attest to the source commit. Installing and running these
APKs on an S25 FE remains a separate, authorized acceptance gate.

**Build cadence:** full optimized APK build is `workflow_dispatch` only;
`xclipse-lab-fast-checks.yml` runs fast Python and Vulkan-policy gates after
source changes. This prevents repeated CI cancellations during iteration.

## Physical device acceptance (not yet done)

Before installing, obtain authorization, verify the package identity and
certificate, back up important app data and ensure the device is idle.
Build `assembleMainlineRelWithDebInfoAndroidTest` from exactly the same
source tree as the RelWithDebInfo application APK, then install both using
in-place updates without clearing data. The expected application package
is `dev.eden.eden_emulator.debug`. The instrumentation runner component
is `dev.eden.eden_emulator.debug.test/androidx.test.runner.AndroidJUnitRunner`.

```bash
python3 tools/eden_lab.py --serial SERIAL device-info
python3 tools/eden_lab.py --serial SERIAL probe
python3 tools/eden_lab.py --serial SERIAL sandbox-roundtrip \
  --tag basic --aggregate-telemetry --allow-test-writes
python3 tools/eden_lab.py --serial SERIAL recover
```

The Phase 2 native groundwork exposes `NativeLibrary.getLabFrameCounts()`:
non-resetting system-frame and game-frame counts since the current emulated
title began. This does **not** yet provide per-game-frame timestamps, display
cadence, shader timing, or actual on-device benchmark evidence. The read-only
lab probe exercises the JNI path and reports zeros when emulation is idle.

The `--aggregate-telemetry` parameter above is **sandbox test data** and
does not change runtime native telemetry. A successful sandbox roundtrip
proves only the private file readback and exact-byte restoration.

Required physical negative tests: unknown request, invalid schema and types,
active emulator refusal, a second host lease, interrupted write/recovery,
corrupt recovery snapshot refusal, non-debug/release build refusal, and
preservation of production user data. Device-wide changes, installed game
launch, workload control and metrics are not authorized by this first slice.

## Strict offline performance evidence comparator

Run `python3 tools/eden_lab_campaign.py CAMPAIGN.json` once a future
gameplay collector has produced a complete schema-v1 JSON campaign. The
comparison requires at least three **cold and three warm** physical-device
trials for each variant, matching live driver, title, checkpoint and Android
fingerprint, matching per-trial effective-policy and artifact identities, and
actual game-frame timestamp distributions. It rejects crashes, correctness
faults and missing metrics, and marks even apparent improvements as
`candidate_improvement_needs_review`, never automatically promotable.

The existing Phase 1 tools **cannot yet generate qualifying evidence**. The
test fixtures are deliberately synthetic and test only validation logic.
The report field `measurement_claim=unverified_external_evidence` warns that
the comparator does not independently authenticate submitted measurements.

## Next implementation gates

- Extend binary/build provenance and resolve pinned GitHub Actions artifacts.
- Add isolated per-title settings/saves/NAND/cache root without modifying
  original saves or shader caches; prove readback and exact restoration.
- Implement session-owned test game launch and virtual input replay, with a
  frozen screenshot/route checkpoint and an idle/lifecycle guard.
- Expose non-destructive, timestamped native PerfStats/XclipseTelemetry
  snapshots and structured correctness counters via JNI.
- Add Android memory/thermal/process collectors, bounded Perfetto traces,
  independent source-game/render/display cadence measurement, and evidence
  coverage checks.
- Implement cold/warm baseline-candidate comparisons, thermal gates and
  candidate-regression classification; only then enable Codex tuning loops.

## Progress ledger

| Gate | Status | Evidence or limitation |
| --- | --- | --- |
| Fork AI/Codex policy | Complete | AGENTS.md, CLAUDE.md, docs/policies/AI.md |
| Host strict protocol/lease | Implemented, not device-tested | tools/eden_lab.py |
| Instrumentation-only bridge | Implemented, build pending | EdenLabBridgeTest |
| Journaled private sandbox roundtrip | Implemented, hardware validation pending | EdenLabSessionManager |
| Host unit tests | Passing in fast CI | tools/tests/test_eden_lab*.py |
| APK identity preflight | Implemented, fast tests passed | aapt2/apksigner read-only attestation; real artifacts pending |
| APK and instrumentation build | Not verified | Manually dispatched RelWithDebInfo workflow must build both |
| Physical device acceptance | Not started | Requires authorization and connected device |
| Native session frame counters | Implemented, build pending | Non-resetting JNI; not yet live-workload sampled |
| Frame timestamps/Xclipse aggregate snapshot | Not started | Required for valid gameplay comparison |
| Cold/warm offline comparator | Implemented, host tests pending | Strict provenance and no automatic promotion |
| Automated gameplay and tuning | Not started | Requires isolation/metrics first |

A test cannot be marked complete based on source presence alone. Track
source commit, test run, artifact hashes and actual device outcomes in
subsequent updates.
