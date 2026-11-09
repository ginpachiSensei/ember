#pragma once

// CPU-side model data, ready to upload to the GPU. Loaded from glTF for now; in phase 2 the
// cooker will produce this layout offline and the runtime will just read it.

#include "ember/core/Math.h"
#include "ember/core/Types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ember::asset {

struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 uv;
};
static_assert(sizeof(Vertex) == 32);

struct Image {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> pixels; // RGBA8
    bool srgb = false;      // color data (base color, emissive) vs. linear data (normals, roughness)
    std::string name;
};

enum class AlphaMode : u8 { Opaque, Mask, Blend };

inline constexpr i32 kNoImage = -1;

struct Material {
    vec4 baseColorFactor{1.0f};
    vec3 emissiveFactor{0.0f};
    f32 metallicFactor = 1.0f;
    f32 roughnessFactor = 1.0f;
    f32 normalScale = 1.0f;
    f32 occlusionStrength = 1.0f;
    f32 alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    i32 baseColorImage = kNoImage; // indices into Model::images
    i32 metallicRoughnessImage = kNoImage;
    i32 normalImage = kNoImage;
    i32 occlusionImage = kNoImage;
    i32 emissiveImage = kNoImage;
    std::string name;
};

// A range of the model's index buffer drawn with one material.
struct Primitive {
    u32 firstIndex = 0;
    u32 indexCount = 0;
    u32 firstVertex = 0; // added to every index
    u32 material = 0;
    vec3 boundsMin{0.0f}; // object space
    vec3 boundsMax{0.0f};
};

struct Mesh {
    u32 firstPrimitive = 0;
    u32 primitiveCount = 0;
    std::string name;
};

// A placement of a mesh in the model's scene (from the glTF node hierarchy, flattened).
struct Instance {
    mat4 transform{1.0f};
    u32 mesh = 0;
};

struct Model {
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
    std::vector<Primitive> primitives;
    std::vector<Mesh> meshes;
    std::vector<Instance> instances;
    std::vector<Material> materials;
    std::vector<Image> images;
    std::string name;

    // World-space bounds over all instances.
    vec3 boundsMin() const;
    vec3 boundsMax() const;
};

// Loads a .gltf or .glb file, decoding all images. Returns nullopt (and logs why) on failure.
std::optional<Model> loadGltf(const std::filesystem::path& path);

struct HdrImage {
    u32 width = 0;
    u32 height = 0;
    std::vector<f32> pixels; // RGBA32F, linear
};

// Loads a Radiance .hdr file (e.g. an equirectangular environment from polyhaven.com).
std::optional<HdrImage> loadHdr(const std::filesystem::path& path);

// Writes tightly packed RGBA8 pixels as a PNG. Returns false (and logs) on failure.
bool writePng(const std::filesystem::path& path, u32 width, u32 height, const u8* rgba);

// --- Procedural shapes (test scenes) ---------------------------------------------------------
u32 addMaterial(Model& model, const Material& material);
u32 addSphereMesh(Model& model, u32 material, f32 radius = 1.0f, u32 segments = 48, u32 rings = 32);
u32 addPlaneMesh(Model& model, u32 material, f32 size = 1.0f, f32 uvScale = 1.0f); // XZ plane, facing +Y
u32 addCubeMesh(Model& model, u32 material, f32 halfExtent = 0.5f);

} // namespace ember::asset
