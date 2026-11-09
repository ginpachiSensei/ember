#include "ember/asset/AssetDatabase.h"

#include "ember/core/Serialize.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

EMBER_REFLECT_BEGIN(ember::asset::AssetMeta)
    EMBER_FIELD(guid)
    EMBER_FIELD(type)
EMBER_REFLECT_END(ember::asset::AssetMeta)

EMBER_REFLECT_BEGIN(ember::asset::ManifestEntry)
    EMBER_FIELD(guid)
    EMBER_FIELD(type)
    EMBER_FIELD(source)
    EMBER_FIELD(cooked)
    EMBER_FIELD(sourceHash)
    EMBER_FIELD(cookerVersion)
EMBER_REFLECT_END(ember::asset::ManifestEntry)

namespace ember::asset {

namespace fs = std::filesystem;

AssetType assetTypeForPath(const fs::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".glb" || ext == ".gltf") {
        return AssetType::Model;
    }
    if (ext == ".hdr") {
        return AssetType::Environment;
    }
    return AssetType::Unknown;
}

const char* cookedExtension(AssetType type)
{
    switch (type) {
    case AssetType::Model: return ".emodel";
    case AssetType::Environment: return ".eenv";
    case AssetType::Unknown: break;
    }
    return ".bin";
}

fs::path metaPathFor(const fs::path& source)
{
    return fs::path(source.string() + ".meta");
}

std::optional<AssetMeta> readMeta(const fs::path& source)
{
    const std::optional<Json> json = readJsonFile(metaPathFor(source));
    if (!json) {
        return std::nullopt;
    }
    AssetMeta meta = fromJson<AssetMeta>(*json);
    if (!meta.guid.isValid()) {
        return std::nullopt;
    }
    return meta;
}

bool writeMeta(const fs::path& source, const AssetMeta& meta)
{
    return writeJsonFile(metaPathFor(source), toJson(meta));
}

const ManifestEntry* Manifest::find(const Guid& guid) const
{
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const ManifestEntry& e) { return e.guid == guid; });
    return it == entries.end() ? nullptr : &*it;
}

const ManifestEntry* Manifest::findBySource(std::string_view source) const
{
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const ManifestEntry& e) { return e.source == source; });
    return it == entries.end() ? nullptr : &*it;
}

Manifest Manifest::load(const fs::path& cookedDir)
{
    Manifest manifest;
    const std::optional<Json> json = readJsonFile(cookedDir / "manifest.json");
    if (json && json->contains("assets") && (*json)["assets"].is_array()) {
        for (const Json& entry : (*json)["assets"]) {
            manifest.entries.push_back(fromJson<ManifestEntry>(entry));
        }
    }
    return manifest;
}

bool Manifest::save(const fs::path& cookedDir) const
{
    Json assets = Json::array();
    for (const ManifestEntry& entry : entries) {
        assets.push_back(toJson(entry));
    }
    return writeJsonFile(cookedDir / "manifest.json", Json{{"version", 1}, {"assets", std::move(assets)}});
}

std::string hashFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    u64 h = 0xcbf29ce484222325ull;
    char buffer[1 << 16];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        h = hash64(buffer, static_cast<usize>(file.gcount()), h);
    }
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(h));
    return text;
}

} // namespace ember::asset
