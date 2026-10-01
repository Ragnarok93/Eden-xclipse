// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/vulkan_common/vulkan_device_profile.h"

TEST_CASE("VulkanDeviceProfile: Xclipse detection requires device-name evidence", "[video_core]") {
    Vulkan::VulkanDeviceIdentity xclipse{};
    xclipse.device_name = "Samsung Xclipse 940";
    xclipse.vendor_id = 0x144D;

    const auto profile = Vulkan::DetectXclipseHardware(xclipse);
    REQUIRE(profile.detected);
    REQUIRE(profile.model == 940);

    Vulkan::VulkanDeviceIdentity vendor_only{};
    vendor_only.device_name = "Mali-G715";
    vendor_only.vendor_id = 0x144D;
    REQUIRE_FALSE(Vulkan::DetectXclipseHardware(vendor_only).detected);

    Vulkan::VulkanDeviceIdentity name_only{};
    name_only.device_name = "Xclipse 940";
    REQUIRE_FALSE(Vulkan::DetectXclipseHardware(name_only).detected);
}

TEST_CASE("VulkanDeviceProfile: Xclipse name matching is case insensitive", "[video_core]") {
    Vulkan::VulkanDeviceIdentity identity{};
    identity.device_name = "Samsung xClIpSe-940";
    identity.vendor_id = 0x13B5;

    const auto profile = Vulkan::DetectXclipseHardware(identity);
    REQUIRE(profile.detected);
    REQUIRE(profile.model == 940);
}

TEST_CASE("VulkanDeviceProfile: policy hash includes driver and capability identity", "[video_core]") {
    Vulkan::VulkanDevicePolicy policy{};
    policy.identity.device_name = "Samsung Xclipse 940";
    policy.identity.vendor_id = 0x144D;
    policy.identity.driver_version = 100;
    policy.capabilities.timeline = Vulkan::CapabilityState::Advertised;
    policy.capabilities.synchronization2 = Vulkan::CapabilityState::Advertised;
    policy.xclipse = Vulkan::DetectXclipseHardware(policy.identity);

    const auto baseline = Vulkan::ComputeVulkanPolicyHash(policy);

    policy.identity.driver_version = 101;
    REQUIRE(Vulkan::ComputeVulkanPolicyHash(policy) != baseline);

    policy.identity.driver_version = 100;
    policy.capabilities.timeline = Vulkan::CapabilityState::Validated;
    REQUIRE(Vulkan::ComputeVulkanPolicyHash(policy) != baseline);

    policy.capabilities.timeline = Vulkan::CapabilityState::Advertised;
    policy.capabilities.bcn[static_cast<std::size_t>(Vulkan::BcnFormat::BC7_UNORM)].image_create =
        Vulkan::CapabilityState::Validated;
    REQUIRE(Vulkan::ComputeVulkanPolicyHash(policy) != baseline);

    policy.capabilities.bcn[static_cast<std::size_t>(Vulkan::BcnFormat::BC7_UNORM)].image_create =
        Vulkan::CapabilityState::Unsupported;
    policy.use_xclipse_sync_policy = true;
    REQUIRE(Vulkan::ComputeVulkanPolicyHash(policy) != baseline);

    policy.use_xclipse_sync_policy = false;
    policy.allow_unvalidated_descriptor_buffer = true;
    REQUIRE(Vulkan::ComputeVulkanPolicyHash(policy) != baseline);
}
