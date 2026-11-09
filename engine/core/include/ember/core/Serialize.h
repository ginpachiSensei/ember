#pragma once

// JSON (de)serialization driven by reflection: any reflected type can be saved and loaded.
// Loading is forgiving: unknown keys are ignored and missing keys keep their default values,
// so adding or removing a field doesn't break existing files.

#include "ember/core/Reflect.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>

namespace ember {

using Json = nlohmann::json;

Json serialize(const void* object, const TypeInfo& type);
void deserialize(void* object, const TypeInfo& type, const Json& json);

template <Reflected T>
Json toJson(const T& object)
{
    return serialize(&object, typeOf<T>());
}

template <Reflected T>
T fromJson(const Json& json, T object = {})
{
    deserialize(&object, typeOf<T>(), json);
    return object;
}

std::optional<Json> readJsonFile(const std::filesystem::path& path);   // logs and returns nullopt on failure
bool writeJsonFile(const std::filesystem::path& path, const Json& json); // pretty-printed

} // namespace ember
