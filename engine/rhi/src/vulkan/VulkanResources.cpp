#include "VulkanCommon.h"

#include "ember/core/Profile.h"

#include <algorithm>
#include <cstring>

namespace ember::rhi {

// ---------------------------------------------------------------------------------------------
// Formats
// ---------------------------------------------------------------------------------------------

VkFormat toVk(Format format)
{
    switch (format) {
    case Format::Unknown: return VK_FORMAT_UNDEFINED;
    case Format::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::R8Unorm: return VK_FORMAT_R8_UNORM;
    case Format::RG8Unorm: return VK_FORMAT_R8G8_UNORM;
    case Format::R16Float: return VK_FORMAT_R16_SFLOAT;
    case Format::RG16Float: return VK_FORMAT_R16G16_SFLOAT;
    case Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32Float: return VK_FORMAT_R32_SFLOAT;
    case Format::RG32Float: return VK_FORMAT_R32G32_SFLOAT;
    case Format::RGBA32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::R11G11B10Float: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case Format::D32Float: return VK_FORMAT_D32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

bool isDepthFormat(Format format)
{
    return format == Format::D32Float;
}

u32 bytesPerPixel(Format format)
{
    switch (format) {
    case Format::R8Unorm: return 1;
    case Format::RG8Unorm:
    case Format::R16Float: return 2;
    case Format::RGBA8Unorm:
    case Format::RGBA8Srgb:
    case Format::BGRA8Unorm:
    case Format::BGRA8Srgb:
    case Format::RG16Float:
    case Format::R32Float:
    case Format::R11G11B10Float:
    case Format::D32Float: return 4;
    case Format::RGBA16Float:
    case Format::RG32Float: return 8;
    case Format::RGBA32Float: return 16;
    case Format::Unknown: return 0;
    }
    return 0;
}

namespace {

VkCompareOp toVk(CompareOp op)
{
    switch (op) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkSamplerAddressMode toVk(AddressMode mode)
{
    switch (mode) {
    case AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

// The layout a texture with this usage has while shaders read it.
// Storage textures always stay in GENERAL so the same bindless descriptor works for reads and writes.
VkImageLayout shaderReadLayout(const VulkanTexture& t)
{
    return has(t.desc.usage, TextureUsage::Storage) ? VK_IMAGE_LAYOUT_GENERAL
                                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

u32 layerCount(const TextureDesc& desc)
{
    return desc.type == TextureType::TextureCube ? 6u : 1u;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------------------------

BufferHandle Device::createBuffer(const BufferDesc& desc, const void* initialData)
{
    Impl& d = *m_impl;
    EMBER_VERIFY(desc.size > 0);

    VkBufferUsageFlags usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (has(desc.usage, BufferUsage::Index)) {
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (has(desc.usage, BufferUsage::Indirect)) {
        usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    if (has(desc.usage, BufferUsage::TransferSrc)) {
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    const bool needsStaging = initialData && desc.memory == MemoryType::GpuOnly;
    if (has(desc.usage, BufferUsage::TransferDst) || needsStaging) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = desc.size;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (desc.memory == MemoryType::Upload) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else if (desc.memory == MemoryType::Readback) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }

    VulkanBuffer buffer;
    buffer.desc = desc;
    VmaAllocationInfo info{};
    EMBER_VK_CHECK(vmaCreateBuffer(d.allocator, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation, &info));
    buffer.mapped = info.pMappedData;

    VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addressInfo.buffer = buffer.buffer;
    buffer.address = vkGetBufferDeviceAddress(d.device, &addressInfo);
    d.setDebugName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<u64>(buffer.buffer), desc.debugName);

    const VkBuffer vkBuffer = buffer.buffer;
    void* mapped = buffer.mapped;
    const BufferHandle handle = d.buffers.emplace(std::move(buffer));

    if (initialData) {
        if (mapped) {
            std::memcpy(mapped, initialData, desc.size);
        } else {
            // Copy through a temporary CPU-visible buffer.
            const BufferHandle staging = createBuffer(
                {.size = desc.size, .usage = BufferUsage::TransferSrc, .memory = MemoryType::Upload, .debugName = "Staging"},
                initialData);
            const VkBuffer stagingBuffer = d.buffer(staging).buffer;
            immediateSubmit([&](CommandList& cmd) {
                VkBufferCopy region{0, 0, desc.size};
                vkCmdCopyBuffer(cmd.m_impl->cmd, stagingBuffer, vkBuffer, 1, &region);
            });
            VulkanBuffer& s = d.buffer(staging);
            vmaDestroyBuffer(d.allocator, s.buffer, s.allocation);
            d.buffers.remove(staging);
        }
    }
    return handle;
}

void Device::destroyBuffer(BufferHandle handle)
{
    Impl& d = *m_impl;
    VulkanBuffer* buffer = d.buffers.get(handle);
    if (!buffer) {
        return;
    }
    const VkBuffer vkBuffer = buffer->buffer;
    const VmaAllocation allocation = buffer->allocation;
    d.buffers.remove(handle);
    d.deferDelete([&d, vkBuffer, allocation] { vmaDestroyBuffer(d.allocator, vkBuffer, allocation); });
}

void* Device::mappedData(BufferHandle handle)
{
    return m_impl->buffer(handle).mapped;
}

u64 Device::deviceAddress(BufferHandle handle)
{
    return m_impl->buffer(handle).address;
}

u64 Device::bufferSize(BufferHandle handle) const
{
    return m_impl->buffer(handle).desc.size;
}

// ---------------------------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------------------------

TextureHandle Device::createTexture(const TextureDesc& desc)
{
    Impl& d = *m_impl;
    EMBER_VERIFY(desc.width > 0 && desc.height > 0 && desc.mipLevels > 0);
    const bool depth = isDepthFormat(desc.format);
    const bool cube = desc.type == TextureType::TextureCube;
    const u32 layers = layerCount(desc);

    VkImageUsageFlags usage = 0;
    if (has(desc.usage, TextureUsage::Sampled)) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (has(desc.usage, TextureUsage::Storage)) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (has(desc.usage, TextureUsage::ColorAttachment)) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (has(desc.usage, TextureUsage::DepthAttachment)) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (has(desc.usage, TextureUsage::TransferSrc)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (has(desc.usage, TextureUsage::TransferDst)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = toVk(desc.format);
    imageInfo.extent = {desc.width, desc.height, 1};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = layers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (has(desc.usage, TextureUsage::ColorAttachment) || has(desc.usage, TextureUsage::DepthAttachment)) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT; // render targets get their own memory
    }

    VulkanTexture texture;
    texture.desc = desc;
    texture.aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    EMBER_VK_CHECK(vmaCreateImage(d.allocator, &imageInfo, &allocInfo, &texture.image, &texture.allocation, nullptr));
    d.setDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<u64>(texture.image), desc.debugName);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = texture.image;
    viewInfo.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange = {texture.aspect, 0, desc.mipLevels, 0, layers};
    EMBER_VK_CHECK(vkCreateImageView(d.device, &viewInfo, nullptr, &texture.view));

    if (has(desc.usage, TextureUsage::Sampled)) {
        texture.sampledIndex = d.sampledIndices.allocate();
        VkDescriptorImageInfo imageDescriptor{VK_NULL_HANDLE, texture.view, shaderReadLayout(texture)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = d.bindlessSet;
        write.dstBinding = kBindingSampled;
        write.dstArrayElement = texture.sampledIndex;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(d.device, 1, &write, 0, nullptr);
    }

    if (has(desc.usage, TextureUsage::Storage)) {
        // One storage view per mip: compute shaders write one mip at a time.
        // Cubes are viewed as 2D arrays (RWTexture2DArray in shaders, one layer per face).
        for (u32 mip = 0; mip < desc.mipLevels; ++mip) {
            VkImageViewCreateInfo mipInfo = viewInfo;
            mipInfo.viewType = cube ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
            mipInfo.subresourceRange = {texture.aspect, mip, 1, 0, layers};
            VkImageView mipView = VK_NULL_HANDLE;
            EMBER_VK_CHECK(vkCreateImageView(d.device, &mipInfo, nullptr, &mipView));
            texture.mipViews.push_back(mipView);

            const u32 index = d.storageIndices.allocate();
            texture.storageIndices.push_back(index);
            VkDescriptorImageInfo imageDescriptor{VK_NULL_HANDLE, mipView, VK_IMAGE_LAYOUT_GENERAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = d.bindlessSet;
            write.dstBinding = kBindingStorage;
            write.dstArrayElement = index;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            write.pImageInfo = &imageDescriptor;
            vkUpdateDescriptorSets(d.device, 1, &write, 0, nullptr);
        }
    }

    return d.textures.emplace(std::move(texture));
}

void Device::Impl::destroyTextureNow(VulkanTexture& t)
{
    for (VkImageView view : t.mipViews) {
        vkDestroyImageView(device, view, nullptr);
    }
    vkDestroyImageView(device, t.view, nullptr);
    vmaDestroyImage(allocator, t.image, t.allocation);
    if (t.sampledIndex != kInvalidIndex) {
        sampledIndices.release(t.sampledIndex);
    }
    for (u32 index : t.storageIndices) {
        storageIndices.release(index);
    }
}

void Device::destroyTexture(TextureHandle handle)
{
    Impl& d = *m_impl;
    VulkanTexture* texture = d.textures.get(handle);
    if (!texture || !texture->allocation) {
        return; // unknown, or a swapchain image
    }
    // Bindless indices are only recycled after the GPU is done, so no in-flight frame can see a reused slot.
    d.deferDelete([&d, t = std::move(*texture)]() mutable { d.destroyTextureNow(t); });
    d.textures.remove(handle);
}

const TextureDesc& Device::textureDesc(TextureHandle handle) const
{
    return m_impl->texture(handle).desc;
}

u32 Device::bindlessIndex(TextureHandle handle) const
{
    const u32 index = m_impl->texture(handle).sampledIndex;
    EMBER_ASSERT(index != kInvalidIndex);
    return index;
}

u32 Device::storageIndex(TextureHandle handle, u32 mip) const
{
    const VulkanTexture& t = m_impl->texture(handle);
    EMBER_VERIFY(mip < t.storageIndices.size());
    return t.storageIndices[mip];
}

void Device::uploadTexture(TextureHandle handle, const void* data, u64 size, u32 mip, u32 layer)
{
    EMBER_PROFILE_FUNCTION();
    Impl& d = *m_impl;
    const BufferHandle staging = createBuffer(
        {.size = size, .usage = BufferUsage::TransferSrc, .memory = MemoryType::Upload, .debugName = "Texture staging"},
        data);
    immediateSubmit([&](CommandList& cmd) {
        cmd.barrier(handle, ResourceState::TransferDst);
        cmd.copyBufferToTexture(staging, 0, handle, mip, layer);
        cmd.barrier(handle, ResourceState::ShaderRead);
    });
    VulkanBuffer& s = d.buffer(staging);
    vmaDestroyBuffer(d.allocator, s.buffer, s.allocation);
    d.buffers.remove(staging);
}

void Device::generateMips(TextureHandle handle)
{
    EMBER_PROFILE_FUNCTION();
    Impl& d = *m_impl;
    VulkanTexture& t = d.texture(handle);
    const VkImage image = t.image;
    const TextureDesc desc = t.desc;
    const u32 layers = layerCount(desc);
    const VkImageLayout finalLayout = shaderReadLayout(t);
    if (desc.mipLevels <= 1) {
        return;
    }

    immediateSubmit([&](CommandList& cmd) {
        cmd.barrier(handle, ResourceState::TransferDst); // whole image in TRANSFER_DST
        VkCommandBuffer vk = cmd.m_impl->cmd;

        auto mipBarrier = [&](u32 mip, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 dstStage,
                              VkAccessFlags2 dstAccess) {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = dstStage;
            barrier.dstAccessMask = dstAccess;
            barrier.oldLayout = from;
            barrier.newLayout = to;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, layers};
            VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(vk, &dep);
        };

        i32 width = static_cast<i32>(desc.width);
        i32 height = static_cast<i32>(desc.height);
        for (u32 mip = 1; mip < desc.mipLevels; ++mip) {
            // The previous mip becomes the blit source.
            mipBarrier(mip - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
            const i32 nextWidth = std::max(1, width / 2);
            const i32 nextHeight = std::max(1, height / 2);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, layers};
            blit.srcOffsets[1] = {width, height, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, layers};
            blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
            vkCmdBlitImage(vk, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);
            mipBarrier(mip - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, finalLayout, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            width = nextWidth;
            height = nextHeight;
        }
        mipBarrier(desc.mipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout,
                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    d.texture(handle).state = ResourceState::ShaderRead;
}

// ---------------------------------------------------------------------------------------------
// Samplers
// ---------------------------------------------------------------------------------------------

SamplerHandle Device::createSampler(const SamplerDesc& desc)
{
    Impl& d = *m_impl;
    const VkFilter filter = desc.filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = desc.filter == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = toVk(desc.address);
    info.addressModeV = toVk(desc.address);
    info.addressModeW = toVk(desc.address);
    info.anisotropyEnable = desc.maxAnisotropy > 0.0f;
    info.maxAnisotropy = std::min(desc.maxAnisotropy, d.gpuProperties.limits.maxSamplerAnisotropy);
    info.compareEnable = desc.compare;
    info.compareOp = toVk(desc.compareOp);
    info.maxLod = VK_LOD_CLAMP_NONE;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    VulkanSampler sampler;
    EMBER_VK_CHECK(vkCreateSampler(d.device, &info, nullptr, &sampler.sampler));
    sampler.index = d.samplerIndices.allocate();

    VkDescriptorImageInfo imageInfo{sampler.sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = d.bindlessSet;
    write.dstBinding = kBindingSamplers;
    write.dstArrayElement = sampler.index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(d.device, 1, &write, 0, nullptr);

    return d.samplers.emplace(sampler);
}

u32 Device::bindlessIndex(SamplerHandle handle) const
{
    const VulkanSampler* sampler = m_impl->samplers.get(handle);
    EMBER_VERIFY(sampler != nullptr);
    return sampler->index;
}

// ---------------------------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------------------------

namespace {

VkShaderModule createShaderModule(VkDevice device, const ShaderModuleDesc& desc)
{
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = desc.spirv.size_bytes();
    info.pCode = desc.spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(device, &info, nullptr, &module);
    if (result != VK_SUCCESS) {
        EMBER_LOG_ERROR("vkCreateShaderModule failed: {}", vkResultName(result));
        return VK_NULL_HANDLE;
    }
    return module;
}

} // namespace

PipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    Impl& d = *m_impl;
    if (desc.vertex.spirv.empty()) {
        EMBER_LOG_ERROR("Pipeline '{}' has no vertex shader", desc.debugName ? desc.debugName : "?");
        return {};
    }

    VkShaderModule vertexModule = createShaderModule(d.device, desc.vertex);
    VkShaderModule fragmentModule =
        desc.fragment.spirv.empty() ? VK_NULL_HANDLE : createShaderModule(d.device, desc.fragment);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = desc.vertex.entryPoint;
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = desc.fragment.entryPoint;

    // Vertices are pulled from buffers by the shader (device addresses), so there is no vertex input state.
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology =
        desc.topology == Topology::LineList ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = desc.cull == CullMode::Back    ? VK_CULL_MODE_BACK_BIT
                      : desc.cull == CullMode::Front ? VK_CULL_MODE_FRONT_BIT
                                                     : VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // glTF convention; works with the flipped viewport
    raster.depthClampEnable = desc.depthClamp;
    raster.depthBiasEnable = desc.depthBiasConstant != 0.0f || desc.depthBiasSlope != 0.0f;
    raster.depthBiasConstantFactor = desc.depthBiasConstant;
    raster.depthBiasSlopeFactor = desc.depthBiasSlope;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = desc.depthTest;
    depthStencil.depthWriteEnable = desc.depthWrite;
    depthStencil.depthCompareOp = toVk(desc.depthCompare);

    VkPipelineColorBlendAttachmentState blendStates[kMaxColorAttachments] = {};
    VkFormat colorFormats[kMaxColorAttachments] = {};
    for (u32 i = 0; i < desc.colorCount; ++i) {
        VkPipelineColorBlendAttachmentState& blend = blendStates[i];
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                               VK_COLOR_COMPONENT_A_BIT;
        if (desc.blend != BlendMode::Opaque) {
            blend.blendEnable = VK_TRUE;
            blend.srcColorBlendFactor =
                desc.blend == BlendMode::Alpha ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
            blend.dstColorBlendFactor =
                desc.blend == BlendMode::Alpha ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
            blend.colorBlendOp = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        colorFormats[i] = toVk(desc.colorFormats[i]);
    }
    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = desc.colorCount;
    colorBlend.pAttachments = blendStates;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = desc.colorCount;
    rendering.pColorAttachmentFormats = colorFormats;
    rendering.depthAttachmentFormat = toVk(desc.depthFormat);

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = fragmentModule ? 2 : 1;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &colorBlend;
    info.pDynamicState = &dynamic;
    info.layout = d.pipelineLayout;

    VulkanPipeline pipeline;
    pipeline.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (vertexModule && (fragmentModule || desc.fragment.spirv.empty())) {
        result = vkCreateGraphicsPipelines(d.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline.pipeline);
    }
    vkDestroyShaderModule(d.device, vertexModule, nullptr);
    vkDestroyShaderModule(d.device, fragmentModule, nullptr);
    if (result != VK_SUCCESS) {
        EMBER_LOG_ERROR("Failed to create pipeline '{}': {}", desc.debugName ? desc.debugName : "?",
                        vkResultName(result));
        return {};
    }
    d.setDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(pipeline.pipeline), desc.debugName);
    return d.pipelines.emplace(pipeline);
}

PipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc)
{
    Impl& d = *m_impl;
    VkShaderModule module = createShaderModule(d.device, desc.shader);
    if (!module) {
        return {};
    }

    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = desc.shader.entryPoint;
    info.layout = d.pipelineLayout;

    VulkanPipeline pipeline;
    pipeline.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    const VkResult result = vkCreateComputePipelines(d.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline.pipeline);
    vkDestroyShaderModule(d.device, module, nullptr);
    if (result != VK_SUCCESS) {
        EMBER_LOG_ERROR("Failed to create compute pipeline '{}': {}", desc.debugName ? desc.debugName : "?",
                        vkResultName(result));
        return {};
    }
    d.setDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(pipeline.pipeline), desc.debugName);
    return d.pipelines.emplace(pipeline);
}

void Device::destroyPipeline(PipelineHandle handle)
{
    Impl& d = *m_impl;
    VulkanPipeline* pipeline = d.pipelines.get(handle);
    if (!pipeline) {
        return;
    }
    const VkPipeline vkPipeline = pipeline->pipeline;
    d.pipelines.remove(handle);
    d.deferDelete([&d, vkPipeline] { vkDestroyPipeline(d.device, vkPipeline, nullptr); });
}

} // namespace ember::rhi
