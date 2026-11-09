#include "ember/platform/Window.h"

#include "ember/core/Log.h"

#define SDL_MAIN_HANDLED // we use our own main()
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>

namespace ember::platform {

namespace {
int g_windowCount = 0; // SDL is initialized with the first window and shut down with the last

int keyIndex(SDL_Scancode code)
{
    switch (code) {
    case SDL_SCANCODE_W: return static_cast<int>(Key::W);
    case SDL_SCANCODE_A: return static_cast<int>(Key::A);
    case SDL_SCANCODE_S: return static_cast<int>(Key::S);
    case SDL_SCANCODE_D: return static_cast<int>(Key::D);
    case SDL_SCANCODE_Q: return static_cast<int>(Key::Q);
    case SDL_SCANCODE_E: return static_cast<int>(Key::E);
    case SDL_SCANCODE_R: return static_cast<int>(Key::R);
    case SDL_SCANCODE_F: return static_cast<int>(Key::F);
    case SDL_SCANCODE_SPACE: return static_cast<int>(Key::Space);
    case SDL_SCANCODE_LSHIFT: return static_cast<int>(Key::LeftShift);
    case SDL_SCANCODE_LCTRL: return static_cast<int>(Key::LeftCtrl);
    case SDL_SCANCODE_F1: return static_cast<int>(Key::F1);
    case SDL_SCANCODE_F2: return static_cast<int>(Key::F2);
    case SDL_SCANCODE_F3: return static_cast<int>(Key::F3);
    case SDL_SCANCODE_F5: return static_cast<int>(Key::F5);
    case SDL_SCANCODE_F12: return static_cast<int>(Key::F12);
    default: return -1;
    }
}

int buttonIndex(Uint8 button)
{
    switch (button) {
    case SDL_BUTTON_LEFT: return static_cast<int>(MouseButton::Left);
    case SDL_BUTTON_RIGHT: return static_cast<int>(MouseButton::Right);
    case SDL_BUTTON_MIDDLE: return static_cast<int>(MouseButton::Middle);
    default: return -1;
    }
}
} // namespace

std::unique_ptr<Window> Window::create(const WindowDesc& desc)
{
    if (g_windowCount == 0) {
        SDL_SetMainReady();
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            EMBER_LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
            return nullptr;
        }
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), static_cast<int>(desc.width),
                                          static_cast<int>(desc.height), flags);
    if (!window) {
        EMBER_LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        if (g_windowCount == 0) {
            SDL_Quit();
        }
        return nullptr;
    }

    ++g_windowCount;
    return std::unique_ptr<Window>(new Window(window));
}

Window::Window(SDL_Window* window)
    : m_window(window)
{
}

Window::~Window()
{
    shutdownImGui();
    SDL_DestroyWindow(m_window);
    if (--g_windowCount == 0) {
        SDL_Quit();
    }
}

void Window::setRelativeMouseMode(bool enabled)
{
    if (enabled != m_relativeMouse) {
        SDL_SetWindowRelativeMouseMode(m_window, enabled);
        m_relativeMouse = enabled;
    }
}

void Window::pollEvents()
{
    m_pressed.fill(false);
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    m_scroll = 0.0f;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        bool imguiWantsKeyboard = false;
        bool imguiWantsMouse = false;
        if (m_imguiEnabled) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            // While the mouse is captured for looking around, the game owns all input.
            imguiWantsKeyboard = ImGui::GetIO().WantCaptureKeyboard && !m_relativeMouse;
            imguiWantsMouse = ImGui::GetIO().WantCaptureMouse && !m_relativeMouse;
        }

        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            m_shouldClose = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            m_resized = true;
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            m_minimized = true;
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            m_minimized = false;
            m_resized = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_ESCAPE && !imguiWantsKeyboard) {
                m_shouldClose = true;
            }
            if (const int key = keyIndex(event.key.scancode); key >= 0 && !imguiWantsKeyboard) {
                m_pressed[key] = !event.key.repeat;
                m_keys[key] = true;
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (const int key = keyIndex(event.key.scancode); key >= 0) {
                m_keys[key] = false; // releases always count, so keys never get stuck
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (const int button = buttonIndex(event.button.button); button >= 0 && !imguiWantsMouse) {
                m_mouse[button] = true;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (const int button = buttonIndex(event.button.button); button >= 0) {
                m_mouse[button] = false;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (!imguiWantsMouse) {
                m_mouseDeltaX += event.motion.xrel;
                m_mouseDeltaY += event.motion.yrel;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (!imguiWantsMouse) {
                m_scroll += event.wheel.y;
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            m_keys.fill(false);
            m_mouse.fill(false);
            break;
        default:
            break;
        }
    }
}

u32 Window::width() const
{
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
    return static_cast<u32>(w);
}

u32 Window::height() const
{
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
    return static_cast<u32>(h);
}

f32 Window::displayScale() const
{
    const f32 scale = SDL_GetWindowDisplayScale(m_window);
    return scale > 0.0f ? scale : 1.0f; // 0 means SDL couldn't tell
}

NativeWindowHandle Window::nativeHandle() const
{
    NativeWindowHandle handle;
#if defined(_WIN32)
    const SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
    handle.hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    handle.hinstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
#endif
    return handle;
}

void Window::initImGui()
{
    if (!m_imguiEnabled) {
        ImGui_ImplSDL3_InitForVulkan(m_window);
        m_imguiEnabled = true;
    }
}

void Window::shutdownImGui()
{
    if (m_imguiEnabled) {
        ImGui_ImplSDL3_Shutdown();
        m_imguiEnabled = false;
    }
}

void Window::beginImGuiFrame()
{
    ImGui_ImplSDL3_NewFrame();
}

} // namespace ember::platform
