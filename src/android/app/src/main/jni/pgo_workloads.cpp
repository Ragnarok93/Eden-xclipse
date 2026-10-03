// SPDX-License-Identifier: GPL-3.0-or-later
#include "pgo_workloads.h"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <vector>

#include "common/dynamic_library.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "video_core/texture_cache/decode_bc.h"
#include "video_core/textures/decoders.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_instance.h"

namespace AndroidPgo {
namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TextureRoundTrips() {
    for (u32 bpp : {1U, 2U, 4U, 8U, 16U}) {
        for (u32 width : {32U, 127U, 256U}) {
            for (u32 block_height : {0U, 2U, 4U}) {
                constexpr u32 height = 65;
                std::vector<u8> linear(static_cast<size_t>(width) * height * bpp);
                std::vector<u8> restored(linear.size());
                std::vector<u8> tiled(Tegra::Texture::CalculateSize(
                    true, bpp, width, height, 1, block_height, 0));
                for (size_t index = 0; index < linear.size(); ++index) {
                    linear[index] = static_cast<u8>((index * 37 + width) & 255);
                }
                for (unsigned iteration = 0; iteration < 12; ++iteration) {
                    Tegra::Texture::SwizzleTexture(tiled, linear, bpp, width, height, 1,
                                                   block_height, 0);
                    Tegra::Texture::UnswizzleTexture(restored, tiled, bpp, width, height, 1,
                                                     block_height, 0);
                    Check(restored == linear, "Texture round-trip mismatch");
                }
            }
        }
    }
}

void DecodeTextures() {
    using VideoCore::Surface::PixelFormat;
    constexpr std::array formats{
        PixelFormat::BC1_RGBA_UNORM, PixelFormat::BC2_UNORM, PixelFormat::BC3_UNORM,
        PixelFormat::BC4_UNORM, PixelFormat::BC4_SNORM, PixelFormat::BC5_UNORM,
        PixelFormat::BC5_SNORM, PixelFormat::BC6H_UFLOAT, PixelFormat::BC6H_SFLOAT,
        PixelFormat::BC7_UNORM,
    };
    // Legal zero-valued blocks, with a valid BC7 mode bit. Repeated mixed endpoint
    // BC1/2/3/4/5 fixtures exercise both interpolation branches without game assets.
    for (auto format : formats) {
        constexpr u32 width = 128;
        constexpr u32 height = 128;
        const size_t block_size = (format == PixelFormat::BC1_RGBA_UNORM ||
                                  format == PixelFormat::BC4_UNORM ||
                                  format == PixelFormat::BC4_SNORM) ? 8 : 16;
        std::vector<u8> input(width * height / 16 * block_size, 0);
        const auto bpp = VideoCommon::ConvertedBytesPerBlock(format);
        std::vector<u8> output(static_cast<size_t>(width) * height * bpp);
        VideoCommon::BufferImageCopy copy{};
        copy.buffer_row_length = width;
        copy.image_subresource.num_layers = 1;
        copy.image_extent = {width, height, 1};
        if (format == PixelFormat::BC7_UNORM) {
            for (size_t offset = 0; offset < input.size(); offset += 16) {
                input[offset] = 64; // BC7 mode 6
            }
        }
        VideoCommon::DecompressBCn(input, output, copy, format);
        // Independently known result: zero endpoints decode to black in every
        // format. BC4/5 have no alpha, BC6 stores three half-float channels.
        const u32 color_bytes = bpp == 8 ? 6 : std::min(bpp, 3U);
        for (size_t pixel = 0; pixel < output.size(); pixel += bpp) {
            for (u32 channel = 0; channel < color_bytes; ++channel) {
                Check(output[pixel + channel] == 0, "Zero-endpoint BCn fixture is not black");
            }
        }
        const auto expected = output;
        for (unsigned iteration = 0; iteration < 12; ++iteration) {
            std::fill(output.begin(), output.end(), 0xCD);
            VideoCommon::DecompressBCn(input, output, copy, format);
            Check(output == expected, "BCn decode was not deterministic");
        }
        if (format != PixelFormat::BC6H_UFLOAT && format != PixelFormat::BC6H_SFLOAT &&
            format != PixelFormat::BC7_UNORM) {
            for (size_t offset = 0; offset < input.size(); offset += block_size) {
                input[offset] = static_cast<u8>((offset / block_size) & 255);
                input[offset + 1] = static_cast<u8>(255 - input[offset]);
                input[offset + 2] = 0x88;
                input[offset + 3] = 0xC6;
            }
            for (unsigned iteration = 0; iteration < 12; ++iteration) {
                VideoCommon::DecompressBCn(input, output, copy, format);
            }
        }
    }
}

std::vector<u32> CompileShader(u32 variant) {
    Shader::ObjectPool<Shader::IR::Inst> pool;
    Shader::IR::Block block{pool};
    Shader::IR::IREmitter ir{block};
    Shader::IR::Program program{};
    program.stage = Shader::Stage::Compute;
    program.workgroup_size = {32, 1, 1};
    program.local_memory_size = 16;
    program.info.uses_local_memory = true;
    program.info.uses_local_invocation_id = true;
    program.blocks.push_back(&block);
    program.post_order_blocks.push_back(&block);
    Shader::IR::AbstractSyntaxNode body{};
    body.type = Shader::IR::AbstractSyntaxNode::Type::Block;
    body.data.block = &block;
    program.syntax_list.push_back(body);
    Shader::IR::AbstractSyntaxNode end{};
    end.type = Shader::IR::AbstractSyntaxNode::Type::Return;
    program.syntax_list.push_back(end);
    ir.Prologue();
    auto value = ir.LocalInvocationIdX();
    for (u32 i = 0; i < 8 + variant; ++i) {
        value = Shader::IR::U32{ir.IAdd(value, ir.Imm32(i + variant))};
        value = ir.BitwiseXor(value, ir.Imm32(0x9e3779b9U + i));
        value = ir.IMul(value, ir.Imm32(3U));
    }
    ir.WriteLocal(ir.Imm32(0U), value);
    ir.Epilogue();
    Shader::Optimization::IdentityRemovalPass(program);
    Shader::Optimization::DeadCodeEliminationPass(program);
    Shader::Optimization::VerificationPass(program);
    const auto words = Shader::Backend::SPIRV::EmitSPIRV({}, program);
    Check(words.size() > 5 && words[0] == 0x07230203U, "Invalid SPIR-V header");
    return words;
}

std::string VulkanPipelines() {
    // Dedicated process, system driver, no surface, no guest state, no queue work.
    // Train Eden's actual capability and Vulkan wrapper paths without a swapchain.
    Common::DynamicLibrary library{"libvulkan.so"};
    Check(library.IsOpen(), "System Vulkan library unavailable");
    Vulkan::vk::InstanceDispatch dispatch{};
    auto instance = Vulkan::CreateInstance(library, dispatch, VK_API_VERSION_1_1);
    const auto physical = instance.EnumeratePhysicalDevices();
    Check(!physical.empty(), "No Vulkan device");
    Vulkan::Device device{*instance, Vulkan::vk::PhysicalDevice{physical.front(), dispatch},
                          VK_NULL_HANDLE, dispatch};
    const auto& logical = device.GetLogical();
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    auto layout = logical.CreatePipelineLayout(layout_info);
    VkPipelineCacheCreateInfo cache_info{};
    cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    auto cache = logical.CreatePipelineCache(cache_info);
    for (u32 variant = 0; variant < 16; ++variant) {
        const auto words = CompileShader(variant);
        VkShaderModuleCreateInfo shader_info{};
        shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.codeSize = words.size() * sizeof(u32);
        shader_info.pCode = words.data();
        auto shader = logical.CreateShaderModule(shader_info);
        VkComputePipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = *shader;
        info.stage.pName = "main";
        info.layout = *layout;
        // Cold creation followed by reuse of the same private cache.
        auto cold = logical.CreateComputePipeline(info);
        auto warm = logical.CreateComputePipeline(info, *cache);
        auto repeated = logical.CreateComputePipeline(info, *cache);
    }
    return std::string{device.GetModelName()} + "; " + device.GetDriverName() +
           "; policy=" + std::to_string(device.GetDevicePolicy().policy_hash);
}
} // namespace

std::string RunWorkload(int stage) {
    switch (stage) {
    case 0:
        TextureRoundTrips();
        return "45 texture layouts; 12 checked round trips per layout";
    case 1:
        DecodeTextures();
        return "BC1-BC7 CPU fallback; signed/unsigned fixtures; deterministic output";
    case 2:
        for (u32 repeat = 0; repeat < 8; ++repeat) {
            for (u32 variant = 0; variant < 16; ++variant) {
                (void)CompileShader(variant);
            }
        }
        return "128 compute IR optimization and SPIR-V compilations";
    case 3:
        return VulkanPipelines();
    default:
        throw std::runtime_error("Unknown training stage");
    }
}
} // namespace AndroidPgo
