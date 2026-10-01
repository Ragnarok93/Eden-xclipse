// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "bc_decoder.h"

namespace {

int SignedByte(std::uint8_t value) {
    return value >= 128 ? static_cast<int>(value) - 256 : static_cast<int>(value);
}

std::uint64_t Load64(const std::uint8_t* src) {
    std::uint64_t value{};
    for (unsigned i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(src[i]) << (i * 8);
    }
    return value;
}

std::uint8_t DecodeChannel(const std::uint8_t* block, unsigned texel, bool is_signed) {
    const std::uint64_t bits = Load64(block);
    const int endpoint0 = is_signed ? SignedByte(block[0]) : static_cast<int>(block[0]);
    const int endpoint1 = is_signed ? SignedByte(block[1]) : static_cast<int>(block[1]);
    const unsigned index = static_cast<unsigned>((bits >> (16 + texel * 3)) & 7ULL);

    int value{};
    if (index == 0) {
        value = endpoint0;
    } else if (index == 1) {
        value = endpoint1;
    } else if (endpoint0 > endpoint1) {
        const int i = static_cast<int>(index);
        value = ((8 - i) * endpoint0 + (i - 1) * endpoint1) / 7;
    } else if (index < 6) {
        const int i = static_cast<int>(index);
        value = ((6 - i) * endpoint0 + (i - 1) * endpoint1) / 5;
    } else if (index == 6) {
        value = is_signed ? -128 : 0;
    } else {
        value = is_signed ? 127 : 255;
    }
    return static_cast<std::uint8_t>(value);
}

std::array<std::uint8_t, 16> MirrorBc4(const std::array<std::uint8_t, 8>& block,
                                       bool is_signed) {
    std::array<std::uint8_t, 16> result{};
    for (unsigned texel = 0; texel < result.size(); ++texel) {
        result[texel] = DecodeChannel(block.data(), texel, is_signed);
    }
    return result;
}

std::array<std::uint8_t, 32> MirrorBc5(const std::array<std::uint8_t, 16>& block,
                                       bool is_signed) {
    std::array<std::uint8_t, 32> result{};
    for (unsigned texel = 0; texel < 16; ++texel) {
        result[texel * 2] = DecodeChannel(block.data(), texel, is_signed);
        result[texel * 2 + 1] = DecodeChannel(block.data() + 8, texel, is_signed);
    }
    return result;
}

} // namespace

TEST_CASE("Xclipse BC4 shader arithmetic matches Eden CPU decoder", "[video_core][bcn]") {
    constexpr std::array blocks{
        std::array<std::uint8_t, 8>{255, 0, 0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA},
        std::array<std::uint8_t, 8>{0, 255, 0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA},
        std::array<std::uint8_t, 8>{0x80, 0x7f, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00},
        std::array<std::uint8_t, 8>{0x7f, 0x80, 0x24, 0x49, 0x92, 0x24, 0x49, 0x92},
    };

    for (const bool is_signed : {false, true}) {
        for (const auto& block : blocks) {
            std::array<std::uint8_t, 16> cpu{};
            bcn::DecodeBc4(block.data(), cpu.data(), 0, 0, 4, 4, is_signed);
            REQUIRE(MirrorBc4(block, is_signed) == cpu);
        }
    }
}

TEST_CASE("Xclipse BC5 shader arithmetic matches Eden CPU decoder", "[video_core][bcn]") {
    constexpr std::array blocks{
        std::array<std::uint8_t, 16>{
            255, 0, 0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA,
            0, 255, 0x24, 0x49, 0x92, 0x24, 0x49, 0x92,
        },
        std::array<std::uint8_t, 16>{
            0x80, 0x7f, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
            0x7f, 0x80, 0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA,
        },
    };

    for (const bool is_signed : {false, true}) {
        for (const auto& block : blocks) {
            std::array<std::uint8_t, 32> cpu{};
            bcn::DecodeBc5(block.data(), cpu.data(), 0, 0, 4, 4, is_signed);
            REQUIRE(MirrorBc5(block, is_signed) == cpu);
        }
    }
}
