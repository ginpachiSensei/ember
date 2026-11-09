#pragma once

#include "ember/core/Math.h"
#include "ember/core/Memory.h"
#include "ember/core/SlotMap.h"
#include "ember/rhi/Device.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ember::asset {
struct Model;
struct HdrImage;
} // namespace ember::asset

namespace ember::render {

struct Camera {
    vec3 position{0.0f, 1.0f, 3.0f};
    f32 yaw = 0.0f;   // radians; 0 looks down -Z
    f32 pitch = 0.0f; // radians; positive looks up
    f32 verticalFov = glm::radians(60.0f);
    f32 nearPlane = 0.05f;

    vec3 forward() const;
    vec3 right() const;
    mat4 view() const;
};

struct DirectionalLight {
    vec3 direction = glm::normalize(vec3(0.4f, 0.8f, 0.3f)); // points towards the light
    vec3 color{1.0f, 0.96f, 0.9f};
    f32 intensity = 6.0f;
};

struct ModelTag;
using ModelHandle = Handle<ModelTag>;

struct RenderInstance {
    ModelHandle model;
    mat4 transform{1.0f};
};

// Everything the renderer needs for one frame: a flat snapshot, filled in by the game side.
// The renderer never looks at game objects directly.
struct RenderScene {
    Camera camera;
    DirectionalLight sun;
    std::vector<RenderInstance, memory::TrackedAllocator<RenderInstance, memory::Tag::Render>> instances;
};

enum class Tonemapper : u32 { None, Reinhard, Aces, AgX, PbrNeutral };
enum class DebugView : u32 { Lit, BaseColor, Normals, Roughness, Metallic, Occlusion, Shadow };

struct RenderSettings {
    f32 exposureEv = -1.0f; // exposure in stops (EV); 0 = x1
    Tonemapper tonemapper = Tonemapper::AgX;
    DebugView debugView = DebugView::Lit;
    f32 iblIntensity = 1.0f;
    f32 skyIntensity = 1.0f;
    u32 shadowMapSize = 2048;
    f32 shadowBiasConstant = 1.5f; // depth bias in depth-buffer units
    f32 shadowBiasSlope = 2.0f;
    f32 shadowNormalBias = 2.5f;   // in shadow-map texels
};

struct RenderStats {
    usize frameMemoryUsed = 0; // the renderer's per-frame scratch (draw list)
    usize frameMemoryCapacity = 0;
    u32 drawCalls = 0;
    u32 triangles = 0;
    u32 shadowDrawCalls = 0;
};

struct RendererDesc {
    // Where compiled .spv files live, and where their .slang sources are (for hot reload).
    // Empty = the paths baked in at build time.
    std::filesystem::path shaderBinaryDir;
    std::filesystem::path shaderSourceDir;
};

class Renderer {
public:
    static std::unique_ptr<Renderer> create(rhi::Device& device, const RendererDesc& desc = {});
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Copies a model's geometry, materials and textures to the GPU (blocking).
    ModelHandle uploadModel(const asset::Model& model);
    void destroyModel(ModelHandle model);
    // Bounds of the model's own scene (before the instance transform).
    void modelBounds(ModelHandle model, vec3& outMin, vec3& outMax) const;

    // nullptr = the procedural sky (follows the sun); otherwise an equirectangular HDR image.
    void setEnvironment(const asset::HdrImage* hdr);

    // Records the whole frame into `cmd`, ending with the image in the backbuffer.
    // ImGui (if any) can be drawn on top afterwards.
    void render(rhi::CommandList& cmd, const RenderScene& scene);

    // Recompiles shaders whose source changed since the last call and rebuilds their pipelines.
    // Cheap when nothing changed. Returns true if anything was reloaded.
    bool reloadChangedShaders();
    const std::string& shaderError() const; // last compile error, empty if none

    // Saves a frame as a PNG: requestCapture(), then recordCapture(cmd) as the last thing in a frame
    // (after any UI). The file is written by the next render() call or finishCapture().
    void requestCapture(const std::filesystem::path& path);
    void recordCapture(rhi::CommandList& cmd); // no-op unless a capture was requested
    void finishCapture(); // call after Device::waitIdle() to write a pending capture immediately

    RenderSettings& settings();
    const RenderStats& stats() const;

    struct Impl;

private:
    explicit Renderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace ember::render
