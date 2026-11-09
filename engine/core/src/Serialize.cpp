#include "ember/core/Serialize.h"

#include "ember/core/Log.h"

#include <fstream>

namespace ember {

namespace {

template <int N, typename V>
Json vectorToJson(const V& v)
{
    Json array = Json::array();
    for (int i = 0; i < N; ++i) {
        array.push_back(v[i]);
    }
    return array;
}

template <int N, typename V>
void vectorFromJson(V& v, const Json& json)
{
    if (json.is_array() && json.size() == N) {
        for (int i = 0; i < N; ++i) {
            if (json[static_cast<usize>(i)].is_number()) {
                v[i] = json[static_cast<usize>(i)].get<f32>();
            }
        }
    }
}

} // namespace

Json serialize(const void* object, const TypeInfo& type)
{
    Json json = Json::object();
    for (const FieldInfo& field : type.fields) {
        Json& value = json[field.name];
        switch (field.kind) {
        case FieldKind::Bool: value = field.get<bool>(object); break;
        case FieldKind::I32: value = field.get<i32>(object); break;
        case FieldKind::U32: value = field.get<u32>(object); break;
        case FieldKind::F32: value = field.get<f32>(object); break;
        case FieldKind::Vec2: value = vectorToJson<2>(field.get<vec2>(object)); break;
        case FieldKind::Vec3: value = vectorToJson<3>(field.get<vec3>(object)); break;
        case FieldKind::Vec4: value = vectorToJson<4>(field.get<vec4>(object)); break;
        case FieldKind::Quat: {
            const quat& q = field.get<quat>(object);
            value = Json::array({q.x, q.y, q.z, q.w});
            break;
        }
        case FieldKind::String: value = field.get<std::string>(object); break;
        case FieldKind::Guid: value = field.get<Guid>(object).toString(); break;
        case FieldKind::Enum: {
            const i32 index = field.get<i32>(object);
            const auto names = field.enumNames();
            value = index >= 0 && static_cast<usize>(index) < names.size() ? Json(names[static_cast<usize>(index)]) : Json(index);
            break;
        }
        case FieldKind::Struct:
            value = serialize(static_cast<const std::byte*>(object) + field.offset, field.nested());
            break;
        }
    }
    return json;
}

void deserialize(void* object, const TypeInfo& type, const Json& json)
{
    if (!json.is_object()) {
        return;
    }
    for (const FieldInfo& field : type.fields) {
        const auto it = json.find(field.name);
        if (it == json.end()) {
            continue; // keep the default
        }
        const Json& value = *it;
        switch (field.kind) {
        case FieldKind::Bool:
            if (value.is_boolean()) {
                field.get<bool>(object) = value.get<bool>();
            }
            break;
        case FieldKind::I32:
            if (value.is_number()) {
                field.get<i32>(object) = value.get<i32>();
            }
            break;
        case FieldKind::U32:
            if (value.is_number()) {
                field.get<u32>(object) = value.get<u32>();
            }
            break;
        case FieldKind::F32:
            if (value.is_number()) {
                field.get<f32>(object) = value.get<f32>();
            }
            break;
        case FieldKind::Vec2: vectorFromJson<2>(field.get<vec2>(object), value); break;
        case FieldKind::Vec3: vectorFromJson<3>(field.get<vec3>(object), value); break;
        case FieldKind::Vec4: vectorFromJson<4>(field.get<vec4>(object), value); break;
        case FieldKind::Quat: {
            vec4 v(0.0f, 0.0f, 0.0f, 1.0f);
            vectorFromJson<4>(v, value);
            field.get<quat>(object) = glm::normalize(quat(v.w, v.x, v.y, v.z));
            break;
        }
        case FieldKind::String:
            if (value.is_string()) {
                field.get<std::string>(object) = value.get<std::string>();
            }
            break;
        case FieldKind::Guid:
            if (value.is_string()) {
                field.get<Guid>(object) = Guid::parse(value.get<std::string>()).value_or(Guid{});
            }
            break;
        case FieldKind::Enum: {
            const auto names = field.enumNames();
            if (value.is_string()) {
                for (usize i = 0; i < names.size(); ++i) {
                    if (value.get<std::string>() == names[i]) {
                        field.get<i32>(object) = static_cast<i32>(i);
                    }
                }
            } else if (value.is_number()) {
                field.get<i32>(object) = value.get<i32>();
            }
            break;
        }
        case FieldKind::Struct:
            deserialize(static_cast<std::byte*>(object) + field.offset, field.nested(), value);
            break;
        }
    }
}

std::optional<Json> readJsonFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }
    Json json = Json::parse(file, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (json.is_discarded()) {
        EMBER_LOG_ERROR("Invalid JSON in {}", path.string());
        return std::nullopt;
    }
    return json;
}

bool writeJsonFile(const std::filesystem::path& path, const Json& json)
{
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    // Write to a temp file and rename, so a crash mid-write never leaves a half-written file.
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream file(temp);
        if (!file) {
            EMBER_LOG_ERROR("Can't write {}", path.string());
            return false;
        }
        file << json.dump(2) << '\n';
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        EMBER_LOG_ERROR("Can't replace {}: {}", path.string(), ec.message());
        return false;
    }
    return true;
}

} // namespace ember
