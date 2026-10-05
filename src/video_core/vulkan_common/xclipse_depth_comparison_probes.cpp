#include "video_core/vulkan_common/xclipse_depth_comparison_probes.h"
#include <array>
#include <bit>
#include <cmath>
#include "video_core/host_shaders/xclipse_r32_dref_probe_comp_spv.h"
#include "video_core/host_shaders/xclipse_r32_sample_probe_comp_spv.h"
#include "video_core/vulkan_common/vma.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "common/logging.h"

namespace Vulkan {
namespace {
struct Resource {
    const vk::DeviceDispatch* dld{};
    VkDevice device{};
    VmaAllocator allocator{};
    VkBuffer buffer{};
    VmaAllocation allocation{};
    void* mapped{};
    ~Resource(){ if(allocation) vmaDestroyBuffer(allocator,buffer,allocation); }
};
struct Image {
    const vk::DeviceDispatch* dld{};
    VkDevice device{};
    VmaAllocator allocator{};
    VkImage image{};
    VmaAllocation allocation{};
    VkImageView view{};
    ~Image(){ if(view)dld->vkDestroyImageView(device,view,nullptr); if(allocation)vmaDestroyImage(allocator,image,allocation); }
};
struct Objects {
    const vk::DeviceDispatch* dld{};
    VkDevice device{};
    VkShaderModule shader{};
    VkDescriptorSetLayout layout{};
    VkPipelineLayout pipeline_layout{};
    VkPipeline pipeline{};
    VkDescriptorPool pool{};
    VkDescriptorSet set{};
    VkCommandPool command_pool{};
    VkCommandBuffer command{};
    VkSampler sampler{};
    VkFence fence{};
    ~Objects(){
        if(fence)dld->vkDestroyFence(device,fence,nullptr);
        if(command_pool&&command)dld->vkFreeCommandBuffers(device,command_pool,1,&command);
        if(command_pool)dld->vkDestroyCommandPool(device,command_pool,nullptr);
        if(sampler)dld->vkDestroySampler(device,sampler,nullptr);
        if(pool)dld->vkDestroyDescriptorPool(device,pool,nullptr);
        if(pipeline)dld->vkDestroyPipeline(device,pipeline,nullptr);
        if(pipeline_layout)dld->vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
        if(layout)dld->vkDestroyDescriptorSetLayout(device,layout,nullptr);
        if(shader)dld->vkDestroyShaderModule(device,shader,nullptr);
    }
};
bool Buffer(const Device& d,VkDeviceSize n,VkBufferUsageFlags usage,Resource& r){
    const VkBufferCreateInfo ci{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=n,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo ai{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT,.usage=VMA_MEMORY_USAGE_AUTO_PREFER_HOST,.preferredFlags=VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT};
    VmaAllocationInfo info{};
    if(vmaCreateBuffer(d.GetAllocator(),&ci,&ai,&r.buffer,&r.allocation,&info)!=VK_SUCCESS)return false;
    r.dld=&d.GetDispatchLoader();r.device=*d.GetLogical();r.allocator=d.GetAllocator();r.mapped=info.pMappedData;return r.mapped;
}
bool ImageCreate(const Device& d,VkFormat f,bool mut,Image& r){
    const std::array fmts{VK_FORMAT_R32_SFLOAT,VK_FORMAT_D32_SFLOAT};
    const VkImageFormatListCreateInfo list{.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,.viewFormatCount=2,.pViewFormats=fmts.data()};
    const VkImageCreateInfo ci{.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.pNext=mut?&list:nullptr,.flags=static_cast<VkImageCreateFlags>(mut?VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT:0),.imageType=VK_IMAGE_TYPE_2D,.format=f,.extent={1,1,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo ai{.usage=VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,.preferredFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};
    if(vmaCreateImage(d.GetAllocator(),&ci,&ai,&r.image,&r.allocation,nullptr)!=VK_SUCCESS)return false;
    r.dld=&d.GetDispatchLoader();r.device=*d.GetLogical();r.allocator=d.GetAllocator();
    const VkFormat vf=mut?VK_FORMAT_D32_SFLOAT:f;
    const VkImageAspectFlags aspect=mut?VK_IMAGE_ASPECT_DEPTH_BIT:(f==VK_FORMAT_D32_SFLOAT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT);
    const VkImageViewCreateInfo vi{.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=r.image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=vf,.subresourceRange={aspect,0,1,0,1}};
    return d.GetDispatchLoader().vkCreateImageView(*d.GetLogical(),&vi,nullptr,&r.view)==VK_SUCCESS;
}
bool Execute(const Device& d,VkFormat f,bool dref,bool compare,bool mut,float expected,CapabilityState& state){
    const auto& x=d.GetDispatchLoader();const VkDevice dev=*d.GetLogical();
    VkFormatProperties2 p{.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};VkFormatProperties3 p3{.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};p.pNext=&p3;x.vkGetPhysicalDeviceFormatProperties2(d.GetPhysical(),f,&p);
    const auto feats=p3.optimalTilingFeatures;
    if(!(feats&VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT)|| (dref&&compare&&!(feats&VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT))){state=CapabilityState::Advertised;return true;}
    Resource src{},out{};if(!Buffer(d,4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,src)||!Buffer(d,4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,out))return false;
    *static_cast<uint32_t*>(src.mapped)=std::bit_cast<uint32_t>(.75f);*static_cast<uint32_t*>(out.mapped)=0;
    Image im{};if(!ImageCreate(d,f,mut,im))return false;
    const uint32_t* code=dref?XCLIPSE_R32_DREF_PROBE_COMP_SPV:XCLIPSE_R32_SAMPLE_PROBE_COMP_SPV;
    const size_t size=dref?sizeof(XCLIPSE_R32_DREF_PROBE_COMP_SPV):sizeof(XCLIPSE_R32_SAMPLE_PROBE_COMP_SPV);
    Objects o{.dld=&x,.device=dev};VkShaderModuleCreateInfo sm{.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=size,.pCode=code};if(x.vkCreateShaderModule(dev,&sm,nullptr,&o.shader)!=VK_SUCCESS)return false;
    std::array<VkDescriptorSetLayoutBinding,2> bindings{{{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
    VkDescriptorSetLayoutCreateInfo dl{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings.data()};if(x.vkCreateDescriptorSetLayout(dev,&dl,nullptr,&o.layout)!=VK_SUCCESS)return false;
    VkPipelineLayoutCreateInfo pl{.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&o.layout};if(x.vkCreatePipelineLayout(dev,&pl,nullptr,&o.pipeline_layout)!=VK_SUCCESS)return false;
    VkSamplerCreateInfo si{.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.compareEnable=compare,.compareOp=VK_COMPARE_OP_LESS,.maxLod=0};if(x.vkCreateSampler(dev,&si,nullptr,&o.sampler)!=VK_SUCCESS)return false;
    VkPipelineShaderStageCreateInfo st{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=o.shader,.pName="main"};VkComputePipelineCreateInfo pc{.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage=st,.layout=o.pipeline_layout};if(x.vkCreateComputePipelines(dev,d.StaticPipelineCache(),1,&pc,nullptr,&o.pipeline)!=VK_SUCCESS)return false;
    std::array<VkDescriptorPoolSize,2>ps{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}}};VkDescriptorPoolCreateInfo dp{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=ps.data()};if(x.vkCreateDescriptorPool(dev,&dp,nullptr,&o.pool)!=VK_SUCCESS)return false;VkDescriptorSetAllocateInfo da{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=o.pool,.descriptorSetCount=1,.pSetLayouts=&o.layout};if(x.vkAllocateDescriptorSets(dev,&da,&o.set)!=VK_SUCCESS)return false;
    const VkImageAspectFlags aspect=mut?VK_IMAGE_ASPECT_DEPTH_BIT:(f==VK_FORMAT_D32_SFLOAT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT);const VkImageLayout rl=dref?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;VkDescriptorImageInfo ii{.sampler=o.sampler,.imageView=im.view,.imageLayout=rl};VkDescriptorBufferInfo bi{.buffer=out.buffer,.range=4};std::array<VkWriteDescriptorSet,2>w{{{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=o.set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&ii},{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=o.set,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi}}};x.vkUpdateDescriptorSets(dev,2,w.data(),0,nullptr);
    VkCommandPoolCreateInfo cpi{.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,.queueFamilyIndex=d.GetGraphicsFamily()};if(x.vkCreateCommandPool(dev,&cpi,nullptr,&o.command_pool)!=VK_SUCCESS)return false;VkCommandBufferAllocateInfo ca{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=o.command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};if(x.vkAllocateCommandBuffers(dev,&ca,&o.command)!=VK_SUCCESS)return false;VkCommandBufferBeginInfo cb{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};if(x.vkBeginCommandBuffer(o.command,&cb)!=VK_SUCCESS)return false;
    VkImageMemoryBarrier a{.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=im.image,.subresourceRange={aspect,0,1,0,1}};x.vkCmdPipelineBarrier(o.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&a);VkBufferImageCopy cp{.imageSubresource={aspect,0,0,1},.imageExtent={1,1,1}};x.vkCmdCopyBufferToImage(o.command,src.buffer,im.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&cp);VkImageMemoryBarrier barrier=a;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=rl;x.vkCmdPipelineBarrier(o.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);x.vkCmdBindPipeline(o.command,VK_PIPELINE_BIND_POINT_COMPUTE,o.pipeline);x.vkCmdBindDescriptorSets(o.command,VK_PIPELINE_BIND_POINT_COMPUTE,o.pipeline_layout,0,1,&o.set,0,nullptr);x.vkCmdDispatch(o.command,1,1,1);VkBufferMemoryBarrier z{.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=out.buffer,.size=4};x.vkCmdPipelineBarrier(o.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&z,0,nullptr);if(x.vkEndCommandBuffer(o.command)!=VK_SUCCESS)return false;
    VkFenceCreateInfo fi{.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};if(x.vkCreateFence(dev,&fi,nullptr,&o.fence)!=VK_SUCCESS)return false;VkSubmitInfo sub{.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&o.command};if(d.GetGraphicsQueue().Submit(vk::Span<VkSubmitInfo>{sub},o.fence)!=VK_SUCCESS)return false;if(x.vkWaitForFences(dev,1,&o.fence,VK_TRUE,1000000000ULL)!=VK_SUCCESS)return false;
    const float got=std::bit_cast<float>(*static_cast<uint32_t*>(out.mapped));state=(std::isfinite(got)&&std::fabs(got-expected)<=.001f)?CapabilityState::Validated:CapabilityState::Advertised;return state==CapabilityState::Validated;
}
}
void RunXclipseDepthComparisonProbes(const Device& d,XclipseOptimizationProbeResults& r){
    if(!d.IsXclipse())return;
    r.r32_sampled_image=CapabilityState::Advertised;r.r32_compare_non_dref=CapabilityState::Advertised;r.r32_dref_sample=CapabilityState::Advertised;r.d32_compare_dref=CapabilityState::Advertised;r.mutable_r32_d32_view=CapabilityState::Advertised;
    Execute(d,VK_FORMAT_R32_SFLOAT,false,false,false,.75f,r.r32_sampled_image);
    Execute(d,VK_FORMAT_R32_SFLOAT,false,true,false,.75f,r.r32_compare_non_dref);
    Execute(d,VK_FORMAT_R32_SFLOAT,true,true,false,1.f,r.r32_dref_sample);
    Execute(d,VK_FORMAT_D32_SFLOAT,true,true,false,1.f,r.d32_compare_dref);
    Execute(d,VK_FORMAT_R32_SFLOAT,true,true,true,1.f,r.mutable_r32_d32_view);
    r.depth_compare_probe_cases=5;
    r.depth_compare_probe_failures=(r.r32_sampled_image!=CapabilityState::Validated)+(r.r32_compare_non_dref!=CapabilityState::Validated)+(r.r32_dref_sample!=CapabilityState::Validated)+(r.d32_compare_dref!=CapabilityState::Validated)+(r.mutable_r32_d32_view!=CapabilityState::Validated);
    LOG_INFO(Render_Vulkan,"XCLIPSE DEPTH PROBES cases={} failures={} r32_sample={} r32_compare={} r32_dref={} d32_dref={} mutable_r32_d32={}",r.depth_compare_probe_cases,r.depth_compare_probe_failures,CapabilityStateName(r.r32_sampled_image),CapabilityStateName(r.r32_compare_non_dref),CapabilityStateName(r.r32_dref_sample),CapabilityStateName(r.d32_compare_dref),CapabilityStateName(r.mutable_r32_d32_view));
}
}