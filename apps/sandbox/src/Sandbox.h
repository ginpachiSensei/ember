#pragma once

#include "ember/asset/AssetManager.h"
#include "ember/core/Jobs.h"
#include "ember/core/Memory.h"
#include "ember/platform/Window.h"
#include "ember/render/Renderer.h"
#include "ember/rhi/Device.h"
#include "ember/scene/Scene.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>

namespace sandbox {

using namespace ember;

// The procedural test shapes aren't a file asset; they're registered under this fixed Guid.
inline constexpr Guid kTestSceneModel{0x00000000000e3be4ull, 0x0000000000000001ull};

struct Options {
    std::filesystem::path scenePath;
    std::filesystem::path capture;
    u32 captureFrames = 5;
    bool captureUi = false;
    bool validation = true;
    bool useHdr = false;
    std::optional<std::array<f32, 5>> camera;
    std::optional<std::array<f32, 2>> sun;
    u32 debugView = 0;
};

struct FlyCamera {
    f32 speed = 3.0f; // meters per second
    void update(render::Camera& camera, platform::Window& window, f32 dt);
};

// GPU copy of a model asset, re-uploaded when the asset manager reports a new version (hot reload).
struct LoadedModel {
    render::ModelHandle handle;
    u32 version = 0;
    bool usedThisFrame = false;
};

struct App {
    Options options;
    std::unique_ptr<platform::Window> window;
    std::unique_ptr<rhi::Device> device;
    std::unique_ptr<render::Renderer> renderer;
    std::unique_ptr<JobSystem> jobs;
    std::unique_ptr<asset::AssetManager> assets;

    scene::Scene scene;
    scene::Entity selected = scene::kNullEntity;
    std::unordered_map<Guid, LoadedModel> models;
    render::RenderScene renderScene;
    FlyCamera fly;

    Guid environmentAsset;
    u32 environmentVersion = 0;
    bool useHdr = false;
    bool environmentApplied = false;

    bool requestScreenshot = false;
    std::array<f32, 120> frameTimesMs{};
    usize frameTimeIndex = 0;
};

void buildDefaultScene(App& app);
void setSunAngles(App& app, f32 azimuthDegrees, f32 elevationDegrees);
void drawEditor(App& app);

} // namespace sandbox
