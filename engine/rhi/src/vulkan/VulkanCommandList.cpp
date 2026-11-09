#include "VulkanCommon.h"
#include <algorithm>

namespace ember::rhi {

namespace {

struct StateInfo {
    VkImageLayout layout;
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;
};

constexpr VkPipelineStageFlags2 kShaderStages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

StateInfo stateInfo(ResourceState state, const VulkanTexture& t)
{
    const bool storage = has(t.desc.usage, TextureUsage::Storage);
    switch (state) {
    case ResourceState::Undefined:
        return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
    case ResourceState::ShaderRead:
        return {storage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kShaderStages,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT};
    case ResourceState::ShaderReadWrite:
        return {VK_IMAGE_LAYOUT_GENERAL, kShaderStages,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    case ResourceState::ColorAttachment:
        return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
    case ResourceState::DepthAttachment:
        return {VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    case ResourceState::TransferSrc:
        return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT};
    case ResourceState::TransferDst:
        return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT};
    case ResourceState::Present:
        return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
    }
    return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT};
}

bool isReadOnly(ResourceState state)
{
    return state == ResourceState::ShaderRead || state == ResourceState::TransferSrc ||
           state == ResourceState::Present;
}

VkAttachmentLoadOp toVk(LoadOp op)
{
    switch (op) {
    case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

} // namespace

void CommandList::barrier(TextureHandle handle, ResourceState newState)
{
    VulkanTexture& t = m_impl->device->texture(handle);
    if (t.state == newState && isReadOnly(newState)) {
        return;
    }
    // Write-after-write on the same state still needs a dependency (e.g. two compute passes on one image).
    const StateInfo from = stateInfo(t.state, t);
    const StateInfo to = stateInfo(newState, t);

    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = from.stages;
    barrier.srcAccessMask = isReadOnly(t.state) ? VK_ACCESS_2_NONE : from.access; // reads need no flush
    barrier.dstStageMask = to.stages;
    barrier.dstAccessMask = to.access;
    barrier.oldLayout = from.layout;
    barrier.newLayout = to.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = t.image;
    barrier.subresourceRange = {t.aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(m_impl->cmd, &dependency);
    t.state = newState;
}

void CommandList::globalBarrier()
{
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(m_impl->cmd, &dependency);
}

void CommandList::beginRendering(const RenderingDesc& desc)
{
    Device::Impl& d = *m_impl->device;

    // Attachments are moved into the right state automatically (barriers can't go inside a render pass).
    for (u32 i = 0; i < desc.colorCount; ++i) {
        barrier(desc.colors[i].texture, ResourceState::ColorAttachment);
    }
    if (desc.depth.texture.isValid()) {
        barrier(desc.depth.texture, ResourceState::DepthAttachment);
    }

    VkRenderingAttachmentInfo colors[kMaxColorAttachments] = {};
    VkExtent2D extent{};
    for (u32 i = 0; i < desc.colorCount; ++i) {
        const VulkanTexture& t = d.texture(desc.colors[i].texture);
        VkRenderingAttachmentInfo& a = colors[i];
        a = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        a.imageView = t.view;
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp = toVk(desc.colors[i].load);
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        for (int c = 0; c < 4; ++c) {
            a.clearValue.color.float32[c] = desc.colors[i].clear[c];
        }
        extent = {t.desc.width, t.desc.height};
    }

    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (desc.depth.texture.isValid()) {
        const VulkanTexture& t = d.texture(desc.depth.texture);
        depth.imageView = t.view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = toVk(desc.depth.load);
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil.depth = desc.depth.clear;
        extent = {t.desc.width, t.desc.height};
    }

    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, extent};
    info.layerCount = 1;
    info.colorAttachmentCount = desc.colorCount;
    info.pColorAttachments = colors;
    info.pDepthAttachment = desc.depth.texture.isValid() ? &depth : nullptr;
    vkCmdBeginRendering(m_impl->cmd, &info);

    setViewport(0.0f, 0.0f, static_cast<f32>(extent.width), static_cast<f32>(extent.height));
    setScissor(0, 0, extent.width, extent.height);
}

void CommandList::endRendering()
{
    vkCmdEndRendering(m_impl->cmd);
}

void CommandList::setViewport(f32 x, f32 y, f32 width, f32 height)
{
    // Negative height flips Y so clip space is y-up, like D3D and most math references.
    VkViewport viewport{x, y + height, width, -height, 0.0f, 1.0f};
    vkCmdSetViewport(m_impl->cmd, 0, 1, &viewport);
}

void CommandList::setScissor(i32 x, i32 y, u32 width, u32 height)
{
    VkRect2D scissor{{x, y}, {width, height}};
    vkCmdSetScissor(m_impl->cmd, 0, 1, &scissor);
}

void CommandList::bindPipeline(PipelineHandle handle)
{
    const VulkanPipeline* pipeline = m_impl->device->pipelines.get(handle);
    EMBER_VERIFY(pipeline != nullptr);
    vkCmdBindPipeline(m_impl->cmd, pipeline->bindPoint, pipeline->pipeline);
}

void CommandList::pushConstants(const void* data, u32 size)
{
    EMBER_ASSERT(size <= kMaxPushConstantSize);
    vkCmdPushConstants(m_impl->cmd, m_impl->device->pipelineLayout, VK_SHADER_STAGE_ALL, 0, size, data);
}

void CommandList::bindIndexBuffer(BufferHandle buffer, u64 offset)
{
    vkCmdBindIndexBuffer(m_impl->cmd, m_impl->device->buffer(buffer).buffer, offset, VK_INDEX_TYPE_UINT32);
}

void CommandList::draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
    vkCmdDraw(m_impl->cmd, vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandList::drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
    vkCmdDrawIndexed(m_impl->cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void CommandList::dispatch(u32 groupsX, u32 groupsY, u32 groupsZ)
{
    vkCmdDispatch(m_impl->cmd, groupsX, groupsY, groupsZ);
}

void CommandList::copyBuffer(BufferHandle src, u64 srcOffset, BufferHandle dst, u64 dstOffset, u64 size)
{
    Device::Impl& d = *m_impl->device;
    VkBufferCopy region{srcOffset, dstOffset, size};
    vkCmdCopyBuffer(m_impl->cmd, d.buffer(src).buffer, d.buffer(dst).buffer, 1, &region);
}

void CommandList::copyBufferToTexture(BufferHandle src, u64 srcOffset, TextureHandle dst, u32 mip, u32 layer)
{
    Device::Impl& d = *m_impl->device;
    const VulkanTexture& t = d.texture(dst);
    EMBER_ASSERT(t.state == ResourceState::TransferDst);

    VkBufferImageCopy region{};
    region.bufferOffset = srcOffset;
    region.imageSubresource = {t.aspect, mip, layer, 1};
    region.imageExtent = {std::max(1u, t.desc.width >> mip), std::max(1u, t.desc.height >> mip), 1};
    vkCmdCopyBufferToImage(m_impl->cmd, d.buffer(src).buffer, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &region);
}

void CommandList::copyTextureToBuffer(TextureHandle src, BufferHandle dst, u64 dstOffset)
{
    Device::Impl& d = *m_impl->device;
    const VulkanTexture& t = d.texture(src);
    EMBER_ASSERT(t.state == ResourceState::TransferSrc);

    VkBufferImageCopy region{};
    region.bufferOffset = dstOffset;
    region.imageSubresource = {t.aspect, 0, 0, 1};
    region.imageExtent = {t.desc.width, t.desc.height, 1};
    vkCmdCopyImageToBuffer(m_impl->cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d.buffer(dst).buffer, 1,
                           &region);
}

void CommandList::beginLabel(const char* name)
{
    if (m_impl->device->debugUtils) {
        VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = name;
        vkCmdBeginDebugUtilsLabelEXT(m_impl->cmd, &label);
    }
}

void CommandList::endLabel()
{
    if (m_impl->device->debugUtils) {
        vkCmdEndDebugUtilsLabelEXT(m_impl->cmd);
    }
}

} // namespace ember::rhi
