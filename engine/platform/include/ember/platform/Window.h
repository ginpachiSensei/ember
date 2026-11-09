#pragma once

#include "ember/core/Types.h"

#include <array>
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

enum class Key : u8 {
    W, A, S, D, Q, E, R, F,
    Space, LeftShift, LeftCtrl,
    F1, F2, F3, F5, F12,
    Count,
};

enum class MouseButton : u8 { Left, Right, Middle, Count };

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

    // Input, as seen by the game: keys and buttons ImGui is using are reported as up.
    bool isKeyDown(Key key) const { return m_keys[static_cast<usize>(key)]; }
    bool wasKeyPressed(Key key) const { return m_pressed[static_cast<usize>(key)]; } // this frame
    bool isMouseDown(MouseButton button) const { return m_mouse[static_cast<usize>(button)]; }
    f32 mouseDeltaX() const { return m_mouseDeltaX; } // pixels moved this frame
    f32 mouseDeltaY() const { return m_mouseDeltaY; }
    f32 scrollDelta() const { return m_scroll; }
    // Hides the cursor and reports unbounded relative motion (for mouse-look).
    void setRelativeMouseMode(bool enabled);

    // Content scale of the monitor the window is on (1.0 = 100%, 1.5 = 150%).
    f32 displayScale() const;

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
    bool m_relativeMouse = false;

    std::array<bool, static_cast<usize>(Key::Count)> m_keys{};
    std::array<bool, static_cast<usize>(Key::Count)> m_pressed{};
    std::array<bool, static_cast<usize>(MouseButton::Count)> m_mouse{};
    f32 m_mouseDeltaX = 0.0f;
    f32 m_mouseDeltaY = 0.0f;
    f32 m_scroll = 0.0f;
};

} // namespace ember::platform
