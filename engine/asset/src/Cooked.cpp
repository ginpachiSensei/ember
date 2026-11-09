#include "ember/asset/Cooked.h"

#include "ember/core/Log.h"
#include "ember/core/Profile.h"

#include <stb_image_resize2.h>

#include <cstring>
#include <fstream>
#include <type_traits>

namespace ember::asset {

namespace {

constexpr u32 kModelMagic = 0x4c444d45; // "EMDL"
constexpr u32 kEnvironmentMagic = 0x564e4545; // "EENV"

// Binary writer/reader for trivially copyable values, arrays and strings.
class Writer {
public:
    explicit Writer(const std::filesystem::path& path)
        : m_file(path, std::ios::binary)
    {
    }
    bool ok() const { return m_file.good(); }

    template <typename T>
    void value(const T& v)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        m_file.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    template <typename T, typename A>
    void array(const std::vector<T, A>& items)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        value(static_cast<u64>(items.size()));
        m_file.write(reinterpret_cast<const char*>(items.data()), static_cast<std::streamsize>(items.size() * sizeof(T)));
    }
    void string(const std::string& s)
    {
        value(static_cast<u32>(s.size()));
        m_file.write(s.data(), static_cast<std::streamsize>(s.size()));
    }

private:
    std::ofstream m_file;
};

class Reader {
public:
    explicit Reader(const std::filesystem::path& path)
        : m_file(path, std::ios::binary)
    {
    }
    bool ok() const { return m_file.good(); }

    template <typename T>
    T value()
    {
        T v{};
        m_file.read(reinterpret_cast<char*>(&v), sizeof(T));
        return v;
    }
    template <typename T, typename A>
    void array(std::vector<T, A>& items)
    {
        const u64 count = value<u64>();
        if (!ok() || count > (1ull << 32)) {
            m_file.setstate(std::ios::failbit);
            return;
        }
        items.resize(count);
        m_file.read(reinterpret_cast<char*>(items.data()), static_cast<std::streamsize>(count * sizeof(T)));
    }
    std::string string()
    {
        const u32 size = value<u32>();
        if (!ok() || size > (1u << 20)) {
            m_file.setstate(std::ios::failbit);
            return {};
        }
        std::string s(size, '\0');
        m_file.read(s.data(), size);
        return s;
    }

private:
    std::ifstream m_file;
};

// Trivially copyable mirrors of the structs that hold strings.
struct MaterialData {
    vec4 baseColorFactor;
    vec3 emissiveFactor;
    f32 metallicFactor, roughnessFactor, normalScale, occlusionStrength, alphaCutoff;
    AlphaMode alphaMode;
    bool doubleSided;
    i32 baseColorImage, metallicRoughnessImage, normalImage, occlusionImage, emissiveImage;
};
struct MeshData {
    u32 firstPrimitive, primitiveCount;
};

} // namespace

void generateMipChains(Model& model)
{
    EMBER_PROFILE_FUNCTION();
    for (Image& image : model.images) {
        if (image.pixels.empty() || image.mipLevels > 1) {
            continue;
        }
        AssetVector<u8> chain = image.pixels;
        u32 width = image.width;
        u32 height = image.height;
        usize previous = 0; // offset of the mip we downsample from
        u32 levels = 1;
        while (width > 1 || height > 1) {
            const u32 nextWidth = std::max(1u, width / 2);
            const u32 nextHeight = std::max(1u, height / 2);
            const usize offset = chain.size();
            chain.resize(offset + static_cast<usize>(nextWidth) * nextHeight * 4);
            // Color data must be averaged in linear space, or dark details grow with every mip.
            if (image.srgb) {
                stbir_resize_uint8_srgb(chain.data() + previous, static_cast<int>(width), static_cast<int>(height), 0,
                                        chain.data() + offset, static_cast<int>(nextWidth), static_cast<int>(nextHeight), 0,
                                        STBIR_RGBA);
            } else {
                stbir_resize_uint8_linear(chain.data() + previous, static_cast<int>(width), static_cast<int>(height), 0,
                                          chain.data() + offset, static_cast<int>(nextWidth), static_cast<int>(nextHeight),
                                          0, STBIR_RGBA);
            }
            previous = offset;
            width = nextWidth;
            height = nextHeight;
            ++levels;
        }
        image.pixels = std::move(chain);
        image.mipLevels = levels;
    }
}

bool writeCookedModel(const std::filesystem::path& path, const Model& model)
{
    Writer w(path);
    w.value(kModelMagic);
    w.value(kCookedModelVersion);
    w.string(model.name);
    w.array(model.vertices);
    w.array(model.indices);
    w.array(model.primitives);
    w.array(model.instances);

    w.value(static_cast<u32>(model.meshes.size()));
    for (const Mesh& mesh : model.meshes) {
        w.value(MeshData{mesh.firstPrimitive, mesh.primitiveCount});
        w.string(mesh.name);
    }
    w.value(static_cast<u32>(model.materials.size()));
    for (const Material& m : model.materials) {
        w.value(MaterialData{m.baseColorFactor, m.emissiveFactor, m.metallicFactor, m.roughnessFactor, m.normalScale,
                             m.occlusionStrength, m.alphaCutoff, m.alphaMode, m.doubleSided, m.baseColorImage,
                             m.metallicRoughnessImage, m.normalImage, m.occlusionImage, m.emissiveImage});
        w.string(m.name);
    }
    w.value(static_cast<u32>(model.images.size()));
    for (const Image& image : model.images) {
        w.value(image.width);
        w.value(image.height);
        w.value(image.mipLevels);
        w.value(image.srgb);
        w.string(image.name);
        w.array(image.pixels);
    }
    if (!w.ok()) {
        EMBER_LOG_ERROR("Failed writing {}", path.string());
    }
    return w.ok();
}

std::optional<Model> readCookedModel(const std::filesystem::path& path)
{
    EMBER_PROFILE_FUNCTION();
    Reader r(path);
    if (r.value<u32>() != kModelMagic || r.value<u32>() != kCookedModelVersion) {
        EMBER_LOG_ERROR("{} is not a cooked model (or was cooked by another version)", path.string());
        return std::nullopt;
    }
    Model model;
    model.name = r.string();
    r.array(model.vertices);
    r.array(model.indices);
    r.array(model.primitives);
    r.array(model.instances);

    const u32 meshCount = r.value<u32>();
    for (u32 i = 0; i < meshCount && r.ok(); ++i) {
        const auto data = r.value<MeshData>();
        model.meshes.push_back({data.firstPrimitive, data.primitiveCount, r.string()});
    }
    const u32 materialCount = r.value<u32>();
    for (u32 i = 0; i < materialCount && r.ok(); ++i) {
        const auto d = r.value<MaterialData>();
        Material m;
        m.baseColorFactor = d.baseColorFactor;
        m.emissiveFactor = d.emissiveFactor;
        m.metallicFactor = d.metallicFactor;
        m.roughnessFactor = d.roughnessFactor;
        m.normalScale = d.normalScale;
        m.occlusionStrength = d.occlusionStrength;
        m.alphaCutoff = d.alphaCutoff;
        m.alphaMode = d.alphaMode;
        m.doubleSided = d.doubleSided;
        m.baseColorImage = d.baseColorImage;
        m.metallicRoughnessImage = d.metallicRoughnessImage;
        m.normalImage = d.normalImage;
        m.occlusionImage = d.occlusionImage;
        m.emissiveImage = d.emissiveImage;
        m.name = r.string();
        model.materials.push_back(std::move(m));
    }
    const u32 imageCount = r.value<u32>();
    for (u32 i = 0; i < imageCount && r.ok(); ++i) {
        Image image;
        image.width = r.value<u32>();
        image.height = r.value<u32>();
        image.mipLevels = r.value<u32>();
        image.srgb = r.value<bool>();
        image.name = r.string();
        r.array(image.pixels);
        model.images.push_back(std::move(image));
    }
    if (!r.ok()) {
        EMBER_LOG_ERROR("{} is truncated or corrupt", path.string());
        return std::nullopt;
    }
    return model;
}

bool writeCookedEnvironment(const std::filesystem::path& path, const HdrImage& image)
{
    Writer w(path);
    w.value(kEnvironmentMagic);
    w.value(kCookedEnvironmentVersion);
    w.value(image.width);
    w.value(image.height);
    w.array(image.pixels);
    return w.ok();
}

std::optional<HdrImage> readCookedEnvironment(const std::filesystem::path& path)
{
    Reader r(path);
    if (r.value<u32>() != kEnvironmentMagic || r.value<u32>() != kCookedEnvironmentVersion) {
        EMBER_LOG_ERROR("{} is not a cooked environment", path.string());
        return std::nullopt;
    }
    HdrImage image;
    image.width = r.value<u32>();
    image.height = r.value<u32>();
    r.array(image.pixels);
    if (!r.ok()) {
        return std::nullopt;
    }
    return image;
}

} // namespace ember::asset
