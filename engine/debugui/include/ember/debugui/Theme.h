#pragma once

#include "ember/core/Types.h"

#include <string>

namespace ember::debugui {

struct ThemeDesc {
    std::string fontDirectory;  // folder containing Inter.ttf and fa-solid-900.ttf
    f32 fontSize = 16.0f;       // in unscaled pixels
    f32 dpiScale = 1.0f;        // monitor content scale (e.g. 1.5 at 150%)
    bool linearOutput = true;   // true when rendering into an sRGB backbuffer
};

// Loads Ember's fonts (Inter + Font Awesome icons) and applies its style and colors.
// Call after ImGui::CreateContext() and before the first ImGui::NewFrame().
// Falls back to ImGui's built-in font if the font files are missing.
void applyTheme(const ThemeDesc& desc);

} // namespace ember::debugui
