#pragma once

#include "ember/rhi/Types.h"

namespace ember::rhi {

class Device;

// Records GPU commands for one frame (or one immediate submission).
// Get one from Device::beginFrame() or Device::immediateSubmit(); never create it yourself.
//
// Binding model (see shaders/core/Bindless.slang):
// - Every pipeline shares one layout: a global bindless table of textures/samplers/storage images,
//   plus up to 128 bytes of push constants.
// - Shaders reach buffers through 64-bit device addresses passed in push constants or other buffers.
// So there are no per-draw descriptor sets: bind a pipeline, push constants, draw.
class CommandList {
public:
    void beginRendering(const RenderingDesc& desc); // also sets viewport + scissor to the full target
    void endRendering();
    void setViewport(f32 x, f32 y, f32 width, f32 height); // y-up: (0,0) is the top-left of the target
    void setScissor(i32 x, i32 y, u32 width, u32 height);

    void bindPipeline(PipelineHandle pipeline);
    void pushConstants(const void* data, u32 size);
    template <typename T>
    void pushConstants(const T& data)
    {
        static_assert(sizeof(T) <= kMaxPushConstantSize, "push constants are limited to 128 bytes");
        pushConstants(&data, sizeof(T));
    }

    void bindIndexBuffer(BufferHandle buffer, u64 offset = 0); // 32-bit indices
    void draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0);
    void drawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0,
                     u32 firstInstance = 0);
    void dispatch(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1);

    // Moves a texture to a new state (layout + memory dependency). No-op if it's already there,
    // except ShaderReadWrite -> ShaderReadWrite, which still waits for earlier writes.
    void barrier(TextureHandle texture, ResourceState newState);
    // Makes all earlier writes (buffers included) visible to all later commands. Heavy-handed but simple;
    // the render graph (phase 3) replaces this with precise barriers.
    void globalBarrier();

    void copyBuffer(BufferHandle src, u64 srcOffset, BufferHandle dst, u64 dstOffset, u64 size);
    void copyBufferToTexture(BufferHandle src, u64 srcOffset, TextureHandle dst, u32 mip = 0, u32 layer = 0);
    // Copies mip 0 / layer 0, tightly packed. The texture must be in TransferSrc.
    void copyTextureToBuffer(TextureHandle src, BufferHandle dst, u64 dstOffset = 0);

    // Named regions shown in RenderDoc / Nsight captures.
    void beginLabel(const char* name);
    void endLabel();

    struct Impl;
    CommandList() = default; // an empty list; only the Device hands out usable ones

private:
    friend class Device;
    Impl* m_impl = nullptr;
};

// RAII helper for CommandList::beginLabel / endLabel.
class ScopedLabel {
public:
    ScopedLabel(CommandList& cmd, const char* name)
        : m_cmd(cmd)
    {
        m_cmd.beginLabel(name);
    }
    ~ScopedLabel() { m_cmd.endLabel(); }
    ScopedLabel(const ScopedLabel&) = delete;
    ScopedLabel& operator=(const ScopedLabel&) = delete;

private:
    CommandList& m_cmd;
};

} // namespace ember::rhi
