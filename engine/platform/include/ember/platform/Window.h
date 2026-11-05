#pragma once

#include "ember/core/Types.h"

#include <memory>
#include <string>

struct SDL_Window;

namespace ember::platform {

struct WindowDesc {
    std::string title = "Ember";
    u32 width = 1280;
    u32 height = 720;
    bool resizable = true;
};

// OS window handles, so the RHI can create a surface without knowing about SDL.
struct NativeWindowHandle {
    void* hwnd = nullptr;      // Win32 HWND
    void* hinstance = nullptr; // Win32 HINSTANCE
};

class Window {
public:
    static std::unique_ptr<Window> create(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Processes OS events. Call once per frame.
    void pollEvents();

    bool shouldClose() const { return m_shouldClose; }
    bool isMinimized() const { return m_minimized; }

    // True once after the drawable size changed.
    bool consumeResized()
    {
        const bool resized = m_resized;
        m_resized = false;
        return resized;
    }

    // Drawable size in pixels (can differ from window size on high-DPI displays).
    u32 width() const;
    u32 height() const;

    NativeWindowHandle nativeHandle() const;

    // Dear ImGui platform side (input, window size). Needs an ImGui context to exist.
    void initImGui();
    void shutdownImGui();
    void beginImGuiFrame();

private:
    explicit Window(SDL_Window* window);

    SDL_Window* m_window = nullptr;
    bool m_shouldClose = false;
    bool m_minimized = false;
    bool m_resized = false;
    bool m_imguiEnabled = false;
};

} // namespace ember::platform
