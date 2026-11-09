#pragma once

#include "ember/core/Serialize.h"
#include "ember/scene/Components.h"

#include <entt/entity/registry.hpp>

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>

namespace ember {
class JobSystem;
}

namespace ember::scene {

using Entity = entt::entity;
inline constexpr Entity kNullEntity = entt::null;

// A component type the editor and the scene file know about: reflection data plus type-erased access.
struct ComponentType {
    const char* name;
    const TypeInfo* type;
    bool (*has)(const entt::registry&, Entity);
    void* (*get)(entt::registry&, Entity); // nullptr if missing
    void (*add)(entt::registry&, Entity);
    void (*remove)(entt::registry&, Entity);
};

// Every entity has an IdComponent, a NameComponent, a Transform, a WorldTransform and a Hierarchy.
class Scene {
public:
    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Entity create(const std::string& name, Entity parent = kNullEntity, Guid id = {});
    void destroy(Entity entity); // and all its children
    void clear();
    bool valid(Entity entity) const { return m_registry.valid(entity); }

    // The child keeps its local transform, so it now moves with the new parent.
    // parent = kNullEntity makes it a root. Refuses to create cycles.
    bool setParent(Entity child, Entity parent);
    Entity parent(Entity entity) const;
    template <typename Fn>
    void forEachChild(Entity entity, Fn&& fn) const
    {
        Entity child = m_registry.get<Hierarchy>(entity).firstChild;
        while (child != kNullEntity) {
            const Entity next = m_registry.get<Hierarchy>(child).nextSibling;
            fn(child);
            child = next;
        }
    }
    template <typename Fn>
    void forEachRoot(Fn&& fn) const
    {
        for (const Entity e : m_registry.view<Hierarchy>()) {
            if (m_registry.get<Hierarchy>(e).parent == kNullEntity) {
                fn(e);
            }
        }
    }

    // world = parent world * local, for every entity. Root subtrees run in parallel when given a job system.
    void updateTransforms(JobSystem* jobs = nullptr);

    Entity findById(const Guid& id) const;
    const std::string& name(Entity entity) const { return m_registry.get<NameComponent>(entity).name; }

    entt::registry& registry() { return m_registry; }
    const entt::registry& registry() const { return m_registry; }

    // Scene files are JSON: a list of entities with their Guid, parent Guid and reflected components.
    Json save() const;
    bool load(const Json& json); // replaces the current contents
    bool saveFile(const std::filesystem::path& path) const;
    bool loadFile(const std::filesystem::path& path);

    // Components that can be saved and edited (all reflected built-ins; games can register more).
    static std::span<const ComponentType> componentTypes();
    template <Reflected T>
    static void registerComponent(const char* name);

private:
    void detach(Entity child);
    void updateSubtree(Entity entity, const mat4& parentWorld);

    entt::registry m_registry;
    std::unordered_map<Guid, Entity> m_byId;
};

namespace detail {
void addComponentType(const ComponentType& type);
}

template <Reflected T>
void Scene::registerComponent(const char* name)
{
    detail::addComponentType({
        name,
        &typeOf<T>(),
        [](const entt::registry& r, Entity e) { return r.all_of<T>(e); },
        [](entt::registry& r, Entity e) -> void* { return r.try_get<T>(e); },
        [](entt::registry& r, Entity e) { r.emplace_or_replace<T>(e); },
        [](entt::registry& r, Entity e) { r.remove<T>(e); },
    });
}

} // namespace ember::scene
