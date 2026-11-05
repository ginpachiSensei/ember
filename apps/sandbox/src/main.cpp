// Sandbox: the test bed where new engine features are tried out.
// Phase 0: window + Vulkan clear + Dear ImGui overlay + Tracy profiling.

#include "ember/core/Log.h"
#include "ember/core/Profile.h"
#include "ember/debugui/Theme.h"
#include "ember/platform/Window.h"
#include "ember/rhi/Device.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <array>
#include <chrono>
#include <cmath>
#include <thread>

using namespace ember;

namespace {

struct SandboxState {
    bool animateColor = true;
    bool showDemoWindow = false;
    bool showStyleEditor = false;
    f32 clearColor[3] = {0.05f, 0.05f, 0.10f};

    std::array<f32, 120> frameTimesMs{}; // ring buffer for the graph
    usize frameTimeIndex = 0;
};

void drawDebugUi(SandboxState& state, const rhi::Device& device)
{
    EMBER_PROFILE_FUNCTION();
    const ImGuiIO& io = ImGui::GetIO();

    state.frameTimesMs[state.frameTimeIndex] = io.DeltaTime * 1000.0f;
    state.frameTimeIndex = (state.frameTimeIndex + 1) % state.frameTimesMs.size();

    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::Begin(ICON_FA_FIRE "  Ember", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::SeparatorText(ICON_FA_GAUGE_HIGH "  Performance");
    ImGui::Text("%.1f FPS", io.Framerate);
    ImGui::SameLine();
    ImGui::TextDisabled("(%.2f ms)", 1000.0f / io.Framerate);
    ImGui::PlotLines("##frametimes", state.frameTimesMs.data(), static_cast<int>(state.frameTimesMs.size()),
                     static_cast<int>(state.frameTimeIndex), "frame time (ms)", 0.0f, 33.3f, ImVec2(260, 60));

    ImGui::SeparatorText(ICON_FA_MICROCHIP "  Device");
    if (ImGui::BeginTable("##device", 2)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("GPU");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(device.gpuName());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Backbuffer");
        ImGui::TableNextColumn();
        ImGui::Text("%u x %u", device.backbufferWidth(), device.backbufferHeight());
        ImGui::EndTable();
    }

    ImGui::SeparatorText(ICON_FA_PALETTE "  Clear color");
    ImGui::Checkbox("Animate", &state.animateColor);
    ImGui::BeginDisabled(state.animateColor);
    ImGui::ColorEdit3("##clearcolor", state.clearColor);
    ImGui::EndDisabled();

    ImGui::SeparatorText(ICON_FA_SLIDERS "  Tools");
    ImGui::Checkbox(ICON_FA_LAYER_GROUP "  ImGui demo window", &state.showDemoWindow);
    ImGui::Checkbox(ICON_FA_EYE "  Style editor", &state.showStyleEditor);
    ImGui::End();

    if (state.showDemoWindow) {
        ImGui::ShowDemoWindow(&state.showDemoWindow);
    }
    if (state.showStyleEditor) {
        // Tweak every style value live; "Export" copies the result as C++ code.
        ImGui::Begin(ICON_FA_EYE "  Style editor", &state.showStyleEditor);
        ImGui::ShowStyleEditor();
        ImGui::End();
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
    debugui::applyTheme({
        .fontDirectory = EMBER_CONTENT_DIR "/fonts",
        .fontSize = 16.0f,
        .dpiScale = window->displayScale(),
        .linearOutput = device->isBackbufferSrgb(),
    });
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
