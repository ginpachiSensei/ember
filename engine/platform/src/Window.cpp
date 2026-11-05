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
}

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

void Window::pollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        bool imguiWantsKeyboard = false;
        if (m_imguiEnabled) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            imguiWantsKeyboard = ImGui::GetIO().WantCaptureKeyboard;
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
