// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/vulkan_common/xclipse_bcn_repetition.h"

TEST_CASE("Xclipse BC repetition separates address shape and content reuse", "[video_core][bcn]") {
    Vulkan::XclipseBcnRepetitionTracker tracker;

    const Vulkan::XclipseBcnObservation first{
        .guest_address = 0x1000,
        .destination_id = 1,
        .format = 10,
        .width = 256,
        .height = 256,
        .depth = 1,
        .level = 0,
        .layer = 0,
        .content_hash = 0xabc,
        .compressed_bytes = 4096,
    };
    tracker.Record(first);

    auto same_address = first;
    same_address.content_hash = 0xdef;
    tracker.Record(same_address);

    auto same_content_elsewhere = first;
    same_content_elsewhere.guest_address = 0x9000;
    same_content_elsewhere.destination_id = 2;
    tracker.Record(same_content_elsewhere);

    const auto snapshot = tracker.Snapshot();
    REQUIRE(snapshot.observations == 3);
    REQUIRE(snapshot.compressed_bytes_hashed == 12288);
    REQUIRE(snapshot.same_address_repeats == 1);
    REQUIRE(snapshot.same_shape_repeats == 2);
    REQUIRE(snapshot.same_content_repeats == 1);
    REQUIRE(snapshot.unique_content == 2);
    REQUIRE(snapshot.hottest_content_observations == 2);
}

TEST_CASE("Xclipse BC repetition tracks destination churn separately", "[video_core][bcn]") {
    Vulkan::XclipseBcnRepetitionTracker tracker;
    Vulkan::XclipseBcnObservation observation{
        .guest_address = 0x2000,
        .destination_id = 4,
        .format = 12,
        .width = 128,
        .height = 64,
        .depth = 1,
        .level = 2,
        .layer = 1,
        .content_hash = 0x1234,
        .compressed_bytes = 1024,
    };
    tracker.Record(observation);
    observation.destination_id = 8;
    tracker.Record(observation);

    const auto snapshot = tracker.Snapshot();
    REQUIRE(snapshot.same_address_repeats == 1);
    REQUIRE(snapshot.destination_changes == 1);
    REQUIRE(snapshot.same_content_repeats == 1);
}
