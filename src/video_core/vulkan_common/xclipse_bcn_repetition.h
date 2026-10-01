// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <span>

#include "common/common_types.h"
#include "common/container/unordered_map.h"
#include "common/container/unordered_set.h"

namespace Vulkan {

struct XclipseBcnObservation {
    u64 guest_address{};
    u64 destination_id{};
    u32 format{};
    u32 width{};
    u32 height{};
    u32 depth{};
    u32 level{};
    u32 layer{};
    u64 content_hash{};
    size_t compressed_bytes{};
};

struct XclipseBcnRepetitionSnapshot {
    u64 observations{};
    u64 compressed_bytes_hashed{};
    u64 same_address_repeats{};
    u64 same_shape_repeats{};
    u64 same_content_repeats{};
    u64 unique_content{};
    u64 hottest_content_observations{};
    u64 destination_changes{};
    u64 dropped_unique_keys{};
};

class XclipseBcnRepetitionTracker {
public:
    static constexpr size_t MaxUniqueKeys = 65536;

    void Record(const XclipseBcnObservation& observation);

    [[nodiscard]] XclipseBcnRepetitionSnapshot Snapshot() const noexcept {
        return snapshot;
    }

private:
    struct AddressKey {
        u64 guest_address{};
        u32 format{};
        u32 level{};
        u32 layer{};

        bool operator==(const AddressKey&) const noexcept = default;
    };

    struct ShapeKey {
        u32 format{};
        u32 width{};
        u32 height{};
        u32 depth{};
        u32 level{};

        bool operator==(const ShapeKey&) const noexcept = default;
    };

    struct ContentKey {
        u64 content_hash{};
        u32 format{};
        u32 width{};
        u32 height{};
        u32 depth{};

        bool operator==(const ContentKey&) const noexcept = default;
    };

    struct AddressHash {
        size_t operator()(const AddressKey& key) const noexcept;
    };
    struct ShapeHash {
        size_t operator()(const ShapeKey& key) const noexcept;
    };
    struct ContentHash {
        size_t operator()(const ContentKey& key) const noexcept;
    };

    struct AddressState {
        u64 destination_id{};
        u64 observations{};
    };

    template <typename Set, typename Key>
    bool InsertBounded(Set& set, const Key& key);

    ::Common::unordered_map<AddressKey, AddressState, AddressHash> addresses;
    ::Common::unordered_map<ShapeKey, u64, ShapeHash> shapes;
    ::Common::unordered_map<ContentKey, u64, ContentHash> contents;
    XclipseBcnRepetitionSnapshot snapshot{};
};

} // namespace Vulkan
