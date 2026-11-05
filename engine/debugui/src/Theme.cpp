#include "ember/debugui/Theme.h"

#include "ember/core/Log.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <cmath>
#include <filesystem>

namespace ember::debugui {

namespace {

// Colors are written in sRGB hex (like in a design tool) and converted if needed.
ImVec4 hex(u32 rgb, f32 alpha = 1.0f)
{
    return ImVec4(static_cast<f32>((rgb >> 16) & 0xFF) / 255.0f, static_cast<f32>((rgb >> 8) & 0xFF) / 255.0f,
                  static_cast<f32>(rgb & 0xFF) / 255.0f, alpha);
}

f32 srgbToLinear(f32 c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

void loadFonts(const ThemeDesc& desc)
{
    ImGuiIO& io = ImGui::GetIO();
    const std::filesystem::path dir = desc.fontDirectory;
    const std::filesystem::path textFont = dir / "Inter.ttf";
    const std::filesystem::path iconFont = dir / "fa-solid-900.ttf";

    if (!std::filesystem::exists(textFont)) {
        EMBER_LOG_WARN("UI font not found at '{}', using ImGui's default font", textFont.string());
        io.Fonts->AddFontDefault();
        return;
    }
    io.Fonts->AddFontFromFileTTF(textFont.string().c_str(), desc.fontSize);

    if (!std::filesystem::exists(iconFont)) {
        EMBER_LOG_WARN("Icon font not found at '{}', icons will be missing", iconFont.string());
        return;
    }
    // Merge the icons into the text font so labels like ICON_FA_FIRE " Ember" just work.
    static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    iconConfig.GlyphMinAdvanceX = desc.fontSize; // fixed-width icons line up in lists
    io.Fonts->AddFontFromFileTTF(iconFont.string().c_str(), desc.fontSize, &iconConfig, iconRanges);
}

void applyStyle(const ThemeDesc& desc)
{
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle(); // start from defaults so this can be called again

    // Shape: rounded, roomy, few borders.
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(6, 6);
    style.ScrollbarSize = 12;
    style.GrabMinSize = 10;

    style.WindowRounding = 8;
    style.ChildRounding = 6;
    style.FrameRounding = 6;
    style.PopupRounding = 6;
    style.ScrollbarRounding = 8;
    style.GrabRounding = 6;
    style.TabRounding = 6;

    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 0;
    style.SeparatorTextBorderSize = 1;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // Palette: neutral dark greys with one ember-orange accent.
    const ImVec4 background = hex(0x16171B);
    const ImVec4 panel = hex(0x1D1F24);
    const ImVec4 frame = hex(0x2A2D35);
    const ImVec4 frameHover = hex(0x343842);
    const ImVec4 frameActive = hex(0x3D424E);
    const ImVec4 border = hex(0x30333B);
    const ImVec4 text = hex(0xE6E8EC);
    const ImVec4 textDim = hex(0x7D838F);
    const ImVec4 accent = hex(0xF2763A);
    const ImVec4 accentHover = hex(0xFF8B52);
    const ImVec4 accentActive = hex(0xD9622A);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_WindowBg] = hex(0x1D1F24, 0.96f);
    c[ImGuiCol_ChildBg] = hex(0x000000, 0.0f);
    c[ImGuiCol_PopupBg] = hex(0x1D1F24, 0.98f);
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_BorderShadow] = hex(0x000000, 0.0f);

    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = frameHover;
    c[ImGuiCol_FrameBgActive] = frameActive;

    c[ImGuiCol_TitleBg] = background;
    c[ImGuiCol_TitleBgActive] = panel;
    c[ImGuiCol_TitleBgCollapsed] = background;
    c[ImGuiCol_MenuBarBg] = background;

    c[ImGuiCol_ScrollbarBg] = hex(0x000000, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = frame;
    c[ImGuiCol_ScrollbarGrabHovered] = frameHover;
    c[ImGuiCol_ScrollbarGrabActive] = frameActive;

    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentActive;

    c[ImGuiCol_Button] = frame;
    c[ImGuiCol_ButtonHovered] = frameHover;
    c[ImGuiCol_ButtonActive] = frameActive;

    c[ImGuiCol_Header] = hex(0xF2763A, 0.20f);
    c[ImGuiCol_HeaderHovered] = hex(0xF2763A, 0.30f);
    c[ImGuiCol_HeaderActive] = hex(0xF2763A, 0.45f);

    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_SeparatorHovered] = accentHover;
    c[ImGuiCol_SeparatorActive] = accent;

    c[ImGuiCol_ResizeGrip] = hex(0xF2763A, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = hex(0xF2763A, 0.5f);
    c[ImGuiCol_ResizeGripActive] = accent;

    c[ImGuiCol_InputTextCursor] = accent;

    c[ImGuiCol_Tab] = panel;
    c[ImGuiCol_TabHovered] = frameHover;
    c[ImGuiCol_TabSelected] = frame;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = background;
    c[ImGuiCol_TabDimmedSelected] = panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = border;

    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotLinesHovered] = accentHover;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentHover;

    c[ImGuiCol_TableHeaderBg] = frame;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight] = hex(0x26282F);
    c[ImGuiCol_TableRowBg] = hex(0x000000, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = hex(0xFFFFFF, 0.03f);

    c[ImGuiCol_TextLink] = accent;
    c[ImGuiCol_TextSelectedBg] = hex(0xF2763A, 0.35f);
    c[ImGuiCol_DragDropTarget] = accent;
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_NavWindowingHighlight] = hex(0xFFFFFF, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = hex(0x000000, 0.5f);
    c[ImGuiCol_ModalWindowDimBg] = hex(0x000000, 0.5f);

    // An sRGB backbuffer expects linear values; convert so the colors match the hex codes above.
    if (desc.linearOutput) {
        for (ImVec4& color : style.Colors) {
            color.x = srgbToLinear(color.x);
            color.y = srgbToLinear(color.y);
            color.z = srgbToLinear(color.z);
        }
    }

    style.ScaleAllSizes(desc.dpiScale);
    style.FontScaleDpi = desc.dpiScale;
}

} // namespace

void applyTheme(const ThemeDesc& desc)
{
    loadFonts(desc);
    applyStyle(desc);
}

} // namespace ember::debugui
