# S25 FE capability-conformance audit

Audited base: `653f4a177fce900ce4dffe079fea28156462b315` on
`feature/xclipse-full-optimization-20261005`.
Evidence: user-supplied `vp_gpuinfo_samsung_sm_s731u_24_0_534_android_16_0.json`,
Samsung SM-S731U, driver 24.0.534, Vulkan 1.3.279.
Live Vulkan queries remain authoritative; this report supplies regression fixtures,
not hardcoded runtime overrides or permission to bypass execution validation.

## Findings and corrections

| Path | Report / audit evidence | Corrected behavior |
| --- | --- | --- |
| Static line pipelines | `rectangularLines=false`, `smoothLines=false`, but EXT line rasterization exists | Select DEFAULT rather than unsupported RECTANGULAR when neither requested mode is available. Do not substitute Bresenham simply because it is supported. |
| Samsung dynamic blending | EDS3 blend bits advertised, but driver workaround disables them after cached support was derived | Apply driver/user masks before deriving support, and refresh after EDS-level settings. Static blend pipeline state remains authoritative for the Samsung workaround. |
| Core buffer device address | Core 1.2 feature is true; extension-name bookkeeping remained false on the core path | Derive usable BDA from API/extension availability AND the queried feature; align VMA allocation flags and probe gating. Load the KHR entry point only as a fallback when the core symbol is absent. |
| Base-feature enablement | Renderer feature copy can be filtered after the physical query | Copy the final filtered bits into the actual device-create feature chain. |
| Missing mandatory capabilities | Constructor logged “continuing anyways” after suitability failure | Reject device creation with FEATURE_NOT_PRESENT. Query driver identity before checking existing driver-specific feature exemptions. All 29 required feature checks pass against this report. |

## Capabilities that remain guarded

| Capability | Device evidence | Handling |
| --- | --- | --- |
| Whole BC family | `textureCompressionBC=false`; BC1–BC3 format masks exist | Do not force the whole-family bit. Retain per-format checks and execution probes; absent BC4–BC7 formats use decode fallback. |
| Required subgroup size | 32–64; required-size stages COMPUTE only | Retain exact-size validation and stage checks. No forced graphics-stage subgroup size. |
| Descriptor buffers | Supported; `descriptorBufferImageLayoutIgnored=false` | Keep the unsupported layout-ignored bit false. Both buffer and image execution probes must pass before the existing backend is selected. Core BDA correction makes those probes reachable; runtime success still needs device testing. |
| Dynamic alpha-to-one | EDS3 bit false; base alphaToOne true | Static alpha-to-one may be used; its unsupported dynamic state is not enabled. |
| EDS2 extras | LogicOp and PatchControlPoints false | Retain feature-specific gates; extension presence alone does not permit them. |
| Shader float64 | False | Do not enable it or claim native FP64 support. |
| Sampler min/max | False; EXT sampler_filter_minmax absent | Retain weighted-average fallback; no forced reduction feature. |
| R32_SFLOAT / D32 filtering | No linear-filter format bit | Retain per-view/per-blit filtering checks. The report's legacy format masks do not prove depth-comparison support; continue querying Flags2 and validating depth operations. |
| D24_UNORM_S8_UINT | Absent from reported formats | Retain queried format alternatives rather than forcing D24. |
| Limits | 16 viewports, 8 color attachments, 8 clip distances, 128-byte push constants, 16x anisotropy | Report satisfies audited fixed requirements; no Xclipse limit inflation added. |

`XCLIPSE ENABLEMENT` logs final BDA, descriptor buffer, EDS3 blending, dynamic
alpha-to-one, line-mode, FP64, and min/max decisions before device creation.
Policy identity advances to v9 to separate cached work from the corrected decisions.

## Verification

The focused Catch2 executable compiles the actual policy implementations and runs
15 cases / 81 assertions, including report-derived EDS3, line and core-1.2 fixtures.
The fixture records the supplied report's SHA256. Existing profile/pipeline tests
remain in the run. Static integration checks verify post-mask support derivation,
filtered base-feature copying, driver-aware exemptions, and failure before device
creation when suitability fails. Android compilation and Vulkan validation on the
S25 FE remain separate gates; these tests do not certify every possible guest draw.

Relevant specification references:
- https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRasterizationLineStateCreateInfo.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceBufferDeviceAddressFeatures.html
- https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceCreateInfo.html
