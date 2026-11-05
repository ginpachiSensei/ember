#pragma once

#include "ember/core/Types.h"
#include "ember/platform/Window.h"

#include <memory>

namespace ember::rhi {

struct DeviceDesc {
    platform::NativeWindowHandle window;
    u32 width = 0;
    u32 height = 0;
    bool enableValidation = false; // needs the Vulkan SDK installed
    bool vsync = true;
};

struct ClearColor {
    f32 r = 0.0f;
    f32 g = 0.0f;
    f32 b = 0.0f;
    f32 a = 1.0f;
};

// Phase 0 device: owns the Vulkan instance, GPU, swapchain and per-frame synchronization.
// Command lists, buffers, textures and pipelines get their own types in phase 1.
// No Vulkan types appear in this header, so the rest of the engine stays API-agnostic.
class Device {
public:
    static std::unique_ptr<Device> create(const DeviceDesc& desc);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // Returns false when this frame should be skipped (e.g. window minimized or swapchain rebuilt).
    bool beginFrame();
    void clearBackbuffer(const ClearColor& color);
    void endFrame();

    void resize(u32 width, u32 height);
    void waitIdle();

    // Dear ImGui renderer side. Needs an ImGui context to exist; shut down before destroying it.
    void initImGui();
    void shutdownImGui();
    void beginImGuiFrame();
    void renderImGui(); // draws ImGui::GetDrawData() on top of the backbuffer; call between begin/endFrame

    const char* gpuName() const;
    u32 backbufferWidth() const;
    u32 backbufferHeight() const;
    bool isBackbufferSrgb() const; // true: write linear colors, the hardware converts to sRGB

    struct Impl;

private:
    explicit Device(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace ember::rhi
