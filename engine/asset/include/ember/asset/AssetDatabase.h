#pragma once

// How source assets map to cooked ones.
//
//   content/models/Helmet.glb            the source, edited by artists
//   content/models/Helmet.glb.meta       {"guid": "...", "type": "Model"}: the asset's permanent identity
//   <cooked>/<guid>.emodel               what the engine loads
//   <cooked>/manifest.json               guid -> type, source path, cooked file, source content hash
//
// The cooker owns writing these; the runtime only reads them.

#include "ember/core/Guid.h"
#include "ember/core/Reflect.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ember::asset {

enum class AssetType : i32 { Unknown, Model, Environment };

struct AssetMeta {
    Guid guid;
    AssetType type = AssetType::Unknown;
};

struct ManifestEntry {
    Guid guid;
    AssetType type = AssetType::Unknown;
    std::string source; // relative to the content directory, '/' separators
    std::string cooked; // file name inside the cooked directory
    std::string sourceHash; // hex FNV-1a of the source bytes: unchanged hash = no re-cook needed
    u32 cookerVersion = 0;
};

// Which importer handles a source file (by extension), or Unknown.
AssetType assetTypeForPath(const std::filesystem::path& path);
const char* cookedExtension(AssetType type);

std::filesystem::path metaPathFor(const std::filesystem::path& source);
std::optional<AssetMeta> readMeta(const std::filesystem::path& source);
bool writeMeta(const std::filesystem::path& source, const AssetMeta& meta);

struct Manifest {
    std::vector<ManifestEntry> entries;

    const ManifestEntry* find(const Guid& guid) const;
    const ManifestEntry* findBySource(std::string_view source) const;

    static Manifest load(const std::filesystem::path& cookedDir); // empty if missing
    bool save(const std::filesystem::path& cookedDir) const;
};

std::string hashFile(const std::filesystem::path& path); // "" if unreadable

} // namespace ember::asset

EMBER_REFLECT_ENUM(ember::asset::AssetType, "Unknown", "Model", "Environment")
EMBER_REFLECT_DECLARE(ember::asset::AssetMeta)
EMBER_REFLECT_DECLARE(ember::asset::ManifestEntry)
