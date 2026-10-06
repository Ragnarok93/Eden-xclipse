# S25 FE Vulkan capability-conformance audit — Samsung SPAL 25.3.3

Canonical regression baseline: Samsung SM-S731U / Galaxy S25 FE, Exynos 2400,
Xclipse 940, Android 17 / One UI 9, Samsung SPAL 25.3.3, Vulkan 1.3.304.

Evidence fixture:
`vp_gpuinfo_samsung_sm_s731u_25_3_3_android_17_0.json`
(SHA256 `dbf90405585ecdbc3e3dfa1c82eafefcda2ae7121f268c0846783116860cdb31`).

Live Vulkan queries remain authoritative. This report is a regression fixture and never
authorizes a static Xclipse 940 capability table or bypass of execution/readback probes.

## 25.3.3 deltas that affect the audit

- `alphaToOne=false`. Static multisample alpha-to-one is sanitized against the queried
  base feature. Dynamic `VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT` additionally requires
  the EDS3 feature. No Xclipse override may re-enable it.
- `storagePushConstant8=true`. The value is recorded from the live 8-bit-storage feature
  query. Eden does not enable a new shader path merely because this changed.
- `swapchainMaintenance1=true`. The feature is recorded in the report fixture only.
  Eden does not change presentation policy without a measured use case.
- `residencyNonResidentStrict=false`. Eden's audited sparse path does not currently rely
  on strict nonresident-page read semantics, so sparse support is not disabled globally.
  The queried value and sparse address-space size remain diagnostic evidence rather than
  pipeline-cache identity inputs.
- Descriptor-buffer capture/replay metadata changed from 4 to 8 bytes. Normal descriptor
  layouts continue to use `VkPhysicalDeviceDescriptorBufferPropertiesEXT` descriptor
  sizes plus Vulkan-reported set-layout sizes/binding offsets. Capture/replay metadata is
  not substituted for normal descriptor stride.
- The pipeline-cache UUID changed. Device policy identity includes the queried UUID, so
  24.0.534 driver cache data cannot be accepted as the same 25.3.3 cache identity.
- `textureCompressionBC=false` remains unchanged. BC4/BC5 and BPTC decode policy stays
  capability/probe driven; no native whole-family BC bit is forced.
- Subgroup size remains 64 with required-size control exposing 32–64 and required subgroup
  size limited to reported stages. No global Wave32/Wave64 override is added.
- Integer dot-product acceleration reporting changed. Eden currently has no Xclipse policy
  decision keyed from these acceleration hints, so they remain queried/report evidence and
  do not become static optimization assumptions.
- Six new extensions are advertised: `VK_EXT_host_image_copy`,
  `VK_EXT_image_compression_control`, `VK_EXT_pipeline_protected_access`,
  `VK_EXT_pipeline_robustness`, `VK_EXT_primitives_generated_query`, and
  `VK_KHR_present_id`. None is auto-adopted by this audit.

## DREF / shadow policy

R32_FLOAT is a color format and is not forced into a native comparison-sampler path.
The Xclipse startup probe now tests the actual legal fallback: raw R32 sampling through a
non-comparison sampler followed by the shader-side guest comparison. The probe is logged as
`r32_software_dref`, distinct from `d32_native_dref`.

The software R32 DREF policy requires validated raw R32 sampling plus the validated software
DREF probe. It no longer depends on native D32 comparison succeeding; D32 native comparison
is an independent capability. Detailed DREF logging is observability-only and may be disabled
without disabling this validated emulation path.

## Debug-control separation

The Android Debug UI is split semantically into:

- **Xclipse Compatibility / Performance** — memory-pressure policy, BCn/BPTC runtime decode,
  synchronization policy, submission batching experiment, subgroup-size policy.
- **Xclipse Validation** — bounded startup probes and pipeline-policy validation.
- **Xclipse Diagnostics / Logging** — low-overhead aggregate telemetry plus independently
  switchable bounded event diagnostics and DREF diagnostics.

Aggregate telemetry uses atomic counters/histograms and performs no hot-path string formatting.
Runtime pipeline-map hits are named explicitly and are not reported as Vulkan driver-cache hits.
Detailed image/fragment/DREF records no longer use the aggregate-telemetry enable as their gate.

## Pipeline / wait attribution

Telemetry separately records:

- exact `vkCreateGraphicsPipelines` / `vkCreateComputePipelines` duration,
- graphics build-wrapper duration,
- async worker queue residence,
- actual condition-variable blocking while a requested pipeline is not ready,
- host/timeline wait duration by tagged reason.

Log2 latency histograms provide bounded P50/P90/P95/P99 estimates without per-event logs.
This allows warm-cache performance runs to retain attribution while verbose diagnostics and
Vulkan-call tracing remain disabled.

## Cache identity

Policy identity is v10. It includes queried alpha-to-one support because that changes generated
pipeline state, and it already includes the full Vulkan pipeline-cache UUID. Probe timing,
diagnostic verbosity, sparse strictness, sparse address-space size, and descriptor capture/replay
metadata are excluded because they do not currently alter generated shader/pipeline behavior.

Future use of any excluded queried property must first make its execution dependency explicit
and then revisit policy/cache identity accordingly.
