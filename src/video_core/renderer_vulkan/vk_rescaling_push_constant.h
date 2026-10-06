// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "common/common_types.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"

namespace Vulkan {

class RescalingPushConstant {
public:
    explicit RescalingPushConstant() noexcept = default;

    void PushTexture(bool is_rescaled) noexcept {
        *texture_ptr |= is_rescaled ? texture_bit : 0u;
        texture_bit <<= 1u;
        if (texture_bit == 0u) {
            texture_bit = 1u;
            ++texture_ptr;
        }
        ++texture_index;
    }

    void PushImage(bool is_rescaled) noexcept {
        *image_ptr |= is_rescaled ? image_bit : 0u;
        image_bit <<= 1u;
        if (image_bit == 0u) {
            image_bit = 1u;
            ++image_ptr;
        }
    }

    void SetDrefCompareOp(u32 compare_op) noexcept {
        const u32 word_index{
            texture_index / Shader::Backend::SPIRV::DREF_COMPARE_OPS_PER_WORD};
        if (word_index >= dref_compare_ops.size()) {
            return;
        }
        const u32 shift{
            (texture_index % Shader::Backend::SPIRV::DREF_COMPARE_OPS_PER_WORD) * 4};
        const u32 mask{0xFu << shift};
        dref_compare_ops[word_index] =
            (dref_compare_ops[word_index] & ~mask) | ((compare_op & 0x7u) << shift);
    }

    [[nodiscard]] const std::array<u32, Shader::Backend::SPIRV::NUM_DREF_COMPARE_OP_WORDS>&
    DrefCompareOps() const noexcept {
        return dref_compare_ops;
    }

    [[nodiscard]] const std::array<u32,
                                   Shader::Backend::SPIRV::NUM_TEXTURE_AND_IMAGE_SCALING_WORDS>&
    Data() const noexcept {
        return words;
    }

private:
    std::array<u32, Shader::Backend::SPIRV::NUM_TEXTURE_AND_IMAGE_SCALING_WORDS> words{};
    u32* texture_ptr{words.data()};
    u32* image_ptr{words.data() + Shader::Backend::SPIRV::NUM_TEXTURE_SCALING_WORDS};
    u32 texture_bit{1u};
    u32 image_bit{1u};
    u32 texture_index{};
    std::array<u32, Shader::Backend::SPIRV::NUM_DREF_COMPARE_OP_WORDS> dref_compare_ops{};
};

} // namespace Vulkan
