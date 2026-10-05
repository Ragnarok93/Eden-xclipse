// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/xclipse_depth_comparison_probes.h"

#include <array>
#include <bit>
#include <cmath>
#include <optional>

#include "common/common_types.h"
#include "common/logging.h"
#include "video_core/host_shaders/xclipse_r32_dref_probe_comp_spv.h"
#include "video_core/host_shaders/xclipse_r32_sample_probe_comp_spv.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
namespace {

struct Buffer {
    const vk::DeviceDispatch& dld; VkDevice device{}; VkBuffer handle{}; VkDeviceMemory memory{}; void* mapped{};
    ~Buffer() {
        if (mapped) dld.vkUnmapMemory(device, memory);
        if (handle) dld.vkDestroyBuffer(device, handle, nullptr);
        if (memory) dld.vkFreeMemory(device, memory, nullptr);
    }
};
struct Image {
    const vk::DeviceDispatch& dld; VkDevice device{}; VkImage handle{}; VkDeviceMemory memory{}; VkImageView view{};
    ~Image() {
        if (view) dld.vkDestroyImageView(device, view, nullptr);
        if (handle) dld.vkDestroyImage(device, handle, nullptr);
        if (memory) dld.vkFreeMemory(device, memory, nullptr);
    }
};
struct Sampler { const vk::DeviceDispatch& dld; VkDevice device{}; VkSampler handle{};
    ~Sampler(){ if(handle) dld.vkDestroySampler(device,handle,nullptr); } };
struct Pipeline {
    const vk::DeviceDispatch& dld; VkDevice device{}; VkShaderModule shader{}; VkDescriptorSetLayout layout{};
    VkPipelineLayout pipeline_layout{}; VkPipeline pipeline{}; VkDescriptorPool pool{}; VkDescriptorSet set{};
    ~Pipeline() {
        if(pipeline) dld.vkDestroyPipeline(device,pipeline,nullptr);
        if(pipeline_layout) dld.vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
        if(pool) dld.vkDestroyDescriptorPool(device,pool,nullptr);
        if(layout) dld.vkDestroyDescriptorSetLayout(device,layout,nullptr);
        if(shader) dld.vkDestroyShaderModule(device,shader,nullptr);
    }
};
struct CommandPool { const vk::DeviceDispatch& dld; VkDevice device{}; VkCommandPool handle{};
    ~CommandPool(){ if(handle) dld.vkDestroyCommandPool(device,handle,nullptr); } };
struct Fence { const vk::DeviceDispatch& dld; VkDevice device{}; VkFence handle{};
    ~Fence(){ if(handle) dld.vkDestroyFence(device,handle,nullptr); } };

std::optional<u32> FindMemoryType(const VkPhysicalDeviceMemoryProperties& p, u32 bits,
                                  VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) {
    for (u32 pass = 0; pass < 2; ++pass) {
        const auto wanted = pass ? required : required | preferred;
        for (u32 i = 0; i < p.memoryTypeCount; ++i)
            if ((bits & (1U << i)) && (p.memoryTypes[i].propertyFlags & wanted) == wanted)
                return i;
    }
    return std::nullopt;
}

bool MakeBuffer(const Device& device, VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out) {
    const auto& dld=device.GetDispatchLoader(); const VkDevice dev=*device.GetLogical();
    const auto mem=device.GetPhysical().GetMemoryProperties().memoryProperties;
    const VkBufferCreateInfo ci{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage,
                                .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    if(dld.vkCreateBuffer(dev,&ci,nullptr,&out.handle)!=VK_SUCCESS) return false;
    VkMemoryRequirements req{};
    const VkBufferMemoryRequirementsInfo2 req_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
        .buffer = out.handle,
    };
    VkMemoryRequirements2 req2{.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    dld.vkGetBufferMemoryRequirements2(dev, &req_info, &req2);
    req = req2.memoryRequirements;
    const auto type=FindMemoryType(mem,req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if(!type) return false;
    const VkMemoryAllocateInfo ai{.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                  .allocationSize=req.size,.memoryTypeIndex=*type};
    if(dld.vkAllocateMemory(dev,&ai,nullptr,&out.memory)!=VK_SUCCESS ||
       dld.vkBindBufferMemory(dev,out.handle,out.memory,0)!=VK_SUCCESS) return false;
    return dld.vkMapMemory(dev,out.memory,0,size,0,&out.mapped)==VK_SUCCESS;
}

bool MakeImage(const Device& device, VkFormat format, VkImageCreateFlags flags,
               const VkImageFormatListCreateInfo* list, Image& out) {
    const auto& dld=device.GetDispatchLoader(); const VkDevice dev=*device.GetLogical();
    const auto mem=device.GetPhysical().GetMemoryProperties().memoryProperties;
    const VkImageCreateInfo ci{.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.pNext=list,.flags=flags,
        .imageType=VK_IMAGE_TYPE_2D,.format=format,.extent={1,1,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    if(dld.vkCreateImage(dev,&ci,nullptr,&out.handle)!=VK_SUCCESS) return false;
    VkMemoryRequirements req{}; dld.vkGetImageMemoryRequirements(dev,out.handle,&req);
    const auto type=FindMemoryType(mem,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0);
    if(!type) return false;
    const VkMemoryAllocateInfo ai{.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                  .allocationSize=req.size,.memoryTypeIndex=*type};
    if(dld.vkAllocateMemory(dev,&ai,nullptr,&out.memory)!=VK_SUCCESS ||
       dld.vkBindImageMemory(dev,out.handle,out.memory,0)!=VK_SUCCESS) return false;
    return true;
}

bool MakeView(const Device& device, Image& image, VkFormat format, VkImageAspectFlags aspect) {
    const auto& dld=device.GetDispatchLoader(); const VkDevice dev=*device.GetLogical();
    const VkImageViewCreateInfo ci{.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image.handle,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,
        .subresourceRange={aspect,0,1,0,1}};
    return dld.vkCreateImageView(dev,&ci,nullptr,&image.view)==VK_SUCCESS;
}

bool MakeSampler(const Device& device, bool compare, Sampler& out) {
    const auto& dld=device.GetDispatchLoader(); const VkDevice dev=*device.GetLogical();
    const VkSamplerCreateInfo ci{.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxAnisotropy=1.0f,.compareEnable=compare?VK_TRUE:VK_FALSE,
        .compareOp=VK_COMPARE_OP_LESS,.minLod=0,.maxLod=0,
        .borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK};
    return dld.vkCreateSampler(dev,&ci,nullptr,&out.handle)==VK_SUCCESS;
}

bool MakePipeline(const Device& device, bool dref, const Sampler& sampler,
                  const Image& image, const Buffer& output, Pipeline& out) {
    const auto& dld=device.GetDispatchLoader(); const VkDevice dev=*device.GetLogical();
    const auto* code=dref?XCLIPSE_R32_DREF_PROBE_COMP_SPV:XCLIPSE_R32_SAMPLE_PROBE_COMP_SPV;
    const auto code_size=dref?sizeof(XCLIPSE_R32_DREF_PROBE_COMP_SPV):sizeof(XCLIPSE_R32_SAMPLE_PROBE_COMP_SPV);
    const VkShaderModuleCreateInfo sci{.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                       .codeSize=code_size,.pCode=code};
    if(dld.vkCreateShaderModule(dev,&sci,nullptr,&out.shader)!=VK_SUCCESS) return false;
    const std::array bindings{
        VkDescriptorSetLayoutBinding{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        VkDescriptorSetLayoutBinding{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    const VkDescriptorSetLayoutCreateInfo lci{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                               .bindingCount=2,.pBindings=bindings.data()};
    if(dld.vkCreateDescriptorSetLayout(dev,&lci,nullptr,&out.layout)!=VK_SUCCESS) return false;
    const VkPipelineLayoutCreateInfo plci{.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                          .setLayoutCount=1,.pSetLayouts=&out.layout};
    if(dld.vkCreatePipelineLayout(dev,&plci,nullptr,&out.pipeline_layout)!=VK_SUCCESS) return false;
    const VkPipelineShaderStageCreateInfo stage{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=out.shader,.pName="main"};
    const VkComputePipelineCreateInfo pci{.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                          .stage=stage,.layout=out.pipeline_layout};
    if(dld.vkCreateComputePipelines(dev,VK_NULL_HANDLE,1,&pci,nullptr,&out.pipeline)!=VK_SUCCESS) return false;
    const std::array sizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    const VkDescriptorPoolCreateInfo dpci{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes.data()};
    if(dld.vkCreateDescriptorPool(dev,&dpci,nullptr,&out.pool)!=VK_SUCCESS) return false;
    const VkDescriptorSetAllocateInfo dsa{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=out.pool,.descriptorSetCount=1,.pSetLayouts=&out.layout};
    if(dld.vkAllocateDescriptorSets(dev,&dsa,&out.set)!=VK_SUCCESS) return false;
    const VkDescriptorImageInfo ii{.sampler=sampler.handle,.imageView=image.view,
        .imageLayout=dref?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorBufferInfo bi{.buffer=output.handle,.offset=0,.range=sizeof(u32)};
    const std::array writes{
        VkWriteDescriptorSet{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=out.set,.dstBinding=0,
            .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&ii},
        VkWriteDescriptorSet{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=out.set,.dstBinding=1,
            .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi}};
    dld.vkUpdateDescriptorSets(dev,2,writes.data(),0,nullptr);
    return true;
}

bool Execute(const Device& device, VkFormat image_format, VkImageAspectFlags aspect,
             bool dref, bool compare, bool mutable_view, float expected, CapabilityState& state) {
    const auto& dld=device.GetDispatchLoader(); const VkDevice dev=*device.GetLogical();
    Buffer source{dld,dev}, output{dld,dev};
    if(!MakeBuffer(device,sizeof(u32),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,source) ||
       !MakeBuffer(device,sizeof(u32),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,output))
        return false;
    *static_cast<u32*>(source.mapped)=std::bit_cast<u32>(0.75f);
    *static_cast<u32*>(output.mapped)=0;

    Image image{dld,dev};
    const std::array formats{VK_FORMAT_R32_SFLOAT,VK_FORMAT_D32_SFLOAT};
    const VkImageFormatListCreateInfo list{.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount=2,.pViewFormats=formats.data()};
    const bool mutable_image=mutable_view;
    if(!MakeImage(device,image_format,mutable_image?VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT:0,
                  mutable_image?&list:nullptr,image)) return false;
    const VkFormat view_format=mutable_view?VK_FORMAT_D32_SFLOAT:image_format;
    const VkImageAspectFlags view_aspect=mutable_view?VK_IMAGE_ASPECT_DEPTH_BIT:aspect;
    if(!MakeView(device,image,view_format,view_aspect)) return false;
    Sampler sampler{dld,dev};
    if(!MakeSampler(device,compare,sampler)) return false;
    Pipeline pipeline{dld,dev};
    if(!MakePipeline(device,dref,sampler,image,output,pipeline)) return false;

    CommandPool pool{dld,dev};
    const VkCommandPoolCreateInfo cpci{.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                       .flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                       .queueFamilyIndex=device.GetGraphicsFamily()};
    if(dld.vkCreateCommandPool(dev,&cpci,nullptr,&pool.handle)!=VK_SUCCESS) return false;
    VkCommandBuffer cmd{};
    const VkCommandBufferAllocateInfo cai{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=pool.handle,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    if(dld.vkAllocateCommandBuffers(dev,&cai,&cmd)!=VK_SUCCESS) return false;
    const VkCommandBufferBeginInfo begin{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                         .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if(dld.vkBeginCommandBuffer(cmd,&begin)!=VK_SUCCESS) return false;

    const VkImageMemoryBarrier to_dst{.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image.handle,
        .subresourceRange={aspect,0,1,0,1}};
    dld.vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,0,nullptr,0,nullptr,1,&to_dst);
    const VkBufferImageCopy copy{.imageSubresource={aspect,0,0,1},.imageExtent={1,1,1}};
    dld.vkCmdCopyBufferToImage(cmd,source.handle,image.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);

    const VkImageLayout read_layout=dref?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                        :VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkImageMemoryBarrier to_read{.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
        .oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.newLayout=read_layout,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=image.handle,.subresourceRange={aspect,0,1,0,1}};
    dld.vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,0,nullptr,0,nullptr,1,&to_read);
    dld.vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.pipeline);
    dld.vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.pipeline_layout,0,1,
                                &pipeline.set,0,nullptr);
    dld.vkCmdDispatch(cmd,1,1,1);
    const VkBufferMemoryBarrier to_host{.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .buffer=output.handle,.offset=0,.size=sizeof(u32)};
    dld.vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
                             0,0,nullptr,1,&to_host,0,nullptr);
    if(dld.vkEndCommandBuffer(cmd)!=VK_SUCCESS) return false;

    Fence fence{dld,dev};
    const VkFenceCreateInfo fci{.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if(dld.vkCreateFence(dev,&fci,nullptr,&fence.handle)!=VK_SUCCESS) return false;
    const VkSubmitInfo submit{.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
    if(device.GetGraphicsQueue().Submit(vk::Span<VkSubmitInfo>{submit},fence.handle)!=VK_SUCCESS) return false;
    if(dld.vkWaitForFences(dev,1,&fence.handle,VK_TRUE,1'000'000'000ULL)!=VK_SUCCESS) return false;
    const float actual=std::bit_cast<float>(*static_cast<const u32*>(output.mapped));
    const bool valid=std::isfinite(actual)&&std::fabs(actual-expected)<=0.001f;
    state=valid?CapabilityState::Validated:CapabilityState::Advertised;
    return valid;
}

} // namespace

void RunXclipseDepthComparisonProbes(const Device& device,
                                     XclipseOptimizationProbeResults& results) {
    if(!device.IsXclipse()) return;
    results.r32_sampled_image=CapabilityState::Advertised;
    results.r32_compare_non_dref=CapabilityState::Advertised;
    results.r32_dref_sample=CapabilityState::Advertised;
    results.d32_compare_dref=CapabilityState::Advertised;
    results.mutable_r32_d32_view=CapabilityState::Advertised;
    try {
        (void)Execute(device,VK_FORMAT_R32_SFLOAT,VK_IMAGE_ASPECT_COLOR_BIT,false,false,false,0.75f,
                      results.r32_sampled_image);
        (void)Execute(device,VK_FORMAT_R32_SFLOAT,VK_IMAGE_ASPECT_COLOR_BIT,false,true,false,0.75f,
                      results.r32_compare_non_dref);
        (void)Execute(device,VK_FORMAT_R32_SFLOAT,VK_IMAGE_ASPECT_COLOR_BIT,true,true,false,1.0f,
                      results.r32_dref_sample);
        (void)Execute(device,VK_FORMAT_D32_SFLOAT,VK_IMAGE_ASPECT_DEPTH_BIT,true,true,false,1.0f,
                      results.d32_compare_dref);
        (void)Execute(device,VK_FORMAT_R32_SFLOAT,VK_IMAGE_ASPECT_COLOR_BIT,true,true,true,1.0f,
                      results.mutable_r32_d32_view);
    } catch (const vk::Exception& exception) {
        LOG_WARNING(Render_Vulkan,"XCLIPSE DEPTH PROBE exception: {}",exception.what());
    }
    results.depth_compare_probe_cases=5;
    results.depth_compare_probe_failures=
        (results.r32_sampled_image==CapabilityState::Validated?0:1)+
        (results.r32_compare_non_dref==CapabilityState::Validated?0:1)+
        (results.r32_dref_sample==CapabilityState::Validated?0:1)+
        (results.d32_compare_dref==CapabilityState::Validated?0:1)+
        (results.mutable_r32_d32_view==CapabilityState::Validated?0:1);
    LOG_INFO(Render_Vulkan,
             "XCLIPSE DEPTH PROBES cases={} failures={} r32_sample={} r32_compare={} "
             "r32_dref={} d32_dref={} mutable_r32_d32={}",
             results.depth_compare_probe_cases,results.depth_compare_probe_failures,
             CapabilityStateName(results.r32_sampled_image),
             CapabilityStateName(results.r32_compare_non_dref),
             CapabilityStateName(results.r32_dref_sample),
             CapabilityStateName(results.d32_compare_dref),
             CapabilityStateName(results.mutable_r32_d32_view));
}

} // namespace Vulkan
