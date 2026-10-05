// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Vulkan {

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
    CapabilityState image_create{CapabilityState::Unsupported};
    CapabilityState sampled{CapabilityState::Unsupported};
    CapabilityState linear_filter{CapabilityState::Unsupported};
    CapabilityState storage_image{CapabilityState::Unsupported};
    CapabilityState transfer_src{CapabilityState::Unsupported};
    CapabilityState transfer_dst{CapabilityState::Unsupported};
    CapabilityState blit_src{CapabilityState::Unsupported};
    CapabilityState blit_dst{CapabilityState::Unsupported};
};

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
    bool sparse_binding_validated{};
    bool synchronization2_validated{};
    bool rgtc_gpu_decode_validated{};
    bool bptc_gpu_decode_capable{};
};

struct VulkanDevicePolicy {
    VulkanDeviceIdentity identity;
    VulkanCapabilitySnapshot capabilities;
    XclipseHardwareProfile xclipse;
    bool use_xclipse_sync_policy{};
    bool use_xclipse_bcn_gpu_decode{};
    bool use_xclipse_bptc_gpu_decode{};
    bool use_xclipse_subgroup_size_control{};
    std::uint64_t policy_hash{};
};

[[nodiscard]] XclipseHardwareProfile DetectXclipseHardware(const VulkanDeviceIdentity& identity);
void UpdateXclipseSynchronizationPolicy(VulkanDevicePolicy& policy, bool setting_enabled) noexcept;
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
