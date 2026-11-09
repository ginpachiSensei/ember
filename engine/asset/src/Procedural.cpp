#include "ember/asset/Model.h"

#include <limits>

namespace ember::asset {

namespace {

// Appends vertices/indices as one new mesh with one primitive.
u32 addMesh(Model& model, u32 material, std::vector<Vertex>&& vertices, std::vector<u32>&& indices,
            const char* name)
{
    Primitive prim;
    prim.firstVertex = static_cast<u32>(model.vertices.size());
    prim.firstIndex = static_cast<u32>(model.indices.size());
    prim.indexCount = static_cast<u32>(indices.size());
    prim.material = material;
    prim.boundsMin = vec3(std::numeric_limits<f32>::max());
    prim.boundsMax = vec3(std::numeric_limits<f32>::lowest());
    for (const Vertex& v : vertices) {
        prim.boundsMin = glm::min(prim.boundsMin, v.position);
        prim.boundsMax = glm::max(prim.boundsMax, v.position);
    }
    model.vertices.insert(model.vertices.end(), vertices.begin(), vertices.end());
    model.indices.insert(model.indices.end(), indices.begin(), indices.end());

    Mesh mesh;
    mesh.firstPrimitive = static_cast<u32>(model.primitives.size());
    mesh.primitiveCount = 1;
    mesh.name = name;
    model.primitives.push_back(prim);
    model.meshes.push_back(mesh);
    return static_cast<u32>(model.meshes.size() - 1);
}

} // namespace

u32 addMaterial(Model& model, const Material& material)
{
    model.materials.push_back(material);
    return static_cast<u32>(model.materials.size() - 1);
}

u32 addSphereMesh(Model& model, u32 material, f32 radius, u32 segments, u32 rings)
{
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
    constexpr f32 pi = 3.14159265358979f;
    for (u32 r = 0; r <= rings; ++r) {
        const f32 v = static_cast<f32>(r) / static_cast<f32>(rings);
        const f32 theta = v * pi; // 0 at the top
        for (u32 s = 0; s <= segments; ++s) {
            const f32 u = static_cast<f32>(s) / static_cast<f32>(segments);
            const f32 phi = u * 2.0f * pi;
            const vec3 n(std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi));
            vertices.push_back({n * radius, n, vec2(u, v)});
        }
    }
    const u32 stride = segments + 1;
    for (u32 r = 0; r < rings; ++r) {
        for (u32 s = 0; s < segments; ++s) {
            const u32 a = r * stride + s;
            const u32 b = a + stride;
            // Counter-clockwise when seen from outside.
            indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    return addMesh(model, material, std::move(vertices), std::move(indices), "Sphere");
}

u32 addPlaneMesh(Model& model, u32 material, f32 size, f32 uvScale)
{
    const f32 h = size * 0.5f;
    const vec3 up(0, 1, 0);
    std::vector<Vertex> vertices = {
        {vec3(-h, 0, -h), up, vec2(0, 0)},
        {vec3(-h, 0, h), up, vec2(0, uvScale)},
        {vec3(h, 0, h), up, vec2(uvScale, uvScale)},
        {vec3(h, 0, -h), up, vec2(uvScale, 0)},
    };
    std::vector<u32> indices = {0, 1, 2, 0, 2, 3};
    return addMesh(model, material, std::move(vertices), std::move(indices), "Plane");
}

u32 addCubeMesh(Model& model, u32 material, f32 e)
{
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
    // Each face: normal, and two axes spanning it (chosen so the winding is counter-clockwise from outside).
    const vec3 faces[6][3] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    for (const auto& face : faces) {
        const vec3 n = face[0];
        const vec3 u = face[1];
        const vec3 v = face[2];
        const u32 base = static_cast<u32>(vertices.size());
        vertices.push_back({(n - u - v) * e, n, vec2(0, 1)});
        vertices.push_back({(n + u - v) * e, n, vec2(1, 1)});
        vertices.push_back({(n + u + v) * e, n, vec2(1, 0)});
        vertices.push_back({(n - u + v) * e, n, vec2(0, 0)});
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return addMesh(model, material, std::move(vertices), std::move(indices), "Cube");
}

} // namespace ember::asset
