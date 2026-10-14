// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Vulkan {

// Startup wall-clock measurements include scheduling and compilation noise. Keep the driver
// default when both sizes work; force a size only when it is the sole validated alternative.
[[nodiscard]] constexpr std::uint32_t SelectXclipseComputeWave(bool wave32, bool wave64) noexcept {
    return wave32 == wave64 ? 0U : wave32 ? 32U : 64U;
}

enum class CapabilityState : std::uint8_t {
    Unsupported,
    Advertised,
    Validated,
};

[[nodiscard]] constexpr std::string_view CapabilityStateName(CapabilityState state) noexcept {
    switch (state) {
    case CapabilityState::Unsupported:
        return "unsupported";
    case CapabilityState::Advertised:
        return "advertised";
    case CapabilityState::Validated:
        return "validated";
    }
    return "unsupported";
}

enum class BcnFormat : std::uint8_t {
    BC1_RGB_UNORM,
    BC1_RGB_SRGB,
    BC1_RGBA_UNORM,
    BC1_RGBA_SRGB,
    BC2_UNORM,
    BC2_SRGB,
    BC3_UNORM,
    BC3_SRGB,
    BC4_UNORM,
    BC4_SNORM,
    BC5_UNORM,
    BC5_SNORM,
    BC6H_UFLOAT,
    BC6H_SFLOAT,
    BC7_UNORM,
    BC7_SRGB,
    Count,
};

constexpr std::size_t BcnFormatCount = static_cast<std::size_t>(BcnFormat::Count);

struct FormatCapabilitySnapshot {
    // Image creation is validated for the sampled/upload usage baseline. Optional operations
    // such as transfer-source readback are tracked independently below.
    CapabilityState image_create{CapabilityState::Unsupported};
    CapabilityState sampled{CapabilityState::Unsupported};
    CapabilityState linear_filter{CapabilityState::Unsupported};
    CapabilityState storage_image{CapabilityState::Unsupported};
    CapabilityState transfer_src{CapabilityState::Unsupported};
    CapabilityState transfer_dst{CapabilityState::Unsupported};
    CapabilityState blit_src{CapabilityState::Unsupported};
    CapabilityState blit_dst{CapabilityState::Unsupported};
};

[[nodiscard]] constexpr bool SupportsAdvertisedBcnUsage(
    const FormatCapabilitySnapshot& format, bool require_transfer_src = true,
    bool require_transfer_dst = true) noexcept {
    return format.image_create != CapabilityState::Unsupported &&
           format.sampled != CapabilityState::Unsupported &&
           format.linear_filter != CapabilityState::Unsupported &&
           (!require_transfer_src || format.transfer_src != CapabilityState::Unsupported) &&
           (!require_transfer_dst || format.transfer_dst != CapabilityState::Unsupported);
}

[[nodiscard]] constexpr bool SupportsValidatedBcnUsage(
    const FormatCapabilitySnapshot& format, bool require_transfer_src = true,
    bool require_transfer_dst = true) noexcept {
    return format.image_create == CapabilityState::Validated &&
           format.sampled == CapabilityState::Validated &&
           format.linear_filter == CapabilityState::Validated &&
           (!require_transfer_src || format.transfer_src == CapabilityState::Validated) &&
           (!require_transfer_dst || format.transfer_dst == CapabilityState::Validated);
}

struct VulkanDeviceIdentity {
    std::string device_name;
    std::string driver_name;
    std::string soc_model;
    std::uint32_t vendor_id{};
    std::uint32_t device_id{};
    std::uint32_t driver_id{};
    std::uint32_t driver_version{};
    std::array<std::uint8_t, 16> pipeline_cache_uuid{};
};

struct VulkanCapabilitySnapshot {
    CapabilityState timeline{CapabilityState::Unsupported};
    CapabilityState synchronization2{CapabilityState::Unsupported};
    CapabilityState descriptor_buffer{CapabilityState::Unsupported};
    CapabilityState sparse_binding{CapabilityState::Unsupported};

    // Queried base/property facts used for regression evidence. Sparse strictness and
    // capture/replay sizes are not renderer policy inputs unless a path actually depends on them.
    bool alpha_to_one{};
    bool storage_push_constant_8{};
    bool residency_non_resident_strict{};
    std::uint64_t sparse_address_space_size{};
    std::uint64_t buffer_capture_replay_descriptor_size{};
    std::uint64_t image_capture_replay_descriptor_size{};

    CapabilityState subgroup_ballot{CapabilityState::Unsupported};
    CapabilityState subgroup_shuffle{CapabilityState::Unsupported};
    CapabilityState subgroup_arithmetic{CapabilityState::Unsupported};
    CapabilityState subgroup_quad{CapabilityState::Unsupported};
    CapabilityState required_subgroup_size{CapabilityState::Unsupported};
    CapabilityState compute_2d_invocations{CapabilityState::Unsupported};
    CapabilityState compute_3d_invocations{CapabilityState::Unsupported};

    std::uint32_t subgroup_size{};
    std::uint32_t subgroup_supported_stages{};
    std::uint32_t subgroup_supported_operations{};
    std::uint32_t min_subgroup_size{};
    std::uint32_t max_subgroup_size{};
    std::uint32_t required_subgroup_size_stages{};

    std::array<FormatCapabilitySnapshot, BcnFormatCount> bcn{};
};

struct XclipseHardwareProfile {
    bool detected{};
    std::uint32_t generation{};
    std::uint32_t model{};

    bool wave32_validated{};
    bool wave64_validated{};
    std::uint32_t allowed_wave_mask{};
    std::uint32_t preferred_compute_wave{};
    std::uint64_t wave32_probe_ns{};
    std::uint64_t wave64_probe_ns{};

    bool bc1_native{};
    bool bc2_native{};
    bool bc3_native{};
    bool bc4_native{};
    bool bc5_native{};
    bool bc6_native{};
    bool bc7_native{};

    bool descriptor_buffer_validated{};
    bool descriptor_buffer_image_validated{};
    bool sparse_binding_validated{};
    bool synchronization2_validated{};
    bool rgtc_gpu_decode_validated{};
    bool bc6_gpu_decode_capable{};
    bool bc7_gpu_decode_capable{};
    bool bptc_gpu_decode_capable{};
    bool bc6_gpu_decode_validated{};
    bool bc7_gpu_decode_validated{};
};

struct XclipseOptimizationProbeResults {
    // Capability probes are fail-closed: Advertised means Vulkan reports support, Validated means
    // the exact operation completed successfully on this device/driver.
    CapabilityState timestamp_queries{CapabilityState::Unsupported};
    CapabilityState empty_queue_submit{CapabilityState::Unsupported};
    CapabilityState buffer_transfer{CapabilityState::Unsupported};
    CapabilityState image_transfer{CapabilityState::Unsupported};
    CapabilityState storage_image_create{CapabilityState::Unsupported};

    // Depth/Dref execution probes are deliberately independent of advertised format features.
    CapabilityState r32_sampled_image{CapabilityState::Unsupported};
    CapabilityState r32_dref_sample{CapabilityState::Unsupported};
    CapabilityState r32_compare_non_dref{CapabilityState::Unsupported};
    CapabilityState r32_compare_dref{CapabilityState::Unsupported};
    CapabilityState d32_compare_dref{CapabilityState::Unsupported};
    CapabilityState mutable_r32_d32_view{CapabilityState::Unsupported};

    std::uint32_t queue_family_count{};
    std::uint32_t graphics_queue_count{};
    std::uint32_t dedicated_compute_queue_count{};
    std::uint32_t dedicated_transfer_queue_count{};

    std::uint32_t timestamp_valid_bits{};
    std::uint64_t timestamp_period_ps{};

    std::uint32_t memory_type_count{};
    std::uint32_t device_local_memory_type_count{};
    std::uint32_t host_visible_coherent_memory_type_count{};
    std::uint32_t host_visible_cached_memory_type_count{};
    std::uint64_t device_local_heap_bytes{};
    std::uint64_t host_visible_heap_bytes{};

    std::uint32_t max_memory_allocation_count{};
    std::uint32_t max_compute_workgroup_invocations{};
    std::uint32_t max_image_dimension_2d{};
    std::uint64_t non_coherent_atom_size{};
    std::uint64_t buffer_image_granularity{};
    std::uint64_t optimal_buffer_copy_offset_alignment{};
    std::uint64_t optimal_buffer_copy_row_pitch_alignment{};

    // Startup microbenchmark values are diagnostics only. They are intentionally excluded from
    // the pipeline policy hash because device load can move these measurements between runs.
    std::uint64_t empty_submit_ns{};
    std::uint64_t copy_64k_ns{};
    std::uint64_t copy_1m_ns{};
    std::uint64_t copy_4m_ns{};

    bool timestamp_timing_validated{};

    // Diagnostics only: actual execution result and output validation for depth comparison.
    std::uint32_t depth_compare_probe_cases{};
    std::uint32_t depth_compare_probe_failures{};

    // The transfer suite owns the other fields, but runs after the independent depth probes.
    void ResetTransferMeasurements() noexcept {
        const auto depth = *this;
        *this = {};
        r32_sampled_image = depth.r32_sampled_image;
        r32_dref_sample = depth.r32_dref_sample;
        r32_compare_non_dref = depth.r32_compare_non_dref;
        r32_compare_dref = depth.r32_compare_dref;
        d32_compare_dref = depth.d32_compare_dref;
        mutable_r32_d32_view = depth.mutable_r32_d32_view;
        depth_compare_probe_cases = depth.depth_compare_probe_cases;
        depth_compare_probe_failures = depth.depth_compare_probe_failures;
    }
};

struct VulkanDevicePolicy {
    VulkanDeviceIdentity identity;
    VulkanCapabilitySnapshot capabilities;
    XclipseHardwareProfile xclipse;
    XclipseOptimizationProbeResults optimization_probes;
    bool use_xclipse_sync_policy{};
    bool use_xclipse_bcn_gpu_decode{};
    bool use_xclipse_bptc_gpu_decode{};
    bool use_xclipse_bc6_gpu_decode{};
    bool use_xclipse_bc7_gpu_decode{};
    bool use_xclipse_subgroup_size_control{};
    std::uint64_t policy_hash{};
};

[[nodiscard]] XclipseHardwareProfile DetectXclipseHardware(const VulkanDeviceIdentity& identity);
void UpdateXclipseSynchronizationPolicy(VulkanDevicePolicy& policy, bool setting_enabled) noexcept;

[[nodiscard]] bool CanUseXclipseR32DrefEmulation(const VulkanDevicePolicy& policy) noexcept;
void UpdateXclipseBcnDecodePolicy(VulkanDevicePolicy& policy, bool setting_enabled) noexcept;
void UpdateXclipseBptcDecodePolicy(VulkanDevicePolicy& policy, bool setting_enabled) noexcept;
void UpdateXclipseSubgroupSizePolicy(VulkanDevicePolicy& policy, bool setting_enabled) noexcept;
[[nodiscard]] bool IsXclipseSubgroupSizeValidated(const VulkanDevicePolicy& policy,
                                                  std::uint32_t subgroup_size) noexcept;
[[nodiscard]] bool CanRequireXclipseSubgroupSize(const VulkanDevicePolicy& policy,
                                                std::uint32_t subgroup_size,
                                                bool subgroup_size_control_enabled,
                                                std::uint32_t required_stage_mask,
                                                std::uint32_t requested_stage_mask) noexcept;
[[nodiscard]] std::uint64_t ComputeVulkanPolicyHash(const VulkanDevicePolicy& policy) noexcept;

} // namespace Vulkan
