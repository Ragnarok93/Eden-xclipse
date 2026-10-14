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
python3 -m unittest discover -s tools/tests -p 'test_eden_lab.py' -v
python3 tools/eden_lab.py --help
python3 tools/eden_lab.py --serial SERIAL device-info
```

Do not claim these pass before actually executing them.

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

The `--aggregate-telemetry` parameter above is **sandbox test data** and
does not change runtime native telemetry. A successful sandbox roundtrip
proves only the private file readback and exact-byte restoration.

Required physical negative tests: unknown request, invalid schema and types,
active emulator refusal, a second host lease, interrupted write/recovery,
corrupt recovery snapshot refusal, non-debug/release build refusal, and
preservation of production user data. Device-wide changes, installed game
launch, workload control and metrics are not authorized by this first slice.

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
| Host unit tests | Added, execution pending | tools/tests/test_eden_lab.py |
| APK and instrumentation build | Not verified | RelWithDebInfo workflow must build both |
| Physical device acceptance | Not started | Requires authorization and connected device |
| Native metric bridge | Not started | No fabricated performance claims |
| Automated gameplay and tuning | Not started | Requires isolation/metrics first |

A test cannot be marked complete based on source presence alone. Track
source commit, test run, artifact hashes and actual device outcomes in
subsequent updates.
