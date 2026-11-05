// Sandbox: the test bed where new engine features are tried out.
// Phase 0: window + Vulkan clear + Dear ImGui overlay + Tracy profiling.

#include "ember/core/Log.h"
#include "ember/core/Profile.h"
#include "ember/platform/Window.h"
#include "ember/rhi/Device.h"

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <thread>

using namespace ember;

namespace {

struct SandboxState {
    bool animateColor = true;
    bool showDemoWindow = false;
    f32 clearColor[3] = {0.05f, 0.05f, 0.10f};
};

void drawDebugUi(SandboxState& state, const rhi::Device& device)
{
    EMBER_PROFILE_FUNCTION();
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::Begin("Ember", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("%.1f FPS (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
    ImGui::Text("GPU: %s", device.gpuName());
    ImGui::Text("Backbuffer: %u x %u", device.backbufferWidth(), device.backbufferHeight());
    ImGui::Separator();
    ImGui::Checkbox("Animate clear color", &state.animateColor);
    ImGui::BeginDisabled(state.animateColor);
    ImGui::ColorEdit3("Clear color", state.clearColor);
    ImGui::EndDisabled();
    ImGui::Checkbox("Show ImGui demo window", &state.showDemoWindow);
    ImGui::End();

    if (state.showDemoWindow) {
        ImGui::ShowDemoWindow(&state.showDemoWindow);
    }
}

} // namespace

int main()
{
    EMBER_LOG_INFO("Ember sandbox starting");

    auto window = platform::Window::create({.title = "Ember Sandbox", .width = 1280, .height = 720});
    if (!window) {
        return 1;
    }

    rhi::DeviceDesc deviceDesc;
    deviceDesc.window = window->nativeHandle();
    deviceDesc.width = window->width();
    deviceDesc.height = window->height();
#if defined(EMBER_DEBUG)
    deviceDesc.enableValidation = true;
#endif

    auto device = rhi::Device::create(deviceDesc);
    if (!device) {
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    window->initImGui();
    device->initImGui();

    SandboxState state;
    const auto start = std::chrono::steady_clock::now();

    while (true) {
        window->pollEvents();
        if (window->shouldClose()) {
            break;
        }
        if (window->isMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        if (window->consumeResized()) {
            device->resize(window->width(), window->height());
        }

        if (state.animateColor) {
            // Slowly cycle the clear color so it's obvious new frames are being presented.
            const f32 t = std::chrono::duration<f32>(std::chrono::steady_clock::now() - start).count();
            state.clearColor[0] = 0.05f + 0.05f * std::sin(t);
            state.clearColor[1] = 0.05f + 0.05f * std::sin(t + 2.1f);
            state.clearColor[2] = 0.10f + 0.05f * std::sin(t + 4.2f);
        }

        device->beginImGuiFrame();
        window->beginImGuiFrame();
        ImGui::NewFrame();
        drawDebugUi(state, *device);
        ImGui::Render();

        if (device->beginFrame()) {
            device->clearBackbuffer({state.clearColor[0], state.clearColor[1], state.clearColor[2], 1.0f});
            device->renderImGui();
            device->endFrame();
        }

        EMBER_PROFILE_FRAME();
    }

    device->waitIdle();
    device->shutdownImGui();
    window->shutdownImGui();
    ImGui::DestroyContext();

    EMBER_LOG_INFO("Shutting down");
    return 0;
}
