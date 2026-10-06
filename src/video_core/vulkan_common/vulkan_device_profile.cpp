// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/vulkan_device_profile.h"

#include <cctype>
#include <type_traits>

namespace Vulkan {
namespace {

constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t FnvPrime = 1099511628211ULL;

bool EqualAsciiInsensitive(char lhs, char rhs) {
    return std::tolower(static_cast<unsigned char>(lhs)) ==
           std::tolower(static_cast<unsigned char>(rhs));
}

std::size_t FindAsciiInsensitive(std::string_view text, std::string_view token) {
    if (token.empty() || token.size() > text.size()) {
        return std::string_view::npos;
    }
    for (std::size_t offset = 0; offset + token.size() <= text.size(); ++offset) {
        bool match = true;
        for (std::size_t index = 0; index < token.size(); ++index) {
            if (!EqualAsciiInsensitive(text[offset + index], token[index])) {
                match = false;
                break;
            }
        }
        if (match) {
            return offset;
        }
    }
    return std::string_view::npos;
}

std::uint32_t ParseModelAfterXclipse(std::string_view name, std::size_t token_offset) {
    constexpr std::string_view token{"xclipse"};
    std::size_t cursor = token_offset + token.size();
    while (cursor < name.size() && !std::isdigit(static_cast<unsigned char>(name[cursor]))) {
        ++cursor;
    }
    if (cursor == name.size()) {
        return 0;
    }

    std::uint64_t value = 0;
    while (cursor < name.size() && std::isdigit(static_cast<unsigned char>(name[cursor]))) {
        value = value * 10 + static_cast<unsigned>(name[cursor] - '0');
        if (value > UINT32_MAX) {
            return 0;
        }
        ++cursor;
    }
    return static_cast<std::uint32_t>(value);
}

class StableHash {
public:
    void Add(std::string_view value) noexcept {
        AddIntegral(static_cast<std::uint64_t>(value.size()));
        for (const unsigned char ch : value) {
            AddByte(ch);
        }
    }

    template <typename T>
    void AddIntegral(T value) noexcept {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
        if constexpr (std::is_enum_v<T>) {
            using Raw = std::underlying_type_t<T>;
            AddUnsigned(static_cast<std::make_unsigned_t<Raw>>(static_cast<Raw>(value)));
        } else if constexpr (std::is_same_v<T, bool>) {
            AddByte(value ? 1U : 0U);
        } else {
            AddUnsigned(static_cast<std::make_unsigned_t<T>>(value));
        }
    }

    template <std::size_t N>
    void Add(const std::array<std::uint8_t, N>& value) noexcept {
        for (const auto byte : value) {
            AddByte(byte);
        }
    }

    [[nodiscard]] std::uint64_t Value() const noexcept {
        return value;
    }

private:
    template <typename T>
    void AddUnsigned(T value) noexcept {
        static_assert(std::is_unsigned_v<T>);
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            AddByte(static_cast<std::uint8_t>((value >> (i * 8)) & 0xffU));
        }
    }

    void AddByte(std::uint8_t byte) noexcept {
        value ^= byte;
        value *= FnvPrime;
    }

    std::uint64_t value{FnvOffset};
};

void HashFormat(StableHash& hash, const FormatCapabilitySnapshot& format) noexcept {
    hash.AddIntegral(format.image_create);
    hash.AddIntegral(format.sampled);
    hash.AddIntegral(format.linear_filter);
    hash.AddIntegral(format.storage_image);
    hash.AddIntegral(format.transfer_src);
    hash.AddIntegral(format.transfer_dst);
    hash.AddIntegral(format.blit_src);
    hash.AddIntegral(format.blit_dst);
}

} // namespace

XclipseHardwareProfile DetectXclipseHardware(const VulkanDeviceIdentity& identity) {
    XclipseHardwareProfile profile{};
    const std::size_t token = FindAsciiInsensitive(identity.device_name, "xclipse");
    if (token == std::string_view::npos) {
        return profile;
    }

    constexpr std::uint32_t SamsungVendorId = 0x144D;
    constexpr std::uint32_t ArmVendorId = 0x13B5;
    const bool vendor_corroborates =
        identity.vendor_id == SamsungVendorId || identity.vendor_id == ArmVendorId;
    const bool driver_corroborates =
        FindAsciiInsensitive(identity.driver_name, "samsung") != std::string_view::npos ||
        FindAsciiInsensitive(identity.driver_name, "sgpu") != std::string_view::npos;
    const bool soc_corroborates =
        FindAsciiInsensitive(identity.soc_model, "exynos") != std::string_view::npos ||
        FindAsciiInsensitive(identity.soc_model, "s5e") != std::string_view::npos;
    if (!vendor_corroborates && !driver_corroborates && !soc_corroborates) {
        return profile;
    }

    profile.detected = true;
    profile.model = ParseModelAfterXclipse(identity.device_name, token);
    return profile;
}

void UpdateXclipseSynchronizationPolicy(VulkanDevicePolicy& policy,
                                        bool setting_enabled) noexcept {
    policy.xclipse.synchronization2_validated =
        policy.capabilities.synchronization2 == CapabilityState::Validated;
    policy.use_xclipse_sync_policy = policy.xclipse.detected && setting_enabled &&
                                     policy.xclipse.synchronization2_validated;
}

void UpdateXclipseBcnDecodePolicy(VulkanDevicePolicy& policy, bool setting_enabled) noexcept {
    policy.use_xclipse_bcn_gpu_decode =
        policy.xclipse.detected && setting_enabled && policy.xclipse.rgtc_gpu_decode_validated;
}

void UpdateXclipseBptcDecodePolicy(VulkanDevicePolicy& policy, bool setting_enabled) noexcept {
    policy.use_xclipse_bptc_gpu_decode =
        policy.xclipse.detected && setting_enabled && policy.xclipse.bptc_gpu_decode_capable;
}

void UpdateXclipseSubgroupSizePolicy(VulkanDevicePolicy& policy, bool setting_enabled) noexcept {
    const std::uint32_t preferred = policy.xclipse.preferred_compute_wave;
    policy.use_xclipse_subgroup_size_control =
        policy.xclipse.detected && setting_enabled &&
        (preferred == 32U || preferred == 64U) &&
        IsXclipseSubgroupSizeValidated(policy, preferred);
}

bool IsXclipseSubgroupSizeValidated(const VulkanDevicePolicy& policy,
                                    std::uint32_t subgroup_size) noexcept {
    if (!policy.xclipse.detected) {
        return false;
    }
    switch (subgroup_size) {
    case 32:
        return policy.xclipse.wave32_validated;
    case 64:
        return policy.xclipse.wave64_validated;
    default:
        return false;
    }
}

bool CanRequireXclipseSubgroupSize(const VulkanDevicePolicy& policy,
                                   std::uint32_t subgroup_size,
                                   bool subgroup_size_control_enabled,
                                   std::uint32_t required_stage_mask,
                                   std::uint32_t requested_stage_mask) noexcept {
    return policy.use_xclipse_subgroup_size_control && subgroup_size_control_enabled &&
           requested_stage_mask != 0 &&
           (required_stage_mask & requested_stage_mask) == requested_stage_mask &&
           IsXclipseSubgroupSizeValidated(policy, subgroup_size);
}

std::uint64_t ComputeVulkanPolicyHash(const VulkanDevicePolicy& policy) noexcept {
    StableHash hash;
    // v10 includes queried alpha-to-one support because it changes generated multisample state.
    // Query-only sparse strictness and descriptor capture/replay sizes stay out of identity until
    // an execution path actually consumes them.
    hash.Add("eden-xclipse-policy-v10");

    const auto& identity = policy.identity;
    hash.Add(identity.device_name);
    hash.Add(identity.driver_name);
    hash.Add(identity.soc_model);
    hash.AddIntegral(identity.vendor_id);
    hash.AddIntegral(identity.device_id);
    hash.AddIntegral(identity.driver_id);
    hash.AddIntegral(identity.driver_version);
    hash.Add(identity.pipeline_cache_uuid);

    const auto& caps = policy.capabilities;
    hash.AddIntegral(caps.timeline);
    hash.AddIntegral(caps.synchronization2);
    hash.AddIntegral(caps.descriptor_buffer);
    hash.AddIntegral(caps.sparse_binding);
    hash.AddIntegral(caps.alpha_to_one);
    hash.AddIntegral(caps.subgroup_ballot);
    hash.AddIntegral(caps.subgroup_shuffle);
    hash.AddIntegral(caps.subgroup_arithmetic);
    hash.AddIntegral(caps.subgroup_quad);
    hash.AddIntegral(caps.required_subgroup_size);
    hash.AddIntegral(caps.compute_2d_invocations);
    hash.AddIntegral(caps.compute_3d_invocations);
    hash.AddIntegral(caps.subgroup_size);
    hash.AddIntegral(caps.subgroup_supported_stages);
    hash.AddIntegral(caps.subgroup_supported_operations);
    hash.AddIntegral(caps.min_subgroup_size);
    hash.AddIntegral(caps.max_subgroup_size);
    hash.AddIntegral(caps.required_subgroup_size_stages);
    for (const auto& format : caps.bcn) {
        HashFormat(hash, format);
    }

    const auto& xclipse = policy.xclipse;
    hash.AddIntegral(xclipse.detected);
    hash.AddIntegral(xclipse.generation);
    hash.AddIntegral(xclipse.model);
    hash.AddIntegral(xclipse.wave32_validated);
    hash.AddIntegral(xclipse.wave64_validated);
    hash.AddIntegral(xclipse.allowed_wave_mask);
    hash.AddIntegral(xclipse.preferred_compute_wave);
    hash.AddIntegral(xclipse.bc1_native);
    hash.AddIntegral(xclipse.bc2_native);
    hash.AddIntegral(xclipse.bc3_native);
    hash.AddIntegral(xclipse.bc4_native);
    hash.AddIntegral(xclipse.bc5_native);
    hash.AddIntegral(xclipse.bc6_native);
    hash.AddIntegral(xclipse.bc7_native);
    hash.AddIntegral(xclipse.descriptor_buffer_validated);
    hash.AddIntegral(xclipse.descriptor_buffer_image_validated);
    hash.AddIntegral(xclipse.sparse_binding_validated);
    hash.AddIntegral(xclipse.synchronization2_validated);
    hash.AddIntegral(xclipse.rgtc_gpu_decode_validated);
    hash.AddIntegral(xclipse.bptc_gpu_decode_capable);

    const auto& probes = policy.optimization_probes;
    hash.AddIntegral(probes.timestamp_queries);
    hash.AddIntegral(probes.empty_queue_submit);
    hash.AddIntegral(probes.buffer_transfer);
    hash.AddIntegral(probes.image_transfer);
    hash.AddIntegral(probes.storage_image_create);
    hash.AddIntegral(probes.r32_sampled_image);
    hash.AddIntegral(probes.r32_dref_sample);
    hash.AddIntegral(probes.r32_compare_non_dref);
    hash.AddIntegral(probes.r32_compare_dref);
    hash.AddIntegral(probes.d32_compare_dref);
    hash.AddIntegral(probes.mutable_r32_d32_view);
    hash.AddIntegral(probes.queue_family_count);
    hash.AddIntegral(probes.graphics_queue_count);
    hash.AddIntegral(probes.dedicated_compute_queue_count);
    hash.AddIntegral(probes.dedicated_transfer_queue_count);
    hash.AddIntegral(probes.timestamp_valid_bits);
    hash.AddIntegral(probes.timestamp_period_ps);
    hash.AddIntegral(probes.memory_type_count);
    hash.AddIntegral(probes.device_local_memory_type_count);
    hash.AddIntegral(probes.host_visible_coherent_memory_type_count);
    hash.AddIntegral(probes.host_visible_cached_memory_type_count);
    hash.AddIntegral(probes.device_local_heap_bytes);
    hash.AddIntegral(probes.host_visible_heap_bytes);
    hash.AddIntegral(probes.max_memory_allocation_count);
    hash.AddIntegral(probes.max_compute_workgroup_invocations);
    hash.AddIntegral(probes.max_image_dimension_2d);
    hash.AddIntegral(probes.non_coherent_atom_size);
    hash.AddIntegral(probes.buffer_image_granularity);
    hash.AddIntegral(probes.optimal_buffer_copy_offset_alignment);
    hash.AddIntegral(probes.optimal_buffer_copy_row_pitch_alignment);
    // Timing results are diagnostic only and intentionally excluded from the policy identity.

    hash.AddIntegral(policy.use_xclipse_sync_policy);
    hash.AddIntegral(policy.use_xclipse_bcn_gpu_decode);
    hash.AddIntegral(policy.use_xclipse_bptc_gpu_decode);
    hash.AddIntegral(policy.use_xclipse_subgroup_size_control);

    return hash.Value();
}

} // namespace Vulkan
