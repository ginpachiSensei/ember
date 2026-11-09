// Debug/editor UI for the sandbox: scene hierarchy, a reflection-driven inspector, and engine stats.

#include "Sandbox.h"

#include "ember/debugui/Inspector.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <vector>

namespace sandbox {

namespace {

constexpr const char* kEntityPayload = "EMBER_ENTITY";

void drawEntityNode(App& app, scene::Entity entity)
{
    scene::Scene& scene = app.scene;
    bool hasChildren = false;
    scene.forEachChild(entity, [&](scene::Entity) { hasChildren = true; });

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (entity == app.selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const char* icon = scene.registry().all_of<scene::DirectionalLight>(entity) ? ICON_FA_SUN
                       : scene.registry().all_of<scene::MeshRenderer>(entity) ? ICON_FA_CUBE
                                                                               : ICON_FA_CIRCLE_NODES;
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(entt::to_integral(entity))),
                                        flags, "%s  %s", icon, scene.name(entity).c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        app.selected = entity;
    }
    // Drag an entity onto another to make it a child.
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(kEntityPayload, &entity, sizeof(entity));
        ImGui::Text("%s", scene.name(entity).c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload)) {
            scene.setParent(*static_cast<const scene::Entity*>(payload->Data), entity);
        }
        ImGui::EndDragDropTarget();
    }
    if (open) {
        std::vector<scene::Entity> children;
        scene.forEachChild(entity, [&](scene::Entity c) { children.push_back(c); });
        for (scene::Entity child : children) {
            drawEntityNode(app, child);
        }
        ImGui::TreePop();
    }
}

void drawScenePanel(App& app)
{
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 436, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin(ICON_FA_SITEMAP "  Scene");
    scene::Scene& scene = app.scene;

    if (ImGui::Button(ICON_FA_PLUS)) {
        app.selected = scene.create("Entity", app.selected);
    }
    ImGui::SetItemTooltip("New entity (child of the selection)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!scene.valid(app.selected));
    if (ImGui::Button(ICON_FA_TRASH)) {
        scene.destroy(app.selected);
        app.selected = scene::kNullEntity;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Delete the selection and its children");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save")) {
        scene.saveFile(app.options.scenePath);
    }
    ImGui::SetItemTooltip("%s", app.options.scenePath.string().c_str());
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load")) {
        if (scene.loadFile(app.options.scenePath)) {
            app.selected = scene::kNullEntity;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE_LEFT " Reset")) {
        buildDefaultScene(app);
    }
    ImGui::Separator();

    std::vector<scene::Entity> roots;
    scene.forEachRoot([&](scene::Entity e) { roots.push_back(e); });
    for (scene::Entity root : roots) {
        drawEntityNode(app, root);
    }
    // Dropping on empty space makes the entity a root again.
    ImGui::InvisibleButton("##drop-root", ImVec2(-1, std::max(ImGui::GetContentRegionAvail().y, 20.0f)));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload)) {
            scene.setParent(*static_cast<const scene::Entity*>(payload->Data), scene::kNullEntity);
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
}

void drawInspector(App& app)
{
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 436, 330), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin(ICON_FA_SLIDERS "  Inspector");
    scene::Scene& scene = app.scene;
    const scene::Entity e = app.selected;
    if (!scene.valid(e)) {
        ImGui::TextDisabled("Select an entity in the Scene panel.");
        ImGui::End();
        return;
    }
    entt::registry& registry = scene.registry();
    ImGui::InputText("Name", &registry.get<scene::NameComponent>(e).name);
    ImGui::TextDisabled("id %s", registry.get<scene::IdComponent>(e).id.toString().c_str());

    // Every registered component is drawn by the same reflection-driven code.
    for (const scene::ComponentType& type : scene::Scene::componentTypes()) {
        void* data = type.get(registry, e);
        if (!data) {
            continue;
        }
        ImGui::PushID(type.name);
        const bool open = ImGui::CollapsingHeader(type.name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        const bool removable = std::string_view(type.name) != "Transform";
        if (removable) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10);
            if (ImGui::SmallButton(ICON_FA_XMARK)) {
                type.remove(registry, e);
                ImGui::PopID();
                continue;
            }
        }
        if (open) {
            debugui::inspect(data, *type.type);
            if (std::string_view(type.name) == "MeshRenderer") {
                // Offer the cooked models by name, since Guids aren't typable.
                auto& renderer = *static_cast<scene::MeshRenderer*>(data);
                if (ImGui::BeginCombo("Asset", "Pick a model...")) {
                    if (ImGui::Selectable("Test shapes (built in)")) {
                        renderer.model = kTestSceneModel;
                    }
                    for (const asset::ManifestEntry& entry : app.assets->manifest().entries) {
                        if (entry.type == asset::AssetType::Model && ImGui::Selectable(entry.source.c_str())) {
                            renderer.model = entry.guid;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::BeginCombo("##add", ICON_FA_PLUS "  Add component")) {
        for (const scene::ComponentType& type : scene::Scene::componentTypes()) {
            if (!type.has(registry, e) && ImGui::Selectable(type.name)) {
                type.add(registry, e);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::End();
}

void drawEnginePanel(App& app)
{
    // Starts collapsed, next to the main panel; expand it from its title bar.
    ImGui::SetNextWindowPos(ImVec2(432, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    ImGui::Begin(ICON_FA_MICROCHIP "  Engine");

    if (ImGui::CollapsingHeader(ICON_FA_MEMORY "  Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("##memory", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Tag");
            ImGui::TableSetupColumn("Live");
            ImGui::TableSetupColumn("Peak");
            ImGui::TableHeadersRow();
            for (u8 t = 0; t < static_cast<u8>(memory::Tag::Count); ++t) {
                const auto tag = static_cast<memory::Tag>(t);
                const memory::TagStats s = memory::stats(tag);
                if (s.peakBytes == 0) {
                    continue;
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(memory::tagName(tag));
                ImGui::TableNextColumn();
                ImGui::Text("%.1f KB (%lld)", static_cast<f64>(s.bytes) / 1024.0, static_cast<long long>(s.allocations));
                ImGui::TableNextColumn();
                ImGui::Text("%.1f KB", static_cast<f64>(s.peakBytes) / 1024.0);
            }
            ImGui::EndTable();
        }
        const render::RenderStats& stats = app.renderer->stats();
        ImGui::TextDisabled("Renderer frame scratch: %.1f / %.0f KB", static_cast<f64>(stats.frameMemoryUsed) / 1024.0,
                            static_cast<f64>(stats.frameMemoryCapacity) / 1024.0);
        const rhi::Device::MemoryUsage gpu = app.device->gpuMemory();
        ImGui::TextDisabled("GPU memory: %.0f / %.0f MB", static_cast<f64>(gpu.usedBytes) / (1024.0 * 1024.0),
                            static_cast<f64>(gpu.budgetBytes) / (1024.0 * 1024.0));
    }

    if (ImGui::CollapsingHeader(ICON_FA_LIST_CHECK "  Jobs")) {
        ImGui::Text("%u threads (main + %u workers)", app.jobs->threadCount(), app.jobs->threadCount() - 1);
        ImGui::TextDisabled("World transforms and asset loads run on the workers.");
    }

    if (ImGui::CollapsingHeader(ICON_FA_BOX_ARCHIVE "  Assets", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Edit a file in content/ and it re-cooks and reloads.");
        for (const asset::ManifestEntry& entry : app.assets->manifest().entries) {
            ImGui::PushID(entry.cooked.c_str());
            const asset::AssetState state = app.assets->state(entry.guid);
            ImGui::Text("%s", entry.source.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s v%u", asset::assetStateName(state), app.assets->version(entry.guid));
            if (state != asset::AssetState::Unloaded) {
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_ROTATE)) {
                    app.assets->reload(entry.guid);
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void drawRenderPanel(App& app)
{
    const ImGuiIO& io = ImGui::GetIO();
    render::Renderer& renderer = *app.renderer;
    render::RenderSettings& settings = renderer.settings();

    app.frameTimesMs[app.frameTimeIndex] = io.DeltaTime * 1000.0f;
    app.frameTimeIndex = (app.frameTimeIndex + 1) % app.frameTimesMs.size();

    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin(ICON_FA_FIRE "  Ember");

    if (ImGui::CollapsingHeader(ICON_FA_GAUGE_HIGH "  Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("%.1f FPS", io.Framerate);
        ImGui::SameLine();
        ImGui::TextDisabled("(%.2f ms)", 1000.0f / io.Framerate);
        ImGui::PlotLines("##frametimes", app.frameTimesMs.data(), static_cast<int>(app.frameTimesMs.size()),
                         static_cast<int>(app.frameTimeIndex), "frame time (ms)", 0.0f, 33.3f, ImVec2(-1, 60));
        const render::RenderStats& stats = renderer.stats();
        ImGui::TextDisabled("%s", app.device->gpuName());
        ImGui::TextDisabled("%u x %u  |  %u draws  |  %u shadow draws  |  %.1fk tris", app.device->backbufferWidth(),
                            app.device->backbufferHeight(), stats.drawCalls, stats.shadowDrawCalls,
                            static_cast<f32>(stats.triangles) / 1000.0f);
    }

    if (ImGui::CollapsingHeader(ICON_FA_CAMERA "  Image", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Exposure (EV)", &settings.exposureEv, -6.0f, 6.0f, "%.1f");
        const char* tonemappers[] = {"None (clip)", "Reinhard", "ACES (fitted)", "AgX", "Khronos PBR Neutral"};
        int tonemapper = static_cast<int>(settings.tonemapper);
        if (ImGui::Combo("Tonemapper", &tonemapper, tonemappers, IM_ARRAYSIZE(tonemappers))) {
            settings.tonemapper = static_cast<render::Tonemapper>(tonemapper);
        }
        const char* views[] = {"Lit", "Base color", "Normals", "Roughness", "Metallic", "Occlusion", "Shadow"};
        int view = static_cast<int>(settings.debugView);
        if (ImGui::Combo("Debug view", &view, views, IM_ARRAYSIZE(views))) {
            settings.debugView = static_cast<render::DebugView>(view);
        }
        if (ImGui::Button(ICON_FA_IMAGE "  Screenshot (F12)")) {
            app.requestScreenshot = true;
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_SUN "  Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("IBL intensity", &settings.iblIntensity, 0.0f, 3.0f);
        ImGui::SliderFloat("Sky intensity", &settings.skyIntensity, 0.0f, 3.0f);
        ImGui::BeginDisabled(!app.environmentAsset.isValid());
        if (ImGui::Checkbox("HDR sky (hdri/sky.hdr)", &app.useHdr)) {
            app.environmentApplied = false;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("The sun is the \"Sun\" entity: rotate it in the Inspector.");
    }

    if (ImGui::CollapsingHeader(ICON_FA_CIRCLE_HALF_STROKE "  Shadows")) {
        static const u32 sizes[] = {512, 1024, 2048, 4096};
        int index = 0;
        for (int i = 0; i < 4; ++i) {
            if (sizes[i] == settings.shadowMapSize) {
                index = i;
            }
        }
        if (ImGui::Combo("Map size", &index, "512\0" "1024\0" "2048\0" "4096\0\0")) {
            settings.shadowMapSize = sizes[index];
        }
        ImGui::SliderFloat("Depth bias", &settings.shadowBiasConstant, 0.0f, 10.0f);
        ImGui::SliderFloat("Slope bias", &settings.shadowBiasSlope, 0.0f, 10.0f);
        ImGui::SliderFloat("Normal bias (texels)", &settings.shadowNormalBias, 0.0f, 5.0f);
    }

    if (ImGui::CollapsingHeader(ICON_FA_CODE "  Shaders")) {
        ImGui::TextDisabled("Edit any file in shaders/ and save: it recompiles live.");
        const std::string& error = renderer.shaderError();
        if (error.empty()) {
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), ICON_FA_CHECK "  All shaders compiled");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION "  Compile error:");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(error.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::SliderFloat("Camera speed", &app.fly.speed, 0.1f, 50.0f, "%.1f m/s", ImGuiSliderFlags_Logarithmic);
    ImGui::TextDisabled("Hold right mouse + WASD/QE to fly");
    ImGui::End();
}

} // namespace

void drawEditor(App& app)
{
    drawRenderPanel(app);
    drawScenePanel(app);
    drawInspector(app);
    drawEnginePanel(app);
}

} // namespace sandbox
