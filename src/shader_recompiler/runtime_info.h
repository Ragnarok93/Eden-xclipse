// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <map>
#include <optional>
#include <vector>

#include "common/common_types.h"
#include "shader_recompiler/varying_state.h"

namespace Shader {

enum class AttributeType : u8 {
    Float,
    SignedInt,
    UnsignedInt,
    SignedScaled,
    UnsignedScaled,
    Disabled,
};

[[nodiscard]] constexpr bool ShouldDeclareFragmentColorOutput(
    AttributeType output_type, bool stores_color, bool force_declaration,
    bool dual_source) noexcept {
    return output_type != AttributeType::Disabled &&
           (stores_color || force_declaration || dual_source);
}

[[nodiscard]] constexpr bool ShouldEmitFragmentColorStore(AttributeType output_type) noexcept {
    return output_type != AttributeType::Disabled;
}

enum class InputTopology {
    Points,
    Lines,
    LinesAdjacency,
    Triangles,
    TrianglesAdjacency,
};

struct InputTopologyVertices {
    static u32 vertices(InputTopology input_topology) {
        switch (input_topology) {
        case InputTopology::Lines:
            return 2;
        case InputTopology::LinesAdjacency:
            return 4;
        case InputTopology::Triangles:
            return 3;
        case InputTopology::TrianglesAdjacency:
            return 6;
        case InputTopology::Points:
        default:
            return 1;
        }
    }
};

enum class CompareFunction {
    Never,
    Less,
    Equal,
    LessThanEqual,
    Greater,
    NotEqual,
    GreaterThanEqual,
    Always,
};

[[nodiscard]] constexpr bool EvaluateDrefCompare(CompareFunction comparison, float reference,
                                                  float sampled) noexcept {
    if (comparison == CompareFunction::Never) {
        return false;
    }
    if (comparison == CompareFunction::Always) {
        return true;
    }

    // The SPIR-V software-DREF path uses ordered floating-point comparisons. Match that
    // guest-visible behavior here: every non-constant comparison is false for NaN operands.
    if (reference != reference || sampled != sampled) {
        return false;
    }

    switch (comparison) {
    case CompareFunction::Less:
        return reference < sampled;
    case CompareFunction::Equal:
        return reference == sampled;
    case CompareFunction::LessThanEqual:
        return reference <= sampled;
    case CompareFunction::Greater:
        return reference > sampled;
    case CompareFunction::NotEqual:
        return reference != sampled;
    case CompareFunction::GreaterThanEqual:
        return reference >= sampled;
    case CompareFunction::Never:
    case CompareFunction::Always:
        break;
    }
    return false;
}

enum class TessPrimitive {
    Isolines,
    Triangles,
    Quads,
};

enum class TessSpacing {
    Equal,
    FractionalOdd,
    FractionalEven,
};

struct TransformFeedbackVarying {
    u32 buffer{};
    u32 stream{};
    u32 stride{};
    u32 offset{};
    u32 components{};
};

struct RuntimeInfo {
    std::array<AttributeType, 32> generic_input_types{};
    VaryingState previous_stage_stores;
    std::map<IR::Attribute, IR::Attribute> previous_stage_legacy_stores_mapping;

    bool convert_depth_mode{};
    bool force_early_z{};

    TessPrimitive tess_primitive{};
    TessSpacing tess_spacing{};
    bool tess_clockwise{};

    InputTopology input_topology{};

    std::optional<float> fixed_state_point_size;
    std::optional<CompareFunction> alpha_test_func;
    float alpha_test_reference{};

    /// Static Y negate value
    bool y_negate{};
    /// Use storage buffers instead of global pointers on GLASM
    bool glasm_use_storage_buffers{};
    /// Emulate R32_FLOAT Dref operations on Xclipse when validated execution probes require it.
    bool xclipse_r32_dref_emulation{};

    /// Transform feedback state for each varying
    std::array<TransformFeedbackVarying, 256> xfb_varyings{};
    u32 xfb_count{0};

    /// Output types for each color attachment
    std::array<AttributeType, 8> color_output_types{};

    /// Dual source blending
    bool dual_source_blend{};
};

} // namespace Shader
