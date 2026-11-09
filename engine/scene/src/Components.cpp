#include "ember/scene/Components.h"

EMBER_REFLECT_BEGIN(ember::scene::NameComponent)
    EMBER_FIELD(name)
EMBER_REFLECT_END(ember::scene::NameComponent)

EMBER_REFLECT_BEGIN(ember::scene::Transform)
    EMBER_FIELD(position, {.speed = 0.05f})
    EMBER_FIELD(rotation)
    EMBER_FIELD(scale, {.min = 0.001f, .max = 1000.0f, .speed = 0.01f})
EMBER_REFLECT_END(ember::scene::Transform)

EMBER_REFLECT_BEGIN(ember::scene::MeshRenderer)
    EMBER_FIELD(model)
EMBER_REFLECT_END(ember::scene::MeshRenderer)

EMBER_REFLECT_BEGIN(ember::scene::DirectionalLight)
    EMBER_FIELD(color, {.color = true})
    EMBER_FIELD(intensity, {.min = 0.0f, .max = 50.0f, .speed = 0.05f})
EMBER_REFLECT_END(ember::scene::DirectionalLight)

EMBER_REFLECT_BEGIN(ember::scene::Spin)
    EMBER_FIELD(axis)
    EMBER_FIELD(speed, {.min = -10.0f, .max = 10.0f})
EMBER_REFLECT_END(ember::scene::Spin)
