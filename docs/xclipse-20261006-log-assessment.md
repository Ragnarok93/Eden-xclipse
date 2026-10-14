# Xclipse gameplay log assessment — 2026-10-06

Input: `0.zip`, `eden_log.txt` (95,271 lines) and `eden_gpu.log`.
The text log appends many historical builds. This assessment isolates the final gameplay
session of `258bce1e373b016c6aadac26b19f5055055d3ecf`, starting near line 94,050,
and its following restart. Device: SM-S731U, Xclipse 940, Samsung 24.0.534,
Vulkan 1.3.279, 8 GB system RAM. Title ID: `0100F2C0115B6000`.

## Confirmed findings and changes

- **Low-memory termination:** the following restart records Android exit reason
  `low_memory(3)`. At gameplay frame 1800, available system memory is 569,072 KiB,
  process RSS is 662 MiB, and process swap is 1,134 MiB. Vulkan usage is 1,976 MiB
  against a reported 5,111 MiB budget. The Vulkan budget is not an Android process
  survival guarantee. Do not add RSS and Vulkan usage: accounting can overlap.
- **Unstable cache identity:** the run deletes a driver cache for policy
  `44fbbb517bc2a6fa` and selects `259559792aaa614d`. Multiple device initializations
  in the same process select different preferred waves. Raw timings were already
  excluded from the hash, but they still selected a hashed subgroup preference.
  Selection now keeps the driver default when both sizes validate and selects a
  forced size only when exactly one validates. Zero validated sizes selects none.
- **False missing-feature reports:** the base-feature copy after `GetFeatures2`
  was commented out by literal backslash-n sequences. Restore the copy and remove
  the unused local aggregate chain overwritten by the member-owned feature chain.
  No unsupported feature is forced on. Policy v8 invalidates caches once for the
  corrected interpretation; subsequent identical capability results should be stable.
- **Erased depth results:** five depth tests with one failure became zero cases and
  unsupported results after the transfer suite reset the shared structure. Reset
  transfer measurements while preserving every independent depth result and count.
  Failed/unvalidated operations remain failed/unvalidated.
- **Whole-guest pageout during gameplay:** every qualifying pressure transition or
  300-frame interval called `MADV_PAGEOUT` over the live guest backing. Remove this
  renderer-triggered pageout; let Android select cold pages. Staging reclamation,
  pressure classification, and texture LRU pressure handling remain active.
  This removes an explicit source of paging churn; it does not establish that the
  game's working set now fits or that low-memory kills are eliminated.
- **Lost pipeline evidence on abrupt exit:** pipeline summaries only showed zero
  counters from temporary devices. Emit live creation counts, runtime map lookups,
  failures, and average/max compile times every 300 frames through the normal debug
  log. Log driver-cache bytes and policy after successful cache-object creation.
  Runtime map hits are not driver compilation-cache hits.

## Remaining evidence gaps

At frame 1800 the run reports 25,509 submits, 11,987 host waits, and 5,805 scheduler
finishes. Those cumulative counts do not attribute stall duration to compilation,
texture readback, or paging. Preserve existing scheduler/semaphore synchronization;
do not remove waits based on counts alone. The latest session also reports scheduler
context-guard failures at startup; their relationship to gameplay termination is
unproven. Historical Maxwell assertions and older validation errors must not be
reported as failures of this latest session.

The separate GPU log describes initialization only (zero Vulkan-call/allocation
counters); it is not a gameplay crash trace. No new setting can be justified from it.

## Verification and device gate

- Focused Catch2 profile suite: 8 cases, 54 assertions passed locally, including
  timing-invariant identity, all wave-validation combinations, preserved depth
  successes/failures, and cleared transfer measurements.
- Compiled the actual base-feature query/copy fragment with a fake physical device:
  baseline fails; patched fragment preserves supported and unsupported bits.
- Static pressure-path checks confirm removal of the pageout call and retention of
  staging/texture reclamation. They do not measure memory or performance improvement.
- Full Android build and existing policy CI run on the pushed branch; inspect their
  results before using the APK. There is no Xclipse device in the local test runtime.
- On device, run the same route twice with unchanged settings. Verify matching policy
  hashes and a loaded driver cache on the second run, preserved depth probe counts,
  live pipeline counters, memory pressure/swap trends, and the next Android exit
  reason. First-run compilation after v8 invalidation is expected. Stable frame times
  and crash elimination remain unverified until that test.
