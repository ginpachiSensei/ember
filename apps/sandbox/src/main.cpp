// Sandbox: the test bed where new engine features are tried out.
// Phase 1: PBR renderer with IBL, sun shadows, tonemapping, glTF models and a fly camera.
//
// Controls: hold right mouse + WASD/QE to fly (Shift = fast, scroll = speed), F12 = screenshot, Esc = quit.
// Command line:
//   --model <file.glb>   load a glTF model (default: content/models/DamagedHelmet/DamagedHelmet.glb)
//   --hdr <file.hdr>     use an HDR environment instead of the procedural sky
//   --capture <file.png> render a few frames, save a screenshot and exit
//   --frames <n>         frames to render before --capture (default 5)
//   --no-validation      run Debug builds without the Vulkan validation layer
//   --camera x,y,z,yaw,pitch   start camera (degrees)       --sun azimuth,elevation (degrees)
//   --view <n>           debug view (0 lit, 1 base color, 2 normals, 3 roughness, 4 metallic, 5 AO, 6 shadow)

#include "ember/asset/Model.h"
#include "ember/core/Log.h"
#include "ember/core/Profile.h"
#include "ember/debugui/Theme.h"
#include "ember/platform/Window.h"
#include "ember/render/Renderer.h"
#include "ember/rhi/Device.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

using namespace ember;
namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path model = fs::path(EMBER_CONTENT_DIR) / "models/DamagedHelmet/DamagedHelmet.glb";
    fs::path hdr = fs::path(EMBER_CONTENT_DIR) / "hdri/sky.hdr";
    fs::path capture;
    u32 captureFrames = 5;
    bool validation = true;
    bool useHdr = false;
    std::optional<std::array<f32, 5>> camera;
    std::optional<std::array<f32, 2>> sun;
    u32 debugView = 0;
};

template <usize N>
std::array<f32, N> parseFloats(const std::string& text)
{
    std::array<f32, N> values{};
    usize start = 0;
    for (usize i = 0; i < N && start <= text.size(); ++i) {
        const usize comma = text.find(',', start);
        values[i] = std::stof(text.substr(start, comma - start));
        start = comma == std::string::npos ? text.size() + 1 : comma + 1;
    }
    return values;
}

Options parseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--model" && hasValue) {
            options.model = argv[++i];
        } else if (arg == "--hdr" && hasValue) {
            options.hdr = argv[++i];
            options.useHdr = true;
        } else if (arg == "--capture" && hasValue) {
            options.capture = argv[++i];
        } else if (arg == "--frames" && hasValue) {
            options.captureFrames = static_cast<u32>(std::stoul(argv[++i]));
        } else if (arg == "--camera" && hasValue) {
            options.camera = parseFloats<5>(argv[++i]);
        } else if (arg == "--sun" && hasValue) {
            options.sun = parseFloats<2>(argv[++i]);
        } else if (arg == "--view" && hasValue) {
            options.debugView = static_cast<u32>(std::stoul(argv[++i]));
        } else if (arg == "--no-validation") {
            options.validation = false;
        } else {
            EMBER_LOG_WARN("Unknown argument '{}'", arg);
        }
    }
    return options;
}

// Spheres in a grid (metallic rows x roughness columns), a ground plane and a couple of cubes.
// The classic sanity check for a PBR implementation.
asset::Model makeTestScene()
{
    asset::Model model;
    model.name = "Test scene";

    const u32 ground = asset::addMaterial(model, {.baseColorFactor = vec4(0.45f, 0.45f, 0.45f, 1.0f),
                                                  .metallicFactor = 0.0f,
                                                  .roughnessFactor = 0.8f,
                                                  .name = "Ground"});
    const u32 groundMesh = asset::addPlaneMesh(model, ground, 40.0f);
    model.instances.push_back({mat4(1.0f), groundMesh});

    constexpr int kSteps = 6;
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < kSteps; ++col) {
            const f32 roughness = static_cast<f32>(col) / static_cast<f32>(kSteps - 1);
            const vec4 color = row == 0 ? vec4(0.9f, 0.1f, 0.1f, 1.0f) : vec4(1.0f, 0.78f, 0.34f, 1.0f); // red plastic, gold
            const u32 material = asset::addMaterial(model, {.baseColorFactor = color,
                                                            .metallicFactor = row == 0 ? 0.0f : 1.0f,
                                                            .roughnessFactor = roughness});
            const u32 mesh = asset::addSphereMesh(model, material, 0.4f);
            const vec3 position(-3.0f + static_cast<f32>(col) * 1.0f + 0.5f, 0.45f + static_cast<f32>(row) * 1.0f,
                                -2.5f);
            model.instances.push_back({glm::translate(mat4(1.0f), position), mesh});
        }
    }

    const u32 white = asset::addMaterial(model, {.baseColorFactor = vec4(0.8f, 0.8f, 0.8f, 1.0f),
                                                 .metallicFactor = 0.0f,
                                                 .roughnessFactor = 0.5f});
    const u32 cube = asset::addCubeMesh(model, white, 0.5f);
    model.instances.push_back({glm::translate(mat4(1.0f), vec3(3.0f, 0.5f, 0.5f)), cube});
    model.instances.push_back(
        {glm::rotate(glm::translate(mat4(1.0f), vec3(-3.0f, 0.75f, 0.8f)), 0.6f, vec3(0, 1, 0)) *
             glm::scale(mat4(1.0f), vec3(0.6f, 1.5f, 0.6f)),
         cube});
    return model;
}

struct FlyCamera {
    f32 speed = 3.0f; // meters per second

    void update(render::Camera& camera, platform::Window& window, f32 dt)
    {
        const bool looking = window.isMouseDown(platform::MouseButton::Right);
        window.setRelativeMouseMode(looking);
        if (looking) {
            constexpr f32 sensitivity = 0.0025f;
            camera.yaw -= window.mouseDeltaX() * sensitivity;
            camera.pitch -= window.mouseDeltaY() * sensitivity;
            camera.pitch = glm::clamp(camera.pitch, -1.55f, 1.55f);
            speed = glm::clamp(speed * std::pow(1.2f, window.scrollDelta()), 0.1f, 200.0f);
        }

        vec3 move(0.0f);
        using platform::Key;
        move += camera.forward() * (static_cast<f32>(window.isKeyDown(Key::W)) - static_cast<f32>(window.isKeyDown(Key::S)));
        move += camera.right() * (static_cast<f32>(window.isKeyDown(Key::D)) - static_cast<f32>(window.isKeyDown(Key::A)));
        move.y += static_cast<f32>(window.isKeyDown(Key::E)) - static_cast<f32>(window.isKeyDown(Key::Q));
        if (glm::length(move) > 0.0f) {
            const f32 boost = window.isKeyDown(Key::LeftShift) ? 4.0f : 1.0f;
            camera.position += glm::normalize(move) * speed * boost * dt;
        }
    }
};

struct SandboxState {
    render::RenderScene scene;
    FlyCamera fly;
    render::ModelHandle helmet;
    bool spinHelmet = true;
    f32 helmetAngle = 0.0f;
    f32 sunAzimuth = glm::radians(35.0f);
    f32 sunElevation = glm::radians(40.0f);
    bool useHdr = false;
    bool hasHdr = false;
    bool environmentChanged = false;
    bool requestScreenshot = false;

    std::array<f32, 120> frameTimesMs{}; // ring buffer for the graph
    usize frameTimeIndex = 0;
};

void drawDebugUi(SandboxState& state, const rhi::Device& device, render::Renderer& renderer)
{
    EMBER_PROFILE_FUNCTION();
    const ImGuiIO& io = ImGui::GetIO();
    render::RenderSettings& settings = renderer.settings();

    state.frameTimesMs[state.frameTimeIndex] = io.DeltaTime * 1000.0f;
    state.frameTimeIndex = (state.frameTimeIndex + 1) % state.frameTimesMs.size();

    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin(ICON_FA_FIRE "  Ember");

    if (ImGui::CollapsingHeader(ICON_FA_GAUGE_HIGH "  Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("%.1f FPS", io.Framerate);
        ImGui::SameLine();
        ImGui::TextDisabled("(%.2f ms)", 1000.0f / io.Framerate);
        ImGui::PlotLines("##frametimes", state.frameTimesMs.data(), static_cast<int>(state.frameTimesMs.size()),
                         static_cast<int>(state.frameTimeIndex), "frame time (ms)", 0.0f, 33.3f, ImVec2(-1, 60));
        const render::RenderStats& stats = renderer.stats();
        ImGui::TextDisabled("%s", device.gpuName());
        ImGui::TextDisabled("%u x %u  |  %u draws  |  %u shadow draws  |  %.1fk tris", device.backbufferWidth(),
                            device.backbufferHeight(), stats.drawCalls, stats.shadowDrawCalls,
                            static_cast<f32>(stats.triangles) / 1000.0f);
    }

    if (ImGui::CollapsingHeader(ICON_FA_CAMERA "  Image", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Exposure (EV)", &settings.exposureEv, -6.0f, 6.0f, "%.1f");
        const char* tonemappers[] = {"None (clip)", "Reinhard", "ACES (fitted)", "AgX", "Khronos PBR Neutral"};
        int tonemapper = static_cast<int>(settings.tonemapper);
        if (ImGui::Combo("Tonemapper", &tonemapper, tonemappers, IM_ARRAYSIZE(tonemappers))) {
            settings.tonemapper = static_cast<render::Tonemapper>(tonemapper);
        }
        const char* views[] = {"Lit", "Base color", "Normals", "Roughness", "Metallic", "Occlusion", "Shadow"};
        int view = static_cast<int>(settings.debugView);
        if (ImGui::Combo("Debug view", &view, views, IM_ARRAYSIZE(views))) {
            settings.debugView = static_cast<render::DebugView>(view);
        }
        if (ImGui::Button(ICON_FA_IMAGE "  Screenshot (F12)")) {
            state.requestScreenshot = true;
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_SUN "  Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderAngle("Sun azimuth", &state.sunAzimuth, -180.0f, 180.0f);
        ImGui::SliderAngle("Sun elevation", &state.sunElevation, -5.0f, 90.0f);
        ImGui::SliderFloat("Sun intensity", &state.scene.sun.intensity, 0.0f, 20.0f);
        ImGui::ColorEdit3("Sun color", &state.scene.sun.color.x);
        ImGui::SliderFloat("IBL intensity", &settings.iblIntensity, 0.0f, 3.0f);
        ImGui::SliderFloat("Sky intensity", &settings.skyIntensity, 0.0f, 3.0f);
        ImGui::BeginDisabled(!state.hasHdr);
        if (ImGui::Checkbox("HDR environment (content/hdri/sky.hdr)", &state.useHdr)) {
            state.environmentChanged = true;
        }
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader(ICON_FA_CIRCLE_HALF_STROKE "  Shadows")) {
        static const u32 sizes[] = {512, 1024, 2048, 4096};
        int index = 0;
        for (int i = 0; i < 4; ++i) {
            if (sizes[i] == settings.shadowMapSize) {
                index = i;
            }
        }
        if (ImGui::Combo("Map size", &index, "512\0" "1024\0" "2048\0" "4096\0\0")) {
            settings.shadowMapSize = sizes[index];
        }
        ImGui::SliderFloat("Depth bias", &settings.shadowBiasConstant, 0.0f, 10.0f);
        ImGui::SliderFloat("Slope bias", &settings.shadowBiasSlope, 0.0f, 10.0f);
        ImGui::SliderFloat("Normal bias (texels)", &settings.shadowNormalBias, 0.0f, 5.0f);
    }

    if (ImGui::CollapsingHeader(ICON_FA_CUBE "  Scene")) {
        ImGui::Checkbox("Spin helmet", &state.spinHelmet);
        ImGui::SliderFloat("Camera speed", &state.fly.speed, 0.1f, 50.0f, "%.1f m/s", ImGuiSliderFlags_Logarithmic);
        ImGui::TextDisabled("Hold right mouse + WASD/QE to fly, scroll = speed");
    }

    if (ImGui::CollapsingHeader(ICON_FA_CODE "  Shaders", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Edit any file in shaders/ and save: it recompiles live.");
        const std::string& error = renderer.shaderError();
        if (error.empty()) {
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), ICON_FA_CHECK "  All shaders compiled");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Compile error:");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(error.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::End();
}

fs::path screenshotPath()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char name[64];
    std::strftime(name, sizeof(name), "ember-%Y%m%d-%H%M%S.png", &local);
    return fs::path(EMBER_CONTENT_DIR).parent_path() / "screenshots" / name;
}

} // namespace

int main(int argc, char** argv)
{
    EMBER_LOG_INFO("Ember sandbox starting");
    const Options options = parseOptions(argc, argv);

    auto window = platform::Window::create({.title = "Ember Sandbox", .width = 1600, .height = 900});
    if (!window) {
        return 1;
    }

    rhi::DeviceDesc deviceDesc;
    deviceDesc.window = window->nativeHandle();
    deviceDesc.width = window->width();
    deviceDesc.height = window->height();
#if defined(EMBER_DEBUG)
    deviceDesc.enableValidation = options.validation;
#endif

    auto device = rhi::Device::create(deviceDesc);
    if (!device) {
        return 1;
    }
    auto renderer = render::Renderer::create(*device);
    if (!renderer) {
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

    // --- Scene ---------------------------------------------------------------------------------
    SandboxState state;
    state.scene.camera.position = vec3(0.0f, 1.6f, 4.5f);
    state.scene.camera.pitch = glm::radians(-10.0f);
    if (options.camera) {
        const auto& c = *options.camera;
        state.scene.camera.position = vec3(c[0], c[1], c[2]);
        state.scene.camera.yaw = glm::radians(c[3]);
        state.scene.camera.pitch = glm::radians(c[4]);
    }
    if (options.sun) {
        state.sunAzimuth = glm::radians((*options.sun)[0]);
        state.sunElevation = glm::radians((*options.sun)[1]);
    }
    renderer->settings().debugView = static_cast<render::DebugView>(options.debugView);

    const render::ModelHandle testScene = renderer->uploadModel(makeTestScene());
    state.scene.instances.push_back({testScene, mat4(1.0f)});

    if (fs::exists(options.model)) {
        if (std::optional<asset::Model> model = asset::loadGltf(options.model)) {
            state.helmet = renderer->uploadModel(*model);
        }
    } else {
        EMBER_LOG_WARN("{} not found. Run tools/fetch-sample-content.ps1 to download sample assets.",
                       options.model.string());
    }
    if (state.helmet.isValid()) {
        state.scene.instances.push_back({state.helmet, mat4(1.0f)});
    }

    std::optional<asset::HdrImage> hdr;
    if (fs::exists(options.hdr)) {
        hdr = asset::loadHdr(options.hdr);
    }
    state.hasHdr = hdr.has_value();
    state.useHdr = options.useHdr && state.hasHdr;
    renderer->setEnvironment(state.useHdr ? &*hdr : nullptr);

    if (!options.capture.empty()) {
        state.spinHelmet = false; // deterministic screenshots
    }

    // --- Main loop -----------------------------------------------------------------------------
    u32 frameCount = 0;
    auto lastTime = std::chrono::steady_clock::now();
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

        const auto now = std::chrono::steady_clock::now();
        const f32 dt = std::min(std::chrono::duration<f32>(now - lastTime).count(), 0.1f);
        lastTime = now;

        renderer->reloadChangedShaders();
        if (state.environmentChanged) {
            renderer->setEnvironment(state.useHdr ? &*hdr : nullptr);
            state.environmentChanged = false;
        }

        // Update the scene snapshot.
        state.fly.update(state.scene.camera, *window, dt);
        state.scene.sun.direction = vec3(std::cos(state.sunElevation) * std::sin(state.sunAzimuth),
                                         std::sin(state.sunElevation),
                                         std::cos(state.sunElevation) * std::cos(state.sunAzimuth));
        if (state.helmet.isValid()) {
            if (state.spinHelmet) {
                state.helmetAngle += dt * 0.4f;
            }
            // Lift the helmet off the ground and turn it slowly (its glTF nodes already orient it upright).
            state.scene.instances.back().transform = glm::translate(mat4(1.0f), vec3(0.0f, 1.1f, 0.0f)) *
                                                     glm::rotate(mat4(1.0f), state.helmetAngle, vec3(0, 1, 0)) *
                                                     glm::scale(mat4(1.0f), vec3(0.8f));
        }

        device->beginImGuiFrame();
        window->beginImGuiFrame();
        ImGui::NewFrame();
        drawDebugUi(state, *device, *renderer);
        ImGui::Render();

        const bool lastCaptureFrame = !options.capture.empty() && frameCount + 1 >= options.captureFrames;
        if (window->wasKeyPressed(platform::Key::F12) || state.requestScreenshot) {
            renderer->requestCapture(screenshotPath());
            state.requestScreenshot = false;
        }
        if (lastCaptureFrame) {
            renderer->requestCapture(options.capture);
        }

        if (rhi::CommandList* cmd = device->beginFrame()) {
            renderer->render(*cmd, state.scene);
            if (options.capture.empty()) {
                device->renderImGui(*cmd); // keep automated screenshots free of UI
            }
            device->endFrame();
            ++frameCount;
        }
        EMBER_PROFILE_FRAME();

        if (lastCaptureFrame) {
            break;
        }
    }

    device->waitIdle();
    renderer->finishCapture();
    renderer.reset();
    device->shutdownImGui();
    window->shutdownImGui();
    ImGui::DestroyContext();

    EMBER_LOG_INFO("Shutting down");
    return 0;
}
