#pragma once

// Built-in components. Plain data only: behavior lives in systems that loop over components.

#include "ember/core/Guid.h"
#include "ember/core/Math.h"
#include "ember/core/Reflect.h"

#include <entt/entity/entity.hpp>

#include <string>

namespace ember::scene {

// Stable identity, saved in scene files. entt::entity values are just array slots and change on load.
struct IdComponent {
    Guid id;
};

struct NameComponent {
    std::string name;
};

// Local transform, relative to the parent (or the world, for root entities).
struct Transform {
    vec3 position{0.0f};
    quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    vec3 scale{1.0f};

    mat4 matrix() const
    {
        return glm::translate(mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(mat4(1.0f), scale);
    }
};

// Computed by Scene::updateTransforms(); don't write it yourself.
struct WorldTransform {
    mat4 matrix{1.0f};
};

// Parent/child links as an intrusive linked list (no per-entity child arrays to allocate).
struct Hierarchy {
    entt::entity parent = entt::null;
    entt::entity firstChild = entt::null;
    entt::entity nextSibling = entt::null;
    entt::entity prevSibling = entt::null;
};

// Draws a model asset (by Guid) at the entity's world transform.
struct MeshRenderer {
    Guid model;
};

// Sunlight. Shines along the entity's -Z axis (like a camera looking down -Z).
struct DirectionalLight {
    vec3 color{1.0f, 0.96f, 0.9f};
    f32 intensity = 6.0f;
};

// Demo gameplay component: rotates the entity around an axis.
struct Spin {
    vec3 axis{0.0f, 1.0f, 0.0f};
    f32 speed = 0.5f; // radians per second
};

} // namespace ember::scene

EMBER_REFLECT_DECLARE(ember::scene::NameComponent)
EMBER_REFLECT_DECLARE(ember::scene::Transform)
EMBER_REFLECT_DECLARE(ember::scene::MeshRenderer)
EMBER_REFLECT_DECLARE(ember::scene::DirectionalLight)
EMBER_REFLECT_DECLARE(ember::scene::Spin)
