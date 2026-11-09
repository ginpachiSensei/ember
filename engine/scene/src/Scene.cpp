#include "ember/scene/Scene.h"

#include "ember/core/Jobs.h"
#include "ember/core/Log.h"
#include "ember/core/Profile.h"

#include <mutex>
#include <vector>

namespace ember::scene {

namespace {

std::vector<ComponentType>& componentRegistry()
{
    static std::vector<ComponentType> types;
    return types;
}

std::once_flag g_builtinsOnce;

void registerBuiltins()
{
    std::call_once(g_builtinsOnce, [] {
        Scene::registerComponent<Transform>("Transform");
        Scene::registerComponent<MeshRenderer>("MeshRenderer");
        Scene::registerComponent<DirectionalLight>("DirectionalLight");
        Scene::registerComponent<Spin>("Spin");
    });
}

} // namespace

namespace detail {
void addComponentType(const ComponentType& type)
{
    componentRegistry().push_back(type);
}
} // namespace detail

std::span<const ComponentType> Scene::componentTypes()
{
    registerBuiltins();
    return componentRegistry();
}

Entity Scene::create(const std::string& name, Entity parent, Guid id)
{
    const Entity entity = m_registry.create();
    if (!id.isValid()) {
        id = Guid::generate();
    }
    m_registry.emplace<IdComponent>(entity, id);
    m_registry.emplace<NameComponent>(entity, name);
    m_registry.emplace<Transform>(entity);
    m_registry.emplace<WorldTransform>(entity);
    m_registry.emplace<Hierarchy>(entity);
    m_byId[id] = entity;
    if (parent != kNullEntity) {
        setParent(entity, parent);
    }
    return entity;
}

void Scene::destroy(Entity entity)
{
    if (!valid(entity)) {
        return;
    }
    std::vector<Entity> children;
    forEachChild(entity, [&](Entity child) { children.push_back(child); });
    for (Entity child : children) {
        destroy(child);
    }
    detach(entity);
    m_byId.erase(m_registry.get<IdComponent>(entity).id);
    m_registry.destroy(entity);
}

void Scene::clear()
{
    m_registry.clear();
    m_byId.clear();
}

void Scene::detach(Entity child)
{
    Hierarchy& h = m_registry.get<Hierarchy>(child);
    if (h.parent == kNullEntity) {
        return;
    }
    if (h.prevSibling != kNullEntity) {
        m_registry.get<Hierarchy>(h.prevSibling).nextSibling = h.nextSibling;
    } else {
        m_registry.get<Hierarchy>(h.parent).firstChild = h.nextSibling;
    }
    if (h.nextSibling != kNullEntity) {
        m_registry.get<Hierarchy>(h.nextSibling).prevSibling = h.prevSibling;
    }
    h.parent = h.prevSibling = h.nextSibling = kNullEntity;
}

bool Scene::setParent(Entity child, Entity parent)
{
    for (Entity p = parent; p != kNullEntity; p = m_registry.get<Hierarchy>(p).parent) {
        if (p == child) {
            EMBER_LOG_WARN("setParent: '{}' can't become a child of its own descendant", name(child));
            return false;
        }
    }
    detach(child);
    if (parent == kNullEntity) {
        return true;
    }
    // Append at the end so children keep their creation order (matters for saved files and the editor).
    Hierarchy& h = m_registry.get<Hierarchy>(child);
    h.parent = parent;
    Hierarchy& ph = m_registry.get<Hierarchy>(parent);
    if (ph.firstChild == kNullEntity) {
        ph.firstChild = child;
        return true;
    }
    Entity last = ph.firstChild;
    while (m_registry.get<Hierarchy>(last).nextSibling != kNullEntity) {
        last = m_registry.get<Hierarchy>(last).nextSibling;
    }
    m_registry.get<Hierarchy>(last).nextSibling = child;
    h.prevSibling = last;
    return true;
}

Entity Scene::parent(Entity entity) const
{
    return m_registry.get<Hierarchy>(entity).parent;
}

Entity Scene::findById(const Guid& id) const
{
    const auto it = m_byId.find(id);
    return it == m_byId.end() ? kNullEntity : it->second;
}

void Scene::updateSubtree(Entity entity, const mat4& parentWorld)
{
    const mat4 world = parentWorld * m_registry.get<Transform>(entity).matrix();
    m_registry.get<WorldTransform>(entity).matrix = world;
    forEachChild(entity, [&](Entity child) { updateSubtree(child, world); });
}

void Scene::updateTransforms(JobSystem* jobs)
{
    EMBER_PROFILE_FUNCTION();
    std::vector<Entity> roots;
    forEachRoot([&](Entity e) { roots.push_back(e); });

    // Each root's subtree touches only its own entities, so subtrees can run in parallel.
    // (Writing existing components from several threads is safe: nothing is added or removed.)
    constexpr usize kParallelThreshold = 64;
    if (jobs && roots.size() >= kParallelThreshold) {
        jobs->parallelFor(static_cast<u32>(roots.size()), 16, [&](u32 begin, u32 end) {
            for (u32 i = begin; i < end; ++i) {
                updateSubtree(roots[i], mat4(1.0f));
            }
        });
    } else {
        for (Entity root : roots) {
            updateSubtree(root, mat4(1.0f));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Save / load
// ---------------------------------------------------------------------------------------------

Json Scene::save() const
{
    auto& registry = const_cast<entt::registry&>(m_registry); // the type-erased getters take non-const

    // Parents before children, so loading can link them in one pass.
    std::vector<Entity> order;
    forEachRoot([&](Entity root) {
        std::vector<Entity> stack{root};
        while (!stack.empty()) {
            const Entity e = stack.back();
            stack.pop_back();
            order.push_back(e);
            std::vector<Entity> children;
            forEachChild(e, [&](Entity c) { children.push_back(c); });
            stack.insert(stack.end(), children.rbegin(), children.rend());
        }
    });

    Json entities = Json::array();
    for (const Entity e : order) {
        Json entity;
        entity["id"] = m_registry.get<IdComponent>(e).id.toString();
        entity["name"] = name(e);
        const Entity p = parent(e);
        if (p != kNullEntity) {
            entity["parent"] = m_registry.get<IdComponent>(p).id.toString();
        }
        Json components = Json::object();
        for (const ComponentType& type : componentTypes()) {
            if (void* data = type.get(registry, e)) {
                components[type.name] = serialize(data, *type.type);
            }
        }
        entity["components"] = std::move(components);
        entities.push_back(std::move(entity));
    }
    return Json{{"version", 1}, {"entities", std::move(entities)}};
}

bool Scene::load(const Json& json)
{
    if (!json.is_object() || !json.contains("entities") || !json["entities"].is_array()) {
        EMBER_LOG_ERROR("Not a scene file");
        return false;
    }
    clear();
    for (const Json& entity : json["entities"]) {
        const Guid id = Guid::parse(entity.value("id", "")).value_or(Guid{});
        Entity parentEntity = kNullEntity;
        if (entity.contains("parent") && entity["parent"].is_string()) {
            parentEntity = findById(Guid::parse(entity["parent"].get<std::string>()).value_or(Guid{}));
        }
        const Entity e = create(entity.value("name", "Entity"), parentEntity, id);
        if (!entity.contains("components")) {
            continue;
        }
        const Json& components = entity["components"];
        for (const ComponentType& type : componentTypes()) {
            const auto it = components.find(type.name);
            if (it != components.end()) {
                type.add(m_registry, e);
                deserialize(type.get(m_registry, e), *type.type, *it);
            }
        }
    }
    updateTransforms();
    return true;
}

bool Scene::saveFile(const std::filesystem::path& path) const
{
    return writeJsonFile(path, save());
}

bool Scene::loadFile(const std::filesystem::path& path)
{
    const std::optional<Json> json = readJsonFile(path);
    return json && load(*json);
}

} // namespace ember::scene
