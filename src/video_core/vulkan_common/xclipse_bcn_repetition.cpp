// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/xclipse_bcn_repetition.h"

#include <functional>

namespace Vulkan {
namespace {

size_t Mix(size_t seed, u64 value) noexcept {
    constexpr u64 k = 0x9e3779b97f4a7c15ULL;
    const u64 mixed = value + k + (static_cast<u64>(seed) << 6) +
                      (static_cast<u64>(seed) >> 2);
    return seed ^ static_cast<size_t>(mixed);
}

} // namespace

size_t XclipseBcnRepetitionTracker::AddressHash::operator()(const AddressKey& key) const noexcept {
    size_t seed = std::hash<u64>{}(key.guest_address);
    seed = Mix(seed, key.format);
    seed = Mix(seed, key.level);
    seed = Mix(seed, key.layer);
    return seed;
}

size_t XclipseBcnRepetitionTracker::ShapeHash::operator()(const ShapeKey& key) const noexcept {
    size_t seed = std::hash<u32>{}(key.format);
    seed = Mix(seed, key.width);
    seed = Mix(seed, key.height);
    seed = Mix(seed, key.depth);
    seed = Mix(seed, key.level);
    return seed;
}

size_t XclipseBcnRepetitionTracker::ContentHash::operator()(const ContentKey& key) const noexcept {
    size_t seed = std::hash<u64>{}(key.content_hash);
    seed = Mix(seed, key.format);
    seed = Mix(seed, key.width);
    seed = Mix(seed, key.height);
    seed = Mix(seed, key.depth);
    return seed;
}

void XclipseBcnRepetitionTracker::Record(const XclipseBcnObservation& observation) {
    ++snapshot.observations;
    snapshot.compressed_bytes_hashed += observation.compressed_bytes;

    const AddressKey address_key{
        .guest_address = observation.guest_address,
        .format = observation.format,
        .level = observation.level,
        .layer = observation.layer,
    };
    auto address_it = addresses.find(address_key);
    if (address_it != addresses.end()) {
        ++snapshot.same_address_repeats;
        ++address_it->second.observations;
        if (address_it->second.destination_id != observation.destination_id) {
            ++snapshot.destination_changes;
            address_it->second.destination_id = observation.destination_id;
        }
    } else if (addresses.size() < MaxUniqueKeys) {
        addresses.emplace(address_key, AddressState{
            .destination_id = observation.destination_id,
            .observations = 1,
        });
    } else {
        ++snapshot.dropped_unique_keys;
    }

    const ShapeKey shape_key{
        .format = observation.format,
        .width = observation.width,
        .height = observation.height,
        .depth = observation.depth,
        .level = observation.level,
    };
    auto shape_it = shapes.find(shape_key);
    if (shape_it != shapes.end()) {
        ++snapshot.same_shape_repeats;
        ++shape_it->second;
    } else if (shapes.size() < MaxUniqueKeys) {
        shapes.emplace(shape_key, 1);
    } else {
        ++snapshot.dropped_unique_keys;
    }

    const ContentKey content_key{
        .content_hash = observation.content_hash,
        .format = observation.format,
        .width = observation.width,
        .height = observation.height,
        .depth = observation.depth,
    };
    auto content_it = contents.find(content_key);
    if (content_it != contents.end()) {
        ++snapshot.same_content_repeats;
        const u64 count = ++content_it->second;
        snapshot.hottest_content_observations =
            std::max(snapshot.hottest_content_observations, count);
    } else if (contents.size() < MaxUniqueKeys) {
        contents.emplace(content_key, 1);
        ++snapshot.unique_content;
        snapshot.hottest_content_observations =
            std::max<u64>(snapshot.hottest_content_observations, 1);
    } else {
        ++snapshot.dropped_unique_keys;
    }
}

} // namespace Vulkan
