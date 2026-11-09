#include "ember/render/Renderer.h"

#include "GpuTypes.h"
#include "ShaderLibrary.h"

#include "ember/asset/Model.h"
#include "ember/core/Assert.h"
#include "ember/core/Log.h"
#include "ember/core/Profile.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <span>

namespace ember::render {

using namespace rhi;

// ---------------------------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------------------------

vec3 Camera::forward() const
{
    return vec3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
}

vec3 Camera::right() const
{
    return glm::normalize(glm::cross(forward(), vec3(0, 1, 0)));
}

mat4 Camera::view() const
{
    return glm::lookAt(position, position + forward(), vec3(0, 1, 0));
}

// ---------------------------------------------------------------------------------------------
// Renderer internals
// ---------------------------------------------------------------------------------------------

namespace {

constexpr Format kHdrFormat = Format::RGBA16Float;
constexpr Format kDepthFormat = Format::D32Float;
constexpr Format kShadowFormat = Format::D32Float;
constexpr u32 kEnvironmentSize = 512;
constexpr u32 kIrradianceSize = 32;
constexpr u32 kPrefilterSize = 256;
constexpr u32 kPrefilterMips = 6; // 256 .. 8; roughness 0 .. 1
constexpr u32 kBrdfLutSize = 256;

struct GpuPrimitive {
    u32 firstIndex;
    u32 indexCount;
    u32 firstVertex;
    u32 material;
};

struct GpuModel {
    BufferHandle vertices;
    BufferHandle indices;
    BufferHandle materials;
    std::vector<TextureHandle> textures;
    std::vector<GpuPrimitive> primitives;
    std::vector<asset::Mesh> meshes;
    std::vector<asset::Instance> instances;
    std::vector<u32> materialFlags;
    vec3 boundsMin{0.0f};
    vec3 boundsMax{0.0f};
};

// One mesh primitive to draw this frame. The list is built once and used by every pass.
struct DrawItem {
    const GpuModel* model;
    const GpuPrimitive* primitive;
    mat4 world;
};

struct Pipelines {
    PipelineHandle meshOpaque;
    PipelineHandle meshDoubleSided;
    PipelineHandle shadowOpaque;
    PipelineHandle shadowMasked;
    PipelineHandle sky;
    PipelineHandle tonemap;
    PipelineHandle iblProceduralSky;
    PipelineHandle iblEquirect;
    PipelineHandle iblIrradiance;
    PipelineHandle iblPrefilter;
    PipelineHandle iblBrdfLut;
};

u32 groups(u32 size)
{
    return (size + 7) / 8;
}

} // namespace

struct Renderer::Impl {
    Device& device;
    ShaderLibrary shaders;
    RenderSettings settings;
    RenderStats stats;
    Pipelines pipelines;

    SlotMap<GpuModel, ModelTag> models;
    f32 builtShadowBiasConstant = 0.0f; // shadow pipelines bake the depth bias in
    f32 builtShadowBiasSlope = 0.0f;

    SamplerHandle linearSampler;
    SamplerHandle clampSampler;
    SamplerHandle pointClampSampler;
    SamplerHandle shadowSampler;

    TextureHandle whiteTexture;
    TextureHandle flatNormalTexture;
    TextureHandle blackTexture;

    // Screen-sized targets, recreated when the backbuffer size changes.
    TextureHandle hdrTarget;
    TextureHandle depthTarget;
    u32 targetWidth = 0;
    u32 targetHeight = 0;

    TextureHandle shadowMap;
    u32 shadowMapSize = 0;

    // Image-based lighting.
    TextureHandle environment; // radiance cube the sky and the filters read
    TextureHandle irradiance;
    TextureHandle prefiltered;
    TextureHandle brdfLut;
    TextureHandle equirect; // source HDR image, if any
    bool environmentDirty = true;
    bool brdfLutDirty = true;
    vec3 bakedSunDirection{0.0f};

    std::array<BufferHandle, kFramesInFlight> frameBuffers;

    std::filesystem::path capturePath;
    BufferHandle captureBuffer;
    bool capturePending = false; // a copy was recorded and the file not yet written
    u32 captureWidth = 0;
    u32 captureHeight = 0;

    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastShaderPoll = startTime;

    Impl(Device& d, std::filesystem::path binaryDir, std::filesystem::path sourceDir)
        : device(d)
        , shaders(std::move(binaryDir), std::move(sourceDir))
    {
    }

    void init();
    void shutdown();
    bool createPipelines();
    void destroyPipelines(Pipelines& p);
    void ensureTargets();
    void ensureShadowMap();
    void bakeEnvironment(const vec3& sunDirection);
    TextureHandle createSolidTexture(u32 rgba, const char* name);
    void writeCapture();

    // Per-frame scratch memory: the draw list lives here and is thrown away next frame.
    FrameAllocator frameMemory{2 * 1024 * 1024};
    std::span<DrawItem> buildDrawList(const RenderScene& scene);
};

TextureHandle Renderer::Impl::createSolidTexture(u32 rgba, const char* name)
{
    const TextureHandle texture = device.createTexture({.width = 1,
                                                        .height = 1,
                                                        .format = Format::RGBA8Unorm,
                                                        .usage = TextureUsage::Sampled | TextureUsage::TransferDst,
                                                        .debugName = name});
    device.uploadTexture(texture, &rgba, sizeof(rgba));
    return texture;
}

void Renderer::Impl::init()
{
    linearSampler = device.createSampler({.filter = Filter::Linear, .address = AddressMode::Repeat, .maxAnisotropy = 16.0f});
    clampSampler = device.createSampler({.filter = Filter::Linear, .address = AddressMode::ClampToEdge});
    pointClampSampler = device.createSampler({.filter = Filter::Nearest, .address = AddressMode::ClampToEdge});
    shadowSampler = device.createSampler(
        {.filter = Filter::Linear, .address = AddressMode::ClampToEdge, .compare = true, .compareOp = CompareOp::LessEqual});

    whiteTexture = createSolidTexture(0xffffffffu, "White");
    blackTexture = createSolidTexture(0xff000000u, "Black");
    flatNormalTexture = createSolidTexture(0xffff8080u, "Flat normal"); // (0.5, 0.5, 1.0)

    const TextureUsage cubeUsage = TextureUsage::Sampled | TextureUsage::Storage;
    environment = device.createTexture({.width = kEnvironmentSize,
                                        .height = kEnvironmentSize,
                                        .mipLevels = mipCount(kEnvironmentSize, kEnvironmentSize),
                                        .type = TextureType::TextureCube,
                                        .format = Format::RGBA16Float,
                                        .usage = cubeUsage | TextureUsage::TransferSrc | TextureUsage::TransferDst,
                                        .debugName = "Environment"});
    irradiance = device.createTexture({.width = kIrradianceSize,
                                       .height = kIrradianceSize,
                                       .type = TextureType::TextureCube,
                                       .format = Format::RGBA16Float,
                                       .usage = cubeUsage,
                                       .debugName = "Irradiance"});
    prefiltered = device.createTexture({.width = kPrefilterSize,
                                        .height = kPrefilterSize,
                                        .mipLevels = kPrefilterMips,
                                        .type = TextureType::TextureCube,
                                        .format = Format::RGBA16Float,
                                        .usage = cubeUsage,
                                        .debugName = "Prefiltered specular"});
    brdfLut = device.createTexture({.width = kBrdfLutSize,
                                    .height = kBrdfLutSize,
                                    .format = Format::RGBA16Float,
                                    .usage = TextureUsage::Sampled | TextureUsage::Storage,
                                    .debugName = "BRDF LUT"});

    for (BufferHandle& buffer : frameBuffers) {
        buffer = device.createBuffer(
            {.size = sizeof(gpu::FrameData), .usage = BufferUsage::Storage, .memory = MemoryType::Upload, .debugName = "Frame data"});
    }

    createPipelines();
}

void Renderer::Impl::shutdown()
{
    device.waitIdle();
    if (capturePending) {
        writeCapture();
    }
    models.forEach([&](GpuModel& model) {
        device.destroyBuffer(model.vertices);
        device.destroyBuffer(model.indices);
        device.destroyBuffer(model.materials);
        for (TextureHandle texture : model.textures) {
            device.destroyTexture(texture);
        }
    });
    destroyPipelines(pipelines);
    for (BufferHandle buffer : frameBuffers) {
        device.destroyBuffer(buffer);
    }
    for (TextureHandle texture : {whiteTexture, blackTexture, flatNormalTexture, hdrTarget, depthTarget, shadowMap,
                                  environment, irradiance, prefiltered, brdfLut, equirect}) {
        device.destroyTexture(texture);
    }
    device.destroyBuffer(captureBuffer);
}

bool Renderer::Impl::createPipelines()
{
    EMBER_PROFILE_FUNCTION();
    const std::vector<u32>& mesh = shaders.get("mesh");
    const std::vector<u32>& sky = shaders.get("sky");
    const std::vector<u32>& tonemap = shaders.get("tonemap");
    const std::vector<u32>& ibl = shaders.get("ibl");
    if (mesh.empty() || sky.empty() || tonemap.empty() || ibl.empty()) {
        return false;
    }

    Pipelines p;
    {
        GraphicsPipelineDesc desc;
        desc.vertex = {mesh, "vsMain"};
        desc.fragment = {mesh, "psMain"};
        desc.colorFormats[0] = kHdrFormat;
        desc.colorCount = 1;
        desc.depthFormat = kDepthFormat;
        desc.depthTest = true;
        desc.depthWrite = true;
        desc.depthCompare = CompareOp::Greater;
        desc.cull = CullMode::Back;
        desc.debugName = "Mesh";
        p.meshOpaque = device.createGraphicsPipeline(desc);
        desc.cull = CullMode::None;
        desc.debugName = "Mesh (double-sided)";
        p.meshDoubleSided = device.createGraphicsPipeline(desc);
    }
    {
        GraphicsPipelineDesc desc;
        desc.vertex = {mesh, "vsShadow"};
        desc.depthFormat = kShadowFormat;
        desc.depthTest = true;
        desc.depthWrite = true;
        desc.depthCompare = CompareOp::LessEqual; // shadow maps use standard depth: 0 near, 1 far
        desc.depthClamp = true;                   // casters behind the near plane still cast
        desc.cull = CullMode::None;               // thin and open meshes cast from both sides
        desc.depthBiasConstant = settings.shadowBiasConstant;
        desc.depthBiasSlope = settings.shadowBiasSlope;
        desc.debugName = "Shadow";
        p.shadowOpaque = device.createGraphicsPipeline(desc);
        desc.fragment = {mesh, "psShadowMasked"};
        desc.debugName = "Shadow (alpha mask)";
        p.shadowMasked = device.createGraphicsPipeline(desc);
    }
    {
        GraphicsPipelineDesc desc;
        desc.vertex = {sky, "vsMain"};
        desc.fragment = {sky, "psMain"};
        desc.colorFormats[0] = kHdrFormat;
        desc.colorCount = 1;
        desc.depthFormat = kDepthFormat;
        desc.depthTest = true;
        desc.depthWrite = false;
        desc.depthCompare = CompareOp::GreaterEqual; // the triangle sits at depth 0: only empty pixels pass
        desc.cull = CullMode::None;
        desc.debugName = "Sky";
        p.sky = device.createGraphicsPipeline(desc);
    }
    {
        GraphicsPipelineDesc desc;
        desc.vertex = {tonemap, "vsMain"};
        desc.fragment = {tonemap, "psMain"};
        desc.colorFormats[0] = device.backbufferFormat();
        desc.colorCount = 1;
        desc.cull = CullMode::None;
        desc.debugName = "Tonemap";
        p.tonemap = device.createGraphicsPipeline(desc);
    }
    p.iblProceduralSky = device.createComputePipeline({{ibl, "csProceduralSky"}, "IBL procedural sky"});
    p.iblEquirect = device.createComputePipeline({{ibl, "csEquirectToCube"}, "IBL equirect to cube"});
    p.iblIrradiance = device.createComputePipeline({{ibl, "csIrradiance"}, "IBL irradiance"});
    p.iblPrefilter = device.createComputePipeline({{ibl, "csPrefilter"}, "IBL prefilter"});
    p.iblBrdfLut = device.createComputePipeline({{ibl, "csBrdfLut"}, "IBL BRDF LUT"});

    const PipelineHandle all[] = {p.meshOpaque, p.meshDoubleSided, p.shadowOpaque, p.shadowMasked, p.sky, p.tonemap,
                                  p.iblProceduralSky, p.iblEquirect, p.iblIrradiance, p.iblPrefilter, p.iblBrdfLut};
    const bool ok = std::all_of(std::begin(all), std::end(all), [](PipelineHandle h) { return h.isValid(); });
    if (!ok) {
        EMBER_LOG_ERROR("Some pipelines failed to build; keeping the previous ones");
        destroyPipelines(p);
        return false;
    }
    destroyPipelines(pipelines);
    pipelines = p;
    builtShadowBiasConstant = settings.shadowBiasConstant;
    builtShadowBiasSlope = settings.shadowBiasSlope;
    return true;
}

void Renderer::Impl::destroyPipelines(Pipelines& p)
{
    for (PipelineHandle* h : {&p.meshOpaque, &p.meshDoubleSided, &p.shadowOpaque, &p.shadowMasked, &p.sky, &p.tonemap,
                              &p.iblProceduralSky, &p.iblEquirect, &p.iblIrradiance, &p.iblPrefilter, &p.iblBrdfLut}) {
        device.destroyPipeline(*h);
        *h = {};
    }
}

void Renderer::Impl::ensureTargets()
{
    const u32 width = device.backbufferWidth();
    const u32 height = device.backbufferHeight();
    if (width == targetWidth && height == targetHeight && hdrTarget.isValid()) {
        return;
    }
    device.destroyTexture(hdrTarget);
    device.destroyTexture(depthTarget);
    hdrTarget = device.createTexture({.width = width,
                                      .height = height,
                                      .format = kHdrFormat,
                                      .usage = TextureUsage::ColorAttachment | TextureUsage::Sampled,
                                      .debugName = "HDR color"});
    depthTarget = device.createTexture({.width = width,
                                        .height = height,
                                        .format = kDepthFormat,
                                        .usage = TextureUsage::DepthAttachment | TextureUsage::Sampled,
                                        .debugName = "Depth"});
    targetWidth = width;
    targetHeight = height;
}

void Renderer::Impl::ensureShadowMap()
{
    const u32 size = std::clamp(settings.shadowMapSize, 256u, 8192u);
    if (size == shadowMapSize && shadowMap.isValid()) {
        return;
    }
    device.destroyTexture(shadowMap);
    shadowMap = device.createTexture({.width = size,
                                      .height = size,
                                      .format = kShadowFormat,
                                      .usage = TextureUsage::DepthAttachment | TextureUsage::Sampled,
                                      .debugName = "Shadow map"});
    shadowMapSize = size;
}

void Renderer::Impl::bakeEnvironment(const vec3& sunDirection)
{
    EMBER_PROFILE_FUNCTION();
    const auto start = std::chrono::steady_clock::now();
    const u32 clamp = device.bindlessIndex(clampSampler);

    if (brdfLutDirty) {
        device.immediateSubmit([&](CommandList& cmd) {
            cmd.barrier(brdfLut, ResourceState::ShaderReadWrite);
            cmd.bindPipeline(pipelines.iblBrdfLut);
            gpu::IblPush push{};
            push.target = device.storageIndex(brdfLut);
            push.size = kBrdfLutSize;
            cmd.pushConstants(push);
            cmd.dispatch(groups(kBrdfLutSize), groups(kBrdfLutSize));
            cmd.barrier(brdfLut, ResourceState::ShaderRead);
        });
        brdfLutDirty = false;
    }

    // 1. Environment radiance, mip 0: either the procedural sky or the HDR image.
    device.immediateSubmit([&](CommandList& cmd) {
        cmd.barrier(environment, ResourceState::ShaderReadWrite);
        gpu::IblPush push{};
        push.target = device.storageIndex(environment, 0);
        push.sampler = clamp;
        push.size = kEnvironmentSize;
        push.sunDirection = glm::normalize(sunDirection);
        if (equirect.isValid()) {
            cmd.bindPipeline(pipelines.iblEquirect);
            push.source = device.bindlessIndex(equirect);
        } else {
            cmd.bindPipeline(pipelines.iblProceduralSky);
        }
        cmd.pushConstants(push);
        cmd.dispatch(groups(kEnvironmentSize), groups(kEnvironmentSize), 6);
        cmd.barrier(environment, ResourceState::ShaderRead);
    });
    // 2. Blurred mips, so the filters below can read pre-averaged light (fewer samples, no fireflies).
    device.generateMips(environment);

    // 3. Diffuse irradiance and specular prefilter.
    device.immediateSubmit([&](CommandList& cmd) {
        const u32 source = device.bindlessIndex(environment);
        cmd.barrier(irradiance, ResourceState::ShaderReadWrite);
        cmd.barrier(prefiltered, ResourceState::ShaderReadWrite);

        gpu::IblPush push{};
        push.source = source;
        push.sampler = clamp;
        push.sourceSize = static_cast<f32>(kEnvironmentSize);

        cmd.bindPipeline(pipelines.iblIrradiance);
        push.target = device.storageIndex(irradiance, 0);
        push.size = kIrradianceSize;
        cmd.pushConstants(push);
        cmd.dispatch(groups(kIrradianceSize), groups(kIrradianceSize), 6);

        cmd.bindPipeline(pipelines.iblPrefilter);
        for (u32 mip = 0; mip < kPrefilterMips; ++mip) {
            push.target = device.storageIndex(prefiltered, mip);
            push.size = std::max(1u, kPrefilterSize >> mip);
            push.roughness = static_cast<f32>(mip) / static_cast<f32>(kPrefilterMips - 1);
            cmd.pushConstants(push);
            cmd.dispatch(groups(push.size), groups(push.size), 6);
        }
        cmd.barrier(irradiance, ResourceState::ShaderRead);
        cmd.barrier(prefiltered, ResourceState::ShaderRead);
    });

    bakedSunDirection = sunDirection;
    environmentDirty = false;
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
    EMBER_LOG_INFO("Baked image-based lighting in {:.1f} ms", ms);
}

std::span<DrawItem> Renderer::Impl::buildDrawList(const RenderScene& scene)
{
    EMBER_PROFILE_FUNCTION();
    usize count = 0;
    for (const RenderInstance& instance : scene.instances) {
        if (const GpuModel* model = models.get(instance.model)) {
            for (const asset::Instance& node : model->instances) {
                count += model->meshes[node.mesh].primitiveCount;
            }
        }
    }
    DrawItem* draws = static_cast<DrawItem*>(frameMemory.allocate(count * sizeof(DrawItem), alignof(DrawItem)));
    usize i = 0;
    for (const RenderInstance& instance : scene.instances) {
        const GpuModel* model = models.get(instance.model);
        if (!model) {
            continue;
        }
        for (const asset::Instance& node : model->instances) {
            const mat4 world = instance.transform * node.transform;
            const asset::Mesh& mesh = model->meshes[node.mesh];
            for (u32 p = mesh.firstPrimitive; p < mesh.firstPrimitive + mesh.primitiveCount; ++p) {
                draws[i++] = {model, &model->primitives[p], world};
            }
        }
    }
    return {draws, count};
}

void Renderer::Impl::writeCapture()
{
    if (!capturePending) {
        return;
    }
    const auto* src = static_cast<const u8*>(device.mappedData(captureBuffer));
    std::vector<u8> rgba(static_cast<usize>(captureWidth) * captureHeight * 4);
    const Format format = device.backbufferFormat();
    const bool bgra = format == Format::BGRA8Srgb || format == Format::BGRA8Unorm;
    for (usize i = 0; i < rgba.size(); i += 4) {
        rgba[i + 0] = src[i + (bgra ? 2 : 0)];
        rgba[i + 1] = src[i + 1];
        rgba[i + 2] = src[i + (bgra ? 0 : 2)];
        rgba[i + 3] = 255;
    }
    if (asset::writePng(capturePath, captureWidth, captureHeight, rgba.data())) {
        EMBER_LOG_INFO("Saved screenshot {}", capturePath.string());
    }
    capturePending = false;
    capturePath.clear();
}

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

std::unique_ptr<Renderer> Renderer::create(Device& device, const RendererDesc& desc)
{
    std::filesystem::path binaryDir = desc.shaderBinaryDir;
    std::filesystem::path sourceDir = desc.shaderSourceDir;
#if defined(EMBER_SHADER_BINARY_DIR)
    if (binaryDir.empty()) {
        binaryDir = EMBER_SHADER_BINARY_DIR;
    }
#endif
#if defined(EMBER_SHADER_SOURCE_DIR)
    if (sourceDir.empty()) {
        sourceDir = EMBER_SHADER_SOURCE_DIR;
    }
#endif
    auto impl = std::make_unique<Impl>(device, binaryDir, sourceDir);
    impl->init();
    if (!impl->pipelines.meshOpaque.isValid()) {
        EMBER_LOG_ERROR("Renderer: failed to create pipelines");
        impl->shutdown();
        return nullptr;
    }
    return std::unique_ptr<Renderer>(new Renderer(std::move(impl)));
}

Renderer::Renderer(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

Renderer::~Renderer()
{
    m_impl->shutdown();
}

ModelHandle Renderer::uploadModel(const asset::Model& model)
{
    EMBER_PROFILE_FUNCTION();
    Impl& r = *m_impl;
    Device& device = r.device;
    if (model.vertices.empty() || model.indices.empty()) {
        EMBER_LOG_WARN("Model '{}' has no geometry", model.name);
        return {};
    }

    GpuModel gpuModel;
    gpuModel.vertices = device.createBuffer({.size = model.vertices.size() * sizeof(asset::Vertex),
                                             .usage = BufferUsage::Storage,
                                             .debugName = "Model vertices"},
                                            model.vertices.data());
    gpuModel.indices = device.createBuffer({.size = model.indices.size() * sizeof(u32),
                                            .usage = BufferUsage::Index,
                                            .debugName = "Model indices"},
                                           model.indices.data());

    // Textures: full mip chains so distant surfaces don't shimmer.
    for (const asset::Image& image : model.images) {
        if (image.pixels.empty()) {
            gpuModel.textures.push_back({}); // failed to decode; materials fall back to defaults
            continue;
        }
        const bool cookedMips = image.mipLevels > 1;
        const TextureHandle texture = device.createTexture(
            {.width = image.width,
             .height = image.height,
             .mipLevels = cookedMips ? image.mipLevels : mipCount(image.width, image.height),
             .format = image.srgb ? Format::RGBA8Srgb : Format::RGBA8Unorm,
             .usage = TextureUsage::Sampled | TextureUsage::TransferSrc | TextureUsage::TransferDst,
             .debugName = image.name.c_str()});
        if (cookedMips) {
            // The cooker already built the mip chain (with sRGB-correct filtering): upload it as-is.
            usize offset = 0;
            for (u32 mip = 0; mip < image.mipLevels; ++mip) {
                const usize size = static_cast<usize>(std::max(1u, image.width >> mip)) *
                                   std::max(1u, image.height >> mip) * 4;
                EMBER_VERIFY(offset + size <= image.pixels.size());
                device.uploadTexture(texture, image.pixels.data() + offset, size, mip);
                offset += size;
            }
        } else {
            device.uploadTexture(texture, image.pixels.data(), image.pixels.size());
            device.generateMips(texture);
        }
        gpuModel.textures.push_back(texture);
    }

    auto textureIndex = [&](i32 image, TextureHandle fallback) {
        if (image >= 0 && static_cast<usize>(image) < gpuModel.textures.size() &&
            gpuModel.textures[static_cast<usize>(image)].isValid()) {
            return device.bindlessIndex(gpuModel.textures[static_cast<usize>(image)]);
        }
        return device.bindlessIndex(fallback);
    };

    std::vector<gpu::Material> materials;
    for (const asset::Material& m : model.materials) {
        gpu::Material g{};
        g.baseColorFactor = m.baseColorFactor;
        g.emissiveFactor = m.emissiveFactor;
        g.metallicFactor = m.metallicFactor;
        g.roughnessFactor = m.roughnessFactor;
        g.normalScale = m.normalScale;
        g.occlusionStrength = m.occlusionImage >= 0 ? m.occlusionStrength : 0.0f;
        g.alphaCutoff = m.alphaCutoff;
        g.baseColorTexture = textureIndex(m.baseColorImage, r.whiteTexture);
        g.metallicRoughnessTexture = textureIndex(m.metallicRoughnessImage, r.whiteTexture);
        g.normalTexture = textureIndex(m.normalImage, r.flatNormalTexture);
        g.occlusionTexture = textureIndex(m.occlusionImage, r.whiteTexture);
        g.emissiveTexture = textureIndex(m.emissiveImage, r.whiteTexture);
        g.flags = (m.alphaMode == asset::AlphaMode::Mask ? gpu::kMaterialAlphaMask : 0u) |
                  (m.doubleSided ? gpu::kMaterialDoubleSided : 0u);
        materials.push_back(g);
        gpuModel.materialFlags.push_back(g.flags);
    }
    gpuModel.materials = device.createBuffer(
        {.size = materials.size() * sizeof(gpu::Material), .usage = BufferUsage::Storage, .debugName = "Model materials"},
        materials.data());

    for (const asset::Primitive& p : model.primitives) {
        gpuModel.primitives.push_back({p.firstIndex, p.indexCount, p.firstVertex, p.material});
    }
    gpuModel.meshes = model.meshes;
    gpuModel.instances = model.instances;
    if (gpuModel.instances.empty()) {
        for (u32 m = 0; m < model.meshes.size(); ++m) {
            gpuModel.instances.push_back({mat4(1.0f), m}); // procedural models: every mesh once, at the origin
        }
    }
    gpuModel.boundsMin = model.boundsMin();
    gpuModel.boundsMax = model.boundsMax();
    if (model.instances.empty()) {
        gpuModel.boundsMin = vec3(std::numeric_limits<f32>::max());
        gpuModel.boundsMax = vec3(std::numeric_limits<f32>::lowest());
        for (const asset::Primitive& p : model.primitives) {
            gpuModel.boundsMin = glm::min(gpuModel.boundsMin, p.boundsMin);
            gpuModel.boundsMax = glm::max(gpuModel.boundsMax, p.boundsMax);
        }
    }
    return r.models.emplace(std::move(gpuModel));
}

void Renderer::destroyModel(ModelHandle handle)
{
    Impl& r = *m_impl;
    GpuModel* model = r.models.get(handle);
    if (!model) {
        return;
    }
    r.device.destroyBuffer(model->vertices);
    r.device.destroyBuffer(model->indices);
    r.device.destroyBuffer(model->materials);
    for (TextureHandle texture : model->textures) {
        r.device.destroyTexture(texture);
    }
    r.models.remove(handle);
}

void Renderer::modelBounds(ModelHandle handle, vec3& outMin, vec3& outMax) const
{
    const GpuModel* model = m_impl->models.get(handle);
    outMin = model ? model->boundsMin : vec3(0.0f);
    outMax = model ? model->boundsMax : vec3(0.0f);
}

void Renderer::setEnvironment(const asset::HdrImage* hdr)
{
    Impl& r = *m_impl;
    r.device.destroyTexture(r.equirect);
    r.equirect = {};
    if (hdr && hdr->width > 0) {
        r.equirect = r.device.createTexture({.width = hdr->width,
                                             .height = hdr->height,
                                             .format = Format::RGBA32Float,
                                             .usage = TextureUsage::Sampled | TextureUsage::TransferDst,
                                             .debugName = "Environment (equirect)"});
        r.device.uploadTexture(r.equirect, hdr->pixels.data(), hdr->pixels.size() * sizeof(f32));
    }
    r.environmentDirty = true;
}

void Renderer::render(CommandList& cmd, const RenderScene& scene)
{
    EMBER_PROFILE_FUNCTION();
    Impl& r = *m_impl;
    Device& device = r.device;
    r.stats = {};
    r.frameMemory.beginFrame();

    if (r.capturePending) {
        // The copy recorded kFramesInFlight frames ago is complete once we're recording again; to keep
        // this simple, wait for the GPU here (screenshots are rare).
        device.waitIdle();
        r.writeCapture();
    }

    r.ensureTargets();
    r.ensureShadowMap();
    if (r.settings.shadowBiasConstant != r.builtShadowBiasConstant ||
        r.settings.shadowBiasSlope != r.builtShadowBiasSlope) {
        r.createPipelines(); // depth bias is fixed pipeline state
    }
    const vec3 sunDir = glm::normalize(scene.sun.direction);
    // The procedural sky follows the sun, so re-bake when it moves (HDR environments don't care).
    if (!r.equirect.isValid() && glm::length(sunDir - r.bakedSunDirection) > 1e-4f) {
        r.environmentDirty = true;
    }
    if (r.environmentDirty) {
        r.bakeEnvironment(sunDir);
    }

    // --- Scene bounds -> shadow camera ----------------------------------------------------------
    vec3 sceneMin(std::numeric_limits<f32>::max());
    vec3 sceneMax(std::numeric_limits<f32>::lowest());
    for (const RenderInstance& instance : scene.instances) {
        const GpuModel* model = r.models.get(instance.model);
        if (!model) {
            continue;
        }
        for (int corner = 0; corner < 8; ++corner) {
            const vec3 local((corner & 1) ? model->boundsMax.x : model->boundsMin.x,
                             (corner & 2) ? model->boundsMax.y : model->boundsMin.y,
                             (corner & 4) ? model->boundsMax.z : model->boundsMin.z);
            const vec3 world = vec3(instance.transform * vec4(local, 1.0f));
            sceneMin = glm::min(sceneMin, world);
            sceneMax = glm::max(sceneMax, world);
        }
    }
    if (sceneMin.x > sceneMax.x) {
        sceneMin = vec3(-1.0f);
        sceneMax = vec3(1.0f);
    }
    const vec3 center = (sceneMin + sceneMax) * 0.5f;
    const f32 radius = std::max(glm::length(sceneMax - sceneMin) * 0.5f, 0.01f);
    const vec3 up = std::abs(sunDir.y) > 0.99f ? vec3(0, 0, 1) : vec3(0, 1, 0);
    mat4 lightView = glm::lookAt(center + sunDir * radius * 2.0f, center, up);
    // Snap the light's origin to whole shadow-map texels, so edges don't crawl when the scene moves.
    const f32 texelWorld = 2.0f * radius / static_cast<f32>(r.shadowMapSize);
    vec4 origin = lightView * vec4(center, 1.0f);
    const vec2 snapped = glm::floor(vec2(origin) / texelWorld) * texelWorld;
    lightView = glm::translate(mat4(1.0f), vec3(snapped - vec2(origin), 0.0f)) * lightView;
    const mat4 lightProj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, radius * 4.0f);
    const mat4 lightViewProj = lightProj * lightView;

    // --- Frame constants -----------------------------------------------------------------------
    const f32 aspect = static_cast<f32>(r.targetWidth) / static_cast<f32>(std::max(r.targetHeight, 1u));
    const mat4 view = scene.camera.view();
    const mat4 proj = perspectiveReverseZ(scene.camera.verticalFov, aspect, scene.camera.nearPlane);

    gpu::FrameData frame{};
    frame.view = view;
    frame.proj = proj;
    frame.viewProj = proj * view;
    frame.invViewProj = glm::inverse(frame.viewProj);
    frame.lightViewProj = lightViewProj;
    frame.cameraPosition = scene.camera.position;
    frame.time = std::chrono::duration<f32>(std::chrono::steady_clock::now() - r.startTime).count();
    frame.sunDirection = sunDir;
    frame.sunIntensity = scene.sun.intensity;
    frame.sunColor = scene.sun.color;
    frame.iblIntensity = r.settings.iblIntensity;
    frame.irradianceMap = device.bindlessIndex(r.irradiance);
    frame.prefilteredMap = device.bindlessIndex(r.prefiltered);
    frame.brdfLut = device.bindlessIndex(r.brdfLut);
    frame.prefilteredMipCount = kPrefilterMips;
    frame.shadowMap = device.bindlessIndex(r.shadowMap);
    frame.linearSampler = device.bindlessIndex(r.linearSampler);
    frame.clampSampler = device.bindlessIndex(r.clampSampler);
    frame.shadowSampler = device.bindlessIndex(r.shadowSampler);
    frame.shadowNormalBias = r.settings.shadowNormalBias * texelWorld;
    frame.shadowTexelSize = 1.0f / static_cast<f32>(r.shadowMapSize);
    frame.debugView = static_cast<u32>(r.settings.debugView);
    frame.skyMap = device.bindlessIndex(r.environment);
    frame.viewportSize = vec2(static_cast<f32>(r.targetWidth), static_cast<f32>(r.targetHeight));
    frame.proceduralSky = r.equirect.isValid() ? 0u : 1u;
    frame.skyIntensity = r.settings.skyIntensity;

    const BufferHandle frameBuffer = r.frameBuffers[device.frameIndex()];
    std::memcpy(device.mappedData(frameBuffer), &frame, sizeof(frame));
    const u64 frameAddress = device.deviceAddress(frameBuffer);

    auto makePush = [&](const GpuModel& model, const GpuPrimitive& prim, const mat4& world) {
        gpu::DrawPush push{};
        push.frame = frameAddress;
        push.vertices = device.deviceAddress(model.vertices);
        push.materials = device.deviceAddress(model.materials);
        push.materialIndex = prim.material;
        push.model = world;
        return push;
    };

    const std::span<DrawItem> draws = r.buildDrawList(scene);
    r.stats.frameMemoryUsed = r.frameMemory.used();
    r.stats.frameMemoryCapacity = r.frameMemory.capacity();

    // --- Shadow pass ---------------------------------------------------------------------------
    {
        ScopedLabel label(cmd, "Shadow map");
        RenderingDesc rendering;
        rendering.depth = {r.shadowMap, LoadOp::Clear, 1.0f};
        cmd.beginRendering(rendering);
        PipelineHandle bound{};
        for (const DrawItem& draw : draws) {
            const GpuModel& model = *draw.model;
            const GpuPrimitive& prim = *draw.primitive;
            const bool masked = (model.materialFlags[prim.material] & gpu::kMaterialAlphaMask) != 0;
            const PipelineHandle pipeline = masked ? r.pipelines.shadowMasked : r.pipelines.shadowOpaque;
            if (pipeline != bound) {
                cmd.bindPipeline(pipeline);
                bound = pipeline;
            }
            cmd.bindIndexBuffer(model.indices);
            cmd.pushConstants(makePush(model, prim, draw.world));
            cmd.drawIndexed(prim.indexCount, 1, prim.firstIndex, static_cast<i32>(prim.firstVertex));
            ++r.stats.shadowDrawCalls;
        }
        cmd.endRendering();
        cmd.barrier(r.shadowMap, ResourceState::ShaderRead);
    }

    // --- Main pass: opaque meshes, then the sky behind them ------------------------------------
    {
        ScopedLabel label(cmd, "Main pass");
        RenderingDesc rendering;
        rendering.colorCount = 1;
        rendering.colors[0] = {r.hdrTarget, LoadOp::Clear, {0.0f, 0.0f, 0.0f, 1.0f}};
        rendering.depth = {r.depthTarget, LoadOp::Clear, 0.0f};
        cmd.beginRendering(rendering);

        PipelineHandle bound{};
        for (const DrawItem& draw : draws) {
            const GpuModel& model = *draw.model;
            const GpuPrimitive& prim = *draw.primitive;
            const bool doubleSided = (model.materialFlags[prim.material] & gpu::kMaterialDoubleSided) != 0;
            const PipelineHandle pipeline = doubleSided ? r.pipelines.meshDoubleSided : r.pipelines.meshOpaque;
            if (pipeline != bound) {
                cmd.bindPipeline(pipeline);
                bound = pipeline;
            }
            cmd.bindIndexBuffer(model.indices);
            cmd.pushConstants(makePush(model, prim, draw.world));
            cmd.drawIndexed(prim.indexCount, 1, prim.firstIndex, static_cast<i32>(prim.firstVertex));
            ++r.stats.drawCalls;
            r.stats.triangles += prim.indexCount / 3;
        }

        cmd.bindPipeline(r.pipelines.sky);
        cmd.pushConstants(gpu::SkyPush{frameAddress});
        cmd.draw(3);
        cmd.endRendering();
        cmd.barrier(r.hdrTarget, ResourceState::ShaderRead);
    }

    // --- Tonemap to the backbuffer -------------------------------------------------------------
    {
        ScopedLabel label(cmd, "Tonemap");
        RenderingDesc rendering;
        rendering.colorCount = 1;
        rendering.colors[0] = {device.backbuffer(), LoadOp::DontCare};
        cmd.beginRendering(rendering);
        cmd.bindPipeline(r.pipelines.tonemap);
        gpu::TonemapPush push{};
        push.hdrTexture = device.bindlessIndex(r.hdrTarget);
        push.sampler = device.bindlessIndex(r.pointClampSampler);
        push.tonemapper = static_cast<u32>(r.settings.tonemapper);
        push.outputSrgb = device.isBackbufferSrgb() ? 0u : 1u;
        push.exposure = std::exp2(r.settings.exposureEv);
        cmd.pushConstants(push);
        cmd.draw(3);
        cmd.endRendering();
    }

}

void Renderer::recordCapture(CommandList& cmd)
{
    Impl& r = *m_impl;
    Device& device = r.device;
    if (!r.capturePath.empty() && !r.capturePending) {
        const u32 width = device.backbufferWidth();
        const u32 height = device.backbufferHeight();
        const u64 size = static_cast<u64>(width) * height * 4;
        if (!r.captureBuffer.isValid() || device.bufferSize(r.captureBuffer) < size) {
            device.destroyBuffer(r.captureBuffer);
            r.captureBuffer = device.createBuffer(
                {.size = size, .usage = BufferUsage::TransferDst, .memory = MemoryType::Readback, .debugName = "Capture"});
        }
        cmd.barrier(device.backbuffer(), ResourceState::TransferSrc);
        cmd.copyTextureToBuffer(device.backbuffer(), r.captureBuffer);
        r.captureWidth = width;
        r.captureHeight = height;
        r.capturePending = true;
    }
}

bool Renderer::reloadChangedShaders()
{
    Impl& r = *m_impl;
    const auto now = std::chrono::steady_clock::now();
    if (now - r.lastShaderPoll < std::chrono::milliseconds(500)) {
        return false;
    }
    r.lastShaderPoll = now;
    if (r.shaders.poll().empty()) {
        return false;
    }
    if (!r.createPipelines()) {
        return false;
    }
    r.environmentDirty = true; // the IBL shaders may have changed
    r.brdfLutDirty = true;
    return true;
}

const std::string& Renderer::shaderError() const
{
    return m_impl->shaders.lastError();
}

void Renderer::requestCapture(const std::filesystem::path& path)
{
    m_impl->capturePath = path;
}

void Renderer::finishCapture()
{
    m_impl->writeCapture();
}

RenderSettings& Renderer::settings()
{
    return m_impl->settings;
}

const RenderStats& Renderer::stats() const
{
    return m_impl->stats;
}

} // namespace ember::render
