// Sandbox: the test bed where new engine features are tried out.
// Phase 2: an ECS scene (EnTT) with a hierarchy, cooked assets loaded on worker threads, a
// reflection-driven inspector and scene files, on top of the phase 1 PBR renderer.
//
// Controls: hold right mouse + WASD/QE to fly (Shift = fast, scroll = speed), F12 = screenshot, Esc = quit.
// Command line:
//   --scene <file.json>  scene file to load/save (default: content/scenes/sandbox.scene.json)
//   --hdr                start with the HDR sky (content/hdri/sky.hdr) instead of the procedural one
//   --capture <file.png> render until assets are loaded, save a screenshot and exit
//   --frames <n>         frames to render before --capture (default 5)
//   --capture-ui         include the editor UI in the --capture screenshot
//   --no-validation      run Debug builds without the Vulkan validation layer
//   --camera x,y,z,yaw,pitch   start camera (degrees)       --sun azimuth,elevation (degrees)
//   --view <n>           debug view (0 lit, 1 base color, 2 normals, 3 roughness, 4 metallic, 5 AO, 6 shadow)

#include "Sandbox.h"

#include "ember/core/Log.h"
#include "ember/core/Profile.h"
#include "ember/debugui/Theme.h"

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <ctime>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace sandbox {

namespace {

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
    options.scenePath = fs::path(EMBER_CONTENT_DIR) / "scenes/sandbox.scene.json";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--scene" && hasValue) {
            options.scenePath = argv[++i];
        } else if (arg == "--hdr") {
            options.useHdr = true;
        } else if (arg == "--capture" && hasValue) {
            options.capture = argv[++i];
        } else if (arg == "--capture-ui") {
            options.captureUi = true;
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
asset::Model makeTestShapes()
{
    asset::Model model;
    model.name = "Test shapes";

    const u32 ground = asset::addMaterial(model, {.baseColorFactor = vec4(0.45f, 0.45f, 0.45f, 1.0f),
                                                  .metallicFactor = 0.0f,
                                                  .roughnessFactor = 0.8f,
                                                  .name = "Ground"});
    model.instances.push_back({mat4(1.0f), asset::addPlaneMesh(model, ground, 40.0f)});

    constexpr int kSteps = 6;
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < kSteps; ++col) {
            const f32 roughness = static_cast<f32>(col) / static_cast<f32>(kSteps - 1);
            const vec4 color = row == 0 ? vec4(0.9f, 0.1f, 0.1f, 1.0f) : vec4(1.0f, 0.78f, 0.34f, 1.0f); // plastic, gold
            const u32 material = asset::addMaterial(model, {.baseColorFactor = color,
                                                            .metallicFactor = row == 0 ? 0.0f : 1.0f,
                                                            .roughnessFactor = roughness});
            const u32 mesh = asset::addSphereMesh(model, material, 0.4f);
            const vec3 position(-2.5f + static_cast<f32>(col), 0.45f + static_cast<f32>(row), -2.5f);
            model.instances.push_back({glm::translate(mat4(1.0f), position), mesh});
        }
    }

    const u32 white = asset::addMaterial(model, {.baseColorFactor = vec4(0.8f, 0.8f, 0.8f, 1.0f),
                                                 .metallicFactor = 0.0f,
                                                 .roughnessFactor = 0.5f});
    const u32 cube = asset::addCubeMesh(model, white, 0.5f);
    model.instances.push_back({glm::translate(mat4(1.0f), vec3(3.0f, 0.5f, 0.5f)), cube});
    model.instances.push_back({glm::rotate(glm::translate(mat4(1.0f), vec3(-3.0f, 0.75f, 0.8f)), 0.6f, vec3(0, 1, 0)) *
                                   glm::scale(mat4(1.0f), vec3(0.6f, 1.5f, 0.6f)),
                               cube});
    return model;
}

fs::path screenshotPath()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char name[64];
    std::strftime(name, sizeof(name), "ember-%Y%m%d-%H%M%S.png", &local);
    return fs::path(EMBER_CONTENT_DIR).parent_path() / "screenshots" / name;
}

// --- Systems: plain functions that loop over components ------------------------------------

void spinSystem(scene::Scene& scene, f32 dt)
{
    for (auto [entity, spin, transform] : scene.registry().view<scene::Spin, scene::Transform>().each()) {
        if (glm::length(spin.axis) > 0.0f) {
            transform.rotation = glm::normalize(glm::angleAxis(spin.speed * dt, glm::normalize(spin.axis)) * transform.rotation);
        }
    }
}

// Makes sure every model the scene references is loaded and uploaded, and drops the ones no longer used.
void syncModels(App& app)
{
    EMBER_PROFILE_FUNCTION();
    for (auto& [guid, loaded] : app.models) {
        loaded.usedThisFrame = guid == kTestSceneModel;
    }
    for (auto [entity, mesh] : app.scene.registry().view<scene::MeshRenderer>().each()) {
        if (!mesh.model.isValid()) {
            continue;
        }
        auto [it, inserted] = app.models.try_emplace(mesh.model);
        if (inserted) {
            app.assets->acquire(mesh.model);
        }
        LoadedModel& loaded = it->second;
        loaded.usedThisFrame = true;
        if (mesh.model == kTestSceneModel) {
            continue;
        }
        // New data (first load or hot reload): upload it, replacing the old GPU copy.
        const u32 version = app.assets->version(mesh.model);
        if (version != loaded.version && app.assets->state(mesh.model) == asset::AssetState::Ready) {
            app.device->waitIdle(); // the old copy may still be in use by in-flight frames
            app.renderer->destroyModel(loaded.handle);
            loaded.handle = app.renderer->uploadModel(*app.assets->model(mesh.model));
            loaded.version = version;
        }
    }
    for (auto it = app.models.begin(); it != app.models.end();) {
        if (!it->second.usedThisFrame) {
            app.renderer->destroyModel(it->second.handle);
            app.assets->release(it->first);
            it = app.models.erase(it);
        } else {
            ++it;
        }
    }
}

void syncEnvironment(App& app)
{
    if (!app.environmentAsset.isValid()) {
        return;
    }
    const u32 version = app.assets->version(app.environmentAsset);
    if (app.useHdr && app.assets->state(app.environmentAsset) == asset::AssetState::Ready &&
        (!app.environmentApplied || version != app.environmentVersion)) {
        app.renderer->setEnvironment(app.assets->environment(app.environmentAsset));
        app.environmentVersion = version;
        app.environmentApplied = true;
    } else if (!app.useHdr && !app.environmentApplied) {
        app.renderer->setEnvironment(nullptr);
        app.environmentApplied = true;
    }
}

// Builds the renderer's flat snapshot from the ECS. The renderer never sees entities.
void extractRenderScene(App& app)
{
    EMBER_PROFILE_FUNCTION();
    render::RenderScene& out = app.renderScene;
    out.instances.clear(); // keeps its capacity: no allocation in the steady state
    entt::registry& registry = app.scene.registry();
    for (auto [entity, mesh, world] : registry.view<scene::MeshRenderer, scene::WorldTransform>().each()) {
        const auto it = app.models.find(mesh.model);
        if (it != app.models.end() && it->second.handle.isValid()) {
            out.instances.push_back({it->second.handle, world.matrix});
        }
    }
    const auto lights = registry.view<scene::DirectionalLight, scene::WorldTransform>();
    if (lights.begin() != lights.end()) { // one sun: the first light found
        const auto [light, world] = lights.get(*lights.begin());
        out.sun.direction = glm::normalize(vec3(world.matrix[2])); // the light shines along -Z
        out.sun.color = light.color;
        out.sun.intensity = light.intensity;
    }
}

} // namespace

void FlyCamera::update(render::Camera& camera, platform::Window& window, f32 dt)
{
    const bool looking = window.isMouseDown(platform::MouseButton::Right);
    window.setRelativeMouseMode(looking);
    if (looking) {
        constexpr f32 sensitivity = 0.0025f;
        camera.yaw -= window.mouseDeltaX() * sensitivity;
        camera.pitch = glm::clamp(camera.pitch - window.mouseDeltaY() * sensitivity, -1.55f, 1.55f);
        speed = glm::clamp(speed * std::pow(1.2f, window.scrollDelta()), 0.1f, 200.0f);
    }
    using platform::Key;
    vec3 move(0.0f);
    move += camera.forward() * (static_cast<f32>(window.isKeyDown(Key::W)) - static_cast<f32>(window.isKeyDown(Key::S)));
    move += camera.right() * (static_cast<f32>(window.isKeyDown(Key::D)) - static_cast<f32>(window.isKeyDown(Key::A)));
    move.y += static_cast<f32>(window.isKeyDown(Key::E)) - static_cast<f32>(window.isKeyDown(Key::Q));
    if (glm::length(move) > 0.0f) {
        const f32 boost = window.isKeyDown(Key::LeftShift) ? 4.0f : 1.0f;
        camera.position += glm::normalize(move) * speed * boost * dt;
    }
}

void setSunAngles(App& app, f32 azimuthDegrees, f32 elevationDegrees)
{
    for (auto [entity, light, transform] : app.scene.registry().view<scene::DirectionalLight, scene::Transform>().each()) {
        const f32 az = glm::radians(azimuthDegrees);
        const f32 el = glm::radians(elevationDegrees);
        const vec3 towardSun(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
        transform.rotation = glm::quatLookAt(-towardSun, vec3(0, 1, 0)); // -Z points where the light travels
    }
}

void buildDefaultScene(App& app)
{
    scene::Scene& scene = app.scene;
    scene.clear();
    app.selected = scene::kNullEntity;

    const scene::Entity sun = scene.create("Sun");
    scene.registry().emplace<scene::DirectionalLight>(sun);
    setSunAngles(app, 35.0f, 40.0f);

    const scene::Entity shapes = scene.create("Test shapes");
    scene.registry().emplace<scene::MeshRenderer>(shapes, kTestSceneModel);

    // A spinning pivot with the helmet as its child: shows the hierarchy at work.
    const scene::Entity pivot = scene.create("Turntable");
    scene.registry().get<scene::Transform>(pivot).position = vec3(0.0f, 1.1f, 0.0f);
    scene.registry().emplace<scene::Spin>(pivot, scene::Spin{vec3(0, 1, 0), 0.4f});

    const Guid helmet = app.assets->findBySource("models/DamagedHelmet/DamagedHelmet.glb");
    if (helmet.isValid()) {
        const scene::Entity e = scene.create("Damaged Helmet", pivot);
        scene.registry().get<scene::Transform>(e).scale = vec3(0.8f);
        scene.registry().emplace<scene::MeshRenderer>(e, helmet);
    } else {
        EMBER_LOG_WARN("Helmet not cooked. Run tools/fetch-sample-content.ps1, then rebuild.");
    }
    scene.updateTransforms();
}

} // namespace sandbox

using namespace sandbox;

int main(int argc, char** argv)
{
    EMBER_LOG_INFO("Ember sandbox starting");
    App app;
    app.options = parseOptions(argc, argv);
    const Options& options = app.options;

    app.window = platform::Window::create({.title = "Ember Sandbox", .width = 1600, .height = 900});
    if (!app.window) {
        return 1;
    }
    rhi::DeviceDesc deviceDesc;
    deviceDesc.window = app.window->nativeHandle();
    deviceDesc.width = app.window->width();
    deviceDesc.height = app.window->height();
#if defined(EMBER_DEBUG)
    deviceDesc.enableValidation = options.validation;
#endif
    app.device = rhi::Device::create(deviceDesc);
    if (!app.device) {
        return 1;
    }
    app.renderer = render::Renderer::create(*app.device);
    if (!app.renderer) {
        return 1;
    }
    app.renderer->settings().debugView = static_cast<render::DebugView>(options.debugView);

    app.jobs = std::make_unique<JobSystem>();
    app.assets = std::make_unique<asset::AssetManager>(
        asset::AssetManagerDesc{.contentDir = EMBER_CONTENT_DIR, .cookedDir = EMBER_COOKED_DIR, .cookerExe = EMBER_COOKER_EXE},
        *app.jobs);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    debugui::applyTheme({
        .fontDirectory = EMBER_CONTENT_DIR "/fonts",
        .fontSize = 16.0f,
        .dpiScale = app.window->displayScale(),
        .linearOutput = app.device->isBackbufferSrgb(),
    });
    app.window->initImGui();
    app.device->initImGui();

    // --- Scene ---------------------------------------------------------------------------------
    {
        const render::ModelHandle shapes = app.renderer->uploadModel(makeTestShapes());
        app.models[kTestSceneModel] = LoadedModel{shapes, 1, true};
    }
    if (!(fs::exists(options.scenePath) && options.capture.empty() && app.scene.loadFile(options.scenePath))) {
        buildDefaultScene(app); // automated captures always use the default scene
    }
    if (options.sun) {
        setSunAngles(app, (*options.sun)[0], (*options.sun)[1]);
    }
    app.environmentAsset = app.assets->findBySource("hdri/sky.hdr");
    app.useHdr = options.useHdr && app.environmentAsset.isValid();
    if (app.environmentAsset.isValid()) {
        app.assets->acquire(app.environmentAsset);
    }

    render::Camera& camera = app.renderScene.camera;
    camera.position = vec3(0.0f, 1.6f, 4.5f);
    camera.pitch = glm::radians(-10.0f);
    if (options.camera) {
        const auto& c = *options.camera;
        camera.position = vec3(c[0], c[1], c[2]);
        camera.yaw = glm::radians(c[3]);
        camera.pitch = glm::radians(c[4]);
    }
    if (!options.capture.empty()) {
        app.scene.registry().clear<scene::Spin>(); // deterministic screenshots
    }

    // --- Main loop -----------------------------------------------------------------------------
    u32 readyFrames = 0;
    auto lastTime = std::chrono::steady_clock::now();
    while (true) {
        app.window->pollEvents();
        if (app.window->shouldClose()) {
            break;
        }
        if (app.window->isMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        if (app.window->consumeResized()) {
            app.device->resize(app.window->width(), app.window->height());
        }
        const auto now = std::chrono::steady_clock::now();
        const f32 dt = std::min(std::chrono::duration<f32>(now - lastTime).count(), 0.1f);
        lastTime = now;

        // Systems, in order.
        app.assets->update();
        app.renderer->reloadChangedShaders();
        app.fly.update(camera, *app.window, dt);
        spinSystem(app.scene, dt);
        app.scene.updateTransforms(app.jobs.get());
        syncModels(app);
        syncEnvironment(app);
        extractRenderScene(app);

        app.device->beginImGuiFrame();
        app.window->beginImGuiFrame();
        ImGui::NewFrame();
        drawEditor(app);
        ImGui::Render();

        // Automated captures wait until every asset has finished loading.
        const bool assetsSettled = app.assets->loadsInFlight() == 0;
        const bool lastCaptureFrame = !options.capture.empty() && assetsSettled && readyFrames + 1 >= options.captureFrames;
        if (app.window->wasKeyPressed(platform::Key::F12) || app.requestScreenshot) {
            app.renderer->requestCapture(screenshotPath());
            app.requestScreenshot = false;
        }
        if (lastCaptureFrame) {
            app.renderer->requestCapture(options.capture);
        }

        if (rhi::CommandList* cmd = app.device->beginFrame()) {
            app.renderer->render(*cmd, app.renderScene);
            if (options.capture.empty() || options.captureUi) {
                app.device->renderImGui(*cmd);
            }
            app.renderer->recordCapture(*cmd);
            app.device->endFrame();
            readyFrames += assetsSettled ? 1 : 0;
        }
        EMBER_PROFILE_FRAME();
        if (lastCaptureFrame) {
            break;
        }
    }

    app.device->waitIdle();
    app.renderer->finishCapture();
    for (auto& [guid, loaded] : app.models) {
        app.renderer->destroyModel(loaded.handle);
    }
    app.assets.reset();
    app.renderer.reset();
    app.device->shutdownImGui();
    app.window->shutdownImGui();
    ImGui::DestroyContext();
    EMBER_LOG_INFO("Shutting down");
    return 0;
}
