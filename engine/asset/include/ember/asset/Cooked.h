#pragma once

// Cooked (runtime) asset formats: what the cooker writes and the engine loads.
// They're plain binary dumps of the in-memory structs, so loading is a few big reads with no parsing,
// no image decoding and no mip generation.

#include "ember/asset/Model.h"

#include <filesystem>
#include <optional>

namespace ember::asset {

inline constexpr u32 kCookedModelVersion = 1;
inline constexpr u32 kCookedEnvironmentVersion = 1;

// Fills in every image's full mip chain (sRGB-correct filtering for color images).
void generateMipChains(Model& model);

bool writeCookedModel(const std::filesystem::path& path, const Model& model);
std::optional<Model> readCookedModel(const std::filesystem::path& path);

bool writeCookedEnvironment(const std::filesystem::path& path, const HdrImage& image);
std::optional<HdrImage> readCookedEnvironment(const std::filesystem::path& path);

} // namespace ember::asset
