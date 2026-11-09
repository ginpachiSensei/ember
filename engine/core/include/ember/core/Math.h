#pragma once

// Engine-wide math: glm configured for Vulkan conventions.
// - depth range [0, 1] (not OpenGL's [-1, 1])
// - right-handed world space, +Y up, the camera looks down -Z
// Matrices are column-major and multiply column vectors (M * v), in C++ and in shaders.
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#define GLM_ENABLE_EXPERIMENTAL

#include "ember/core/Types.h"

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace ember {

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat3 = glm::mat3;
using mat4 = glm::mat4;
using quat = glm::quat;
using uvec2 = glm::uvec2;

// Reverse-Z perspective projection with an infinite far plane: depth 1 at the near plane, 0 at infinity.
// Spreads float depth precision evenly over distance. Use depth clear 0 and the "greater" compare.
inline mat4 perspectiveReverseZ(f32 verticalFov, f32 aspect, f32 nearPlane)
{
    const f32 f = 1.0f / std::tan(verticalFov * 0.5f);
    mat4 m(0.0f);
    m[0][0] = f / aspect;
    m[1][1] = f;
    m[2][3] = -1.0f;      // w = -z_view
    m[3][2] = nearPlane;  // z = near, so depth = near / -z_view
    return m;
}

} // namespace ember
