#pragma once

// Minimal reflection: a runtime description of a struct's fields (name, offset, kind).
// It is what lets one generic function save, load or draw an editor for *any* registered type.
//
// In a header, at global namespace scope:
//     EMBER_REFLECT_DECLARE(game::Health)
// In one .cpp, at global namespace scope:
//     EMBER_REFLECT_BEGIN(game::Health)
//         EMBER_FIELD(current)
//         EMBER_FIELD(maximum, {.min = 1.0f, .max = 1000.0f})   // optional editor hints
//     EMBER_REFLECT_END(game::Health)
//
// Supported field types: bool, i32, u32, f32, vec2/3/4, quat, std::string, Guid, reflected enums
// (EMBER_REFLECT_ENUM) and other reflected structs (nested).

#include "ember/core/Guid.h"
#include "ember/core/Math.h"
#include "ember/core/Types.h"

#include <cstddef>
#include <span>
#include <string>
#include <type_traits>

namespace ember {

enum class FieldKind : u8 { Bool, I32, U32, F32, Vec2, Vec3, Vec4, Quat, String, Guid, Enum, Struct };

struct FieldHints {
    f32 min = 0.0f; // min == max: no range limit
    f32 max = 0.0f;
    f32 speed = 0.01f; // drag speed in the editor
    bool color = false; // vec3/vec4 edited as a color
    bool angles = false; // quat edited as Euler angles in degrees (always true for quats)
};

struct TypeInfo;

struct FieldInfo {
    const char* name = nullptr;
    usize offset = 0;
    FieldKind kind = FieldKind::F32;
    const TypeInfo& (*nested)() = nullptr;          // FieldKind::Struct
    std::span<const char* const> (*enumNames)() = nullptr; // FieldKind::Enum; values are 0..n-1, stored as i32-sized
    FieldHints hints;

    template <typename T>
    T& get(void* object) const
    {
        return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
    }
    template <typename T>
    const T& get(const void* object) const
    {
        return *reinterpret_cast<const T*>(static_cast<const std::byte*>(object) + offset);
    }
};

struct TypeInfo {
    const char* name = nullptr;
    usize size = 0;
    std::span<const FieldInfo> fields;
};

// Specialized for every reflected type by EMBER_REFLECT_DECLARE.
template <typename T>
struct Reflect;

template <typename T>
concept Reflected = requires { Reflect<T>::info(); };

template <typename T>
const TypeInfo& typeOf()
{
    return Reflect<T>::info();
}

// Specialized by EMBER_REFLECT_ENUM.
template <typename E>
struct EnumNames;

template <typename T>
concept ReflectedEnum = std::is_enum_v<T> && requires { EnumNames<T>::names(); };

template <typename T>
FieldInfo makeField(const char* name, usize offset, FieldHints hints = {})
{
    FieldInfo f;
    f.name = name;
    f.offset = offset;
    f.hints = hints;
    if constexpr (std::is_same_v<T, bool>) {
        f.kind = FieldKind::Bool;
    } else if constexpr (std::is_same_v<T, i32>) {
        f.kind = FieldKind::I32;
    } else if constexpr (std::is_same_v<T, u32>) {
        f.kind = FieldKind::U32;
    } else if constexpr (std::is_same_v<T, f32>) {
        f.kind = FieldKind::F32;
    } else if constexpr (std::is_same_v<T, vec2>) {
        f.kind = FieldKind::Vec2;
    } else if constexpr (std::is_same_v<T, vec3>) {
        f.kind = FieldKind::Vec3;
    } else if constexpr (std::is_same_v<T, vec4>) {
        f.kind = FieldKind::Vec4;
    } else if constexpr (std::is_same_v<T, quat>) {
        f.kind = FieldKind::Quat;
    } else if constexpr (std::is_same_v<T, std::string>) {
        f.kind = FieldKind::String;
    } else if constexpr (std::is_same_v<T, Guid>) {
        f.kind = FieldKind::Guid;
    } else if constexpr (ReflectedEnum<T>) {
        static_assert(sizeof(T) == sizeof(i32), "reflected enums must be 32-bit");
        f.kind = FieldKind::Enum;
        f.enumNames = &EnumNames<T>::names;
    } else if constexpr (Reflected<T>) {
        f.kind = FieldKind::Struct;
        f.nested = &Reflect<T>::info;
    } else {
        static_assert(sizeof(T) == 0, "this field type can't be reflected (see Reflect.h for the list)");
    }
    return f;
}

} // namespace ember

#define EMBER_REFLECT_DECLARE(Type)                    \
    template <>                                        \
    struct ember::Reflect<Type> {                      \
        static const ::ember::TypeInfo& info();        \
    };

#define EMBER_REFLECT_BEGIN(Type)                              \
    const ::ember::TypeInfo& ember::Reflect<Type>::info()      \
    {                                                          \
        using Self = Type;                                     \
        static const ::ember::FieldInfo fields[] = {

#define EMBER_FIELD(name, ...) \
    ::ember::makeField<decltype(Self::name)>(#name, offsetof(Self, name) __VA_OPT__(, ) __VA_ARGS__),

#define EMBER_REFLECT_END(Type)                                                     \
        };                                                                          \
        static const ::ember::TypeInfo info{#Type, sizeof(Self), fields};           \
        return info;                                                                \
    }

// Enum values must be 0, 1, 2, ... in the order of the names. At global namespace scope, in a header.
#define EMBER_REFLECT_ENUM(Type, ...)                                    \
    template <>                                                          \
    struct ember::EnumNames<Type> {                                      \
        static std::span<const char* const> names()                     \
        {                                                                \
            static const char* const values[] = {__VA_ARGS__};           \
            return values;                                               \
        }                                                                \
    };
