#include "ember/asset/Model.h"

#include "ember/core/Log.h"
#include "ember/core/Profile.h"

#include <cgltf.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <cstdlib>
#include <cstring>
#include <future>
#include <limits>

namespace ember::asset {

namespace {

// Decodes one glTF image (embedded in a buffer view, a data URI or a separate file) to RGBA8.
Image decodeImage(const cgltf_image& image, const std::filesystem::path& baseDir, const cgltf_options& options)
{
    EMBER_PROFILE_SCOPE("Decode image");
    Image out;
    out.name = image.name ? image.name : (image.uri ? image.uri : "");

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = nullptr;

    if (image.buffer_view) {
        const cgltf_buffer_view& view = *image.buffer_view;
        const auto* data = static_cast<const stbi_uc*>(view.buffer->data) + view.offset;
        pixels = stbi_load_from_memory(data, static_cast<int>(view.size), &width, &height, &channels, 4);
    } else if (image.uri && std::string_view(image.uri).starts_with("data:")) {
        const char* comma = std::strchr(image.uri, ',');
        if (comma) {
            std::string_view base64(comma + 1);
            usize padding = 0;
            while (!base64.empty() && base64.back() == '=') {
                base64.remove_suffix(1);
                ++padding;
            }
            const cgltf_size size = (base64.size() + padding) * 3 / 4 - padding;
            void* decoded = nullptr;
            if (cgltf_load_buffer_base64(&options, size, comma + 1, &decoded) == cgltf_result_success) {
                pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(decoded), static_cast<int>(size), &width,
                                               &height, &channels, 4);
                std::free(decoded);
            }
        }
    } else if (image.uri) {
        std::string uri = image.uri;
        cgltf_decode_uri(uri.data()); // e.g. "%20" -> " "
        uri.resize(std::strlen(uri.c_str()));
        const std::string file = (baseDir / std::filesystem::path(std::u8string(uri.begin(), uri.end()))).string();
        pixels = stbi_load(file.c_str(), &width, &height, &channels, 4);
    }

    if (!pixels) {
        EMBER_LOG_WARN("Could not decode image '{}': {}", out.name, stbi_failure_reason());
        return out;
    }
    out.width = static_cast<u32>(width);
    out.height = static_cast<u32>(height);
    out.pixels.assign(pixels, pixels + static_cast<usize>(width) * height * 4);
    stbi_image_free(pixels);
    return out;
}

i32 imageIndex(const cgltf_data& data, const cgltf_texture_view& view)
{
    if (!view.texture || !view.texture->image) {
        return kNoImage;
    }
    return static_cast<i32>(view.texture->image - data.images);
}

Material convertMaterial(const cgltf_data& data, const cgltf_material& m)
{
    Material out;
    out.name = m.name ? m.name : "";
    if (m.has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr = m.pbr_metallic_roughness;
        out.baseColorFactor = glm::make_vec4(pbr.base_color_factor);
        out.metallicFactor = pbr.metallic_factor;
        out.roughnessFactor = pbr.roughness_factor;
        out.baseColorImage = imageIndex(data, pbr.base_color_texture);
        out.metallicRoughnessImage = imageIndex(data, pbr.metallic_roughness_texture);
    }
    out.emissiveFactor = glm::make_vec3(m.emissive_factor);
    if (m.has_emissive_strength) {
        out.emissiveFactor *= m.emissive_strength.emissive_strength;
    }
    out.normalImage = imageIndex(data, m.normal_texture);
    out.normalScale = m.normal_texture.scale;
    out.occlusionImage = imageIndex(data, m.occlusion_texture);
    out.occlusionStrength = m.occlusion_texture.texture ? m.occlusion_texture.scale : 1.0f;
    out.emissiveImage = imageIndex(data, m.emissive_texture);
    out.alphaMode = m.alpha_mode == cgltf_alpha_mode_mask    ? AlphaMode::Mask
                    : m.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend
                                                             : AlphaMode::Opaque;
    out.alphaCutoff = m.alpha_cutoff;
    out.doubleSided = m.double_sided;
    return out;
}

// Smooth vertex normals from triangle areas, for meshes that don't ship any.
void generateNormals(std::vector<Vertex>& vertices, u32 firstVertex, const u32* indices, usize indexCount)
{
    for (usize i = 0; i + 2 < indexCount; i += 3) {
        Vertex& a = vertices[firstVertex + indices[i]];
        Vertex& b = vertices[firstVertex + indices[i + 1]];
        Vertex& c = vertices[firstVertex + indices[i + 2]];
        const vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (usize v = firstVertex; v < vertices.size(); ++v) {
        const f32 len = glm::length(vertices[v].normal);
        vertices[v].normal = len > 0.0f ? vertices[v].normal / len : vec3(0, 1, 0);
    }
}

bool convertPrimitive(const cgltf_primitive& prim, const cgltf_data& data, Model& model)
{
    if (prim.type != cgltf_primitive_type_triangles) {
        return false; // points/lines aren't drawn by the mesh pipeline
    }
    const cgltf_accessor* positions = nullptr;
    const cgltf_accessor* normals = nullptr;
    const cgltf_accessor* uvs = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const cgltf_attribute& attr = prim.attributes[i];
        if (attr.type == cgltf_attribute_type_position) {
            positions = attr.data;
        } else if (attr.type == cgltf_attribute_type_normal) {
            normals = attr.data;
        } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
            uvs = attr.data;
        }
    }
    if (!positions) {
        return false;
    }

    Primitive out;
    out.firstVertex = static_cast<u32>(model.vertices.size());
    out.firstIndex = static_cast<u32>(model.indices.size());
    out.material = prim.material ? static_cast<u32>(prim.material - data.materials) : 0;

    const usize vertexCount = positions->count;
    model.vertices.resize(model.vertices.size() + vertexCount, Vertex{vec3(0.0f), vec3(0.0f), vec2(0.0f)});
    Vertex* verts = model.vertices.data() + out.firstVertex;

    out.boundsMin = vec3(std::numeric_limits<f32>::max());
    out.boundsMax = vec3(std::numeric_limits<f32>::lowest());
    for (usize v = 0; v < vertexCount; ++v) {
        cgltf_accessor_read_float(positions, v, &verts[v].position.x, 3);
        if (normals) {
            cgltf_accessor_read_float(normals, v, &verts[v].normal.x, 3);
        }
        if (uvs) {
            cgltf_accessor_read_float(uvs, v, &verts[v].uv.x, 2);
        }
        out.boundsMin = glm::min(out.boundsMin, verts[v].position);
        out.boundsMax = glm::max(out.boundsMax, verts[v].position);
    }

    if (prim.indices) {
        out.indexCount = static_cast<u32>(prim.indices->count);
        model.indices.resize(model.indices.size() + out.indexCount);
        cgltf_accessor_unpack_indices(prim.indices, model.indices.data() + out.firstIndex, sizeof(u32),
                                      out.indexCount);
    } else {
        out.indexCount = static_cast<u32>(vertexCount);
        for (u32 i = 0; i < out.indexCount; ++i) {
            model.indices.push_back(i);
        }
    }

    if (!normals) {
        generateNormals(model.vertices, out.firstVertex, model.indices.data() + out.firstIndex, out.indexCount);
    }
    model.primitives.push_back(out);
    return true;
}

void addNodeInstances(const cgltf_node& node, const cgltf_data& data, Model& model)
{
    if (node.mesh) {
        Instance instance;
        cgltf_node_transform_world(&node, glm::value_ptr(instance.transform));
        instance.mesh = static_cast<u32>(node.mesh - data.meshes);
        model.instances.push_back(instance);
    }
    for (cgltf_size i = 0; i < node.children_count; ++i) {
        addNodeInstances(*node.children[i], data, model);
    }
}

} // namespace

std::optional<Model> loadGltf(const std::filesystem::path& path)
{
    EMBER_PROFILE_FUNCTION();
    const std::string pathString = path.string();

    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, pathString.c_str(), &data);
    if (result == cgltf_result_success) {
        result = cgltf_load_buffers(&options, data, pathString.c_str());
    }
    if (result != cgltf_result_success) {
        EMBER_LOG_ERROR("Failed to load glTF '{}' (cgltf error {})", pathString, static_cast<int>(result));
        cgltf_free(data);
        return std::nullopt;
    }

    Model model;
    model.name = path.stem().string();

    // Images are the slow part, so decode them in parallel. (std::async until the job system lands.)
    const std::filesystem::path baseDir = path.parent_path();
    std::vector<std::future<Image>> imageJobs;
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        imageJobs.push_back(std::async(std::launch::async, [&, i] { return decodeImage(data->images[i], baseDir, options); }));
    }

    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        model.materials.push_back(convertMaterial(*data, data->materials[i]));
    }
    if (model.materials.empty()) {
        model.materials.push_back(Material{.name = "default"});
    }

    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        const cgltf_mesh& mesh = data->meshes[m];
        Mesh out;
        out.name = mesh.name ? mesh.name : "";
        out.firstPrimitive = static_cast<u32>(model.primitives.size());
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            convertPrimitive(mesh.primitives[p], *data, model);
        }
        out.primitiveCount = static_cast<u32>(model.primitives.size()) - out.firstPrimitive;
        model.meshes.push_back(out);
    }

    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
            addNodeInstances(*scene->nodes[i], *data, model);
        }
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            if (!data->nodes[i].parent) {
                addNodeInstances(data->nodes[i], *data, model);
            }
        }
    }

    for (auto& job : imageJobs) {
        model.images.push_back(job.get());
    }
    // Color textures are stored in sRGB; everything else is linear data.
    for (const Material& material : model.materials) {
        for (i32 index : {material.baseColorImage, material.emissiveImage}) {
            if (index != kNoImage) {
                model.images[static_cast<usize>(index)].srgb = true;
            }
        }
    }

    cgltf_free(data);
    EMBER_LOG_INFO("Loaded '{}': {} meshes, {} instances, {} materials, {} images, {} triangles", model.name,
                   model.meshes.size(), model.instances.size(), model.materials.size(), model.images.size(),
                   model.indices.size() / 3);
    return model;
}

std::optional<HdrImage> loadHdr(const std::filesystem::path& path)
{
    EMBER_PROFILE_FUNCTION();
    int width = 0;
    int height = 0;
    int channels = 0;
    f32* pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        EMBER_LOG_ERROR("Failed to load HDR '{}': {}", path.string(), stbi_failure_reason());
        return std::nullopt;
    }
    HdrImage image;
    image.width = static_cast<u32>(width);
    image.height = static_cast<u32>(height);
    image.pixels.assign(pixels, pixels + static_cast<usize>(width) * height * 4);
    stbi_image_free(pixels);
    return image;
}

bool writePng(const std::filesystem::path& path, u32 width, u32 height, const u8* rgba)
{
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    const int ok = stbi_write_png(path.string().c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba,
                                  static_cast<int>(width * 4));
    if (!ok) {
        EMBER_LOG_ERROR("Failed to write {}", path.string());
    }
    return ok != 0;
}

vec3 Model::boundsMin() const
{
    vec3 result(std::numeric_limits<f32>::max());
    for (const Instance& instance : instances) {
        const Mesh& mesh = meshes[instance.mesh];
        for (u32 p = mesh.firstPrimitive; p < mesh.firstPrimitive + mesh.primitiveCount; ++p) {
            for (int corner = 0; corner < 8; ++corner) {
                const Primitive& prim = primitives[p];
                const vec3 local((corner & 1) ? prim.boundsMax.x : prim.boundsMin.x,
                                 (corner & 2) ? prim.boundsMax.y : prim.boundsMin.y,
                                 (corner & 4) ? prim.boundsMax.z : prim.boundsMin.z);
                result = glm::min(result, vec3(instance.transform * vec4(local, 1.0f)));
            }
        }
    }
    return result;
}

vec3 Model::boundsMax() const
{
    vec3 result(std::numeric_limits<f32>::lowest());
    for (const Instance& instance : instances) {
        const Mesh& mesh = meshes[instance.mesh];
        for (u32 p = mesh.firstPrimitive; p < mesh.firstPrimitive + mesh.primitiveCount; ++p) {
            for (int corner = 0; corner < 8; ++corner) {
                const Primitive& prim = primitives[p];
                const vec3 local((corner & 1) ? prim.boundsMax.x : prim.boundsMin.x,
                                 (corner & 2) ? prim.boundsMax.y : prim.boundsMin.y,
                                 (corner & 4) ? prim.boundsMax.z : prim.boundsMin.z);
                result = glm::max(result, vec3(instance.transform * vec4(local, 1.0f)));
            }
        }
    }
    return result;
}

} // namespace ember::asset
