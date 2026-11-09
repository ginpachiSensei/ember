#pragma once

#include "ember/core/Types.h"
#include "ember/platform/Window.h"
#include "ember/rhi/CommandList.h"
#include "ember/rhi/Types.h"

#include <functional>
#include <memory>

namespace ember::rhi {

struct DeviceDesc {
    platform::NativeWindowHandle window;
    u32 width = 0;
    u32 height = 0;
    bool enableValidation = false;
    bool vsync = true;
};

// The GPU: owns the Vulkan instance, device, swapchain, frame synchronization,
// the bindless descriptor table and every resource created through it.
// No Vulkan types appear in this header, so the rest of the engine stays API-agnostic.
//
// Resources are referenced by handles. Destroying a resource is deferred until the GPU
// has finished every frame that might still use it, so it's safe to destroy at any time.
class Device {
public:
    static std::unique_ptr<Device> create(const DeviceDesc& desc);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // --- Frame ---------------------------------------------------------------------------------
    // Returns nullptr when this frame should be skipped (e.g. window minimized or swapchain rebuilt).
    CommandList* beginFrame();
    void endFrame(); // moves the backbuffer to Present, submits and presents
    TextureHandle backbuffer() const; // the swapchain image for the current frame
    Format backbufferFormat() const;
    u32 backbufferWidth() const;
    u32 backbufferHeight() const;
    bool isBackbufferSrgb() const; // true: write linear colors, the hardware converts to sRGB
    u32 frameIndex() const;        // 0 .. kFramesInFlight-1, for per-frame resources

    void resize(u32 width, u32 height);
    void waitIdle();

    // Records commands, submits them and waits for the GPU. For loading and one-off setup work.
    void immediateSubmit(const std::function<void(CommandList&)>& record);

    // --- Buffers -------------------------------------------------------------------------------
    // GpuOnly buffers with initialData are filled through a staging copy (blocking).
    BufferHandle createBuffer(const BufferDesc& desc, const void* initialData = nullptr);
    void destroyBuffer(BufferHandle buffer);
    void* mappedData(BufferHandle buffer); // Upload/Readback memory only; stays mapped for its lifetime
    u64 deviceAddress(BufferHandle buffer); // the 64-bit pointer shaders use to read it
    u64 bufferSize(BufferHandle buffer) const;

    // --- Textures ------------------------------------------------------------------------------
    TextureHandle createTexture(const TextureDesc& desc);
    void destroyTexture(TextureHandle texture);
    const TextureDesc& textureDesc(TextureHandle texture) const;
    // Blocking upload of one mip of one layer (cube face). Leaves the texture in ShaderRead.
    void uploadTexture(TextureHandle texture, const void* data, u64 size, u32 mip = 0, u32 layer = 0);
    // Fills mips 1.. from mip 0 with linear downsampling (blocking). Needs TransferSrc|TransferDst usage.
    void generateMips(TextureHandle texture);

    u32 bindlessIndex(TextureHandle texture) const;              // index into the sampled-texture table
    u32 storageIndex(TextureHandle texture, u32 mip = 0) const;  // index into the storage-image table

    SamplerHandle createSampler(const SamplerDesc& desc);
    u32 bindlessIndex(SamplerHandle sampler) const;

    // --- Pipelines -----------------------------------------------------------------------------
    PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc); // invalid handle on failure
    PipelineHandle createComputePipeline(const ComputePipelineDesc& desc);
    void destroyPipeline(PipelineHandle pipeline);

    // --- Dear ImGui (renderer side) ------------------------------------------------------------
    // Needs an ImGui context to exist; shut down before destroying it.
    void initImGui();
    void shutdownImGui();
    void beginImGuiFrame();
    void renderImGui(CommandList& cmd); // draws ImGui::GetDrawData() on top of the backbuffer

    const char* gpuName() const;

    struct Impl;

private:
    explicit Device(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace ember::rhi
