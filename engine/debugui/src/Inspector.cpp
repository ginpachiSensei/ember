#include "ember/debugui/Inspector.h"

#include <imgui.h>
#include <imgui_stdlib.h>

namespace ember::debugui {

namespace {

ImGuiSliderFlags flagsFor(const FieldHints& hints)
{
    return hints.min < hints.max ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
}

} // namespace

bool inspect(void* object, const TypeInfo& type)
{
    bool changed = false;
    for (const FieldInfo& field : type.fields) {
        ImGui::PushID(field.name);
        const FieldHints& h = field.hints;
        switch (field.kind) {
        case FieldKind::Bool: changed |= ImGui::Checkbox(field.name, &field.get<bool>(object)); break;
        case FieldKind::I32:
            changed |= ImGui::DragInt(field.name, &field.get<i32>(object), 1.0f, static_cast<int>(h.min),
                                      static_cast<int>(h.max), "%d", flagsFor(h));
            break;
        case FieldKind::U32: {
            const u32 min = static_cast<u32>(h.min);
            const u32 max = static_cast<u32>(h.max);
            changed |= ImGui::DragScalar(field.name, ImGuiDataType_U32, &field.get<u32>(object), 1.0f,
                                         h.min < h.max ? &min : nullptr, h.min < h.max ? &max : nullptr);
            break;
        }
        case FieldKind::F32:
            changed |= ImGui::DragFloat(field.name, &field.get<f32>(object), h.speed, h.min, h.max, "%.3f", flagsFor(h));
            break;
        case FieldKind::Vec2:
            changed |= ImGui::DragFloat2(field.name, &field.get<vec2>(object).x, h.speed, h.min, h.max, "%.3f", flagsFor(h));
            break;
        case FieldKind::Vec3:
            changed |= h.color ? ImGui::ColorEdit3(field.name, &field.get<vec3>(object).x, ImGuiColorEditFlags_Float)
                               : ImGui::DragFloat3(field.name, &field.get<vec3>(object).x, h.speed, h.min, h.max, "%.3f",
                                                   flagsFor(h));
            break;
        case FieldKind::Vec4:
            changed |= h.color ? ImGui::ColorEdit4(field.name, &field.get<vec4>(object).x, ImGuiColorEditFlags_Float)
                               : ImGui::DragFloat4(field.name, &field.get<vec4>(object).x, h.speed, h.min, h.max, "%.3f",
                                                   flagsFor(h));
            break;
        case FieldKind::Quat: {
            // Edited as Euler angles in degrees. Converting back and forth every frame would make the
            // angles jump around (Euler angles aren't unique), so only write when the user drags.
            quat& q = field.get<quat>(object);
            vec3 degrees = glm::degrees(glm::eulerAngles(q));
            if (ImGui::DragFloat3(field.name, &degrees.x, 0.5f, 0.0f, 0.0f, "%.1f deg")) {
                q = glm::normalize(quat(glm::radians(degrees)));
                changed = true;
            }
            break;
        }
        case FieldKind::String: changed |= ImGui::InputText(field.name, &field.get<std::string>(object)); break;
        case FieldKind::Guid: {
            const Guid& guid = field.get<Guid>(object);
            ImGui::LabelText(field.name, "%s", guid.isValid() ? guid.toString().c_str() : "(none)");
            break;
        }
        case FieldKind::Enum: {
            i32& value = field.get<i32>(object);
            const auto names = field.enumNames();
            const char* preview = value >= 0 && static_cast<usize>(value) < names.size() ? names[static_cast<usize>(value)] : "?";
            if (ImGui::BeginCombo(field.name, preview)) {
                for (usize i = 0; i < names.size(); ++i) {
                    if (ImGui::Selectable(names[i], static_cast<usize>(value) == i)) {
                        value = static_cast<i32>(i);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case FieldKind::Struct:
            if (ImGui::TreeNode(field.name)) {
                changed |= inspect(static_cast<std::byte*>(object) + field.offset, field.nested());
                ImGui::TreePop();
            }
            break;
        }
        ImGui::PopID();
    }
    return changed;
}

} // namespace ember::debugui
