# Third-party code that we compile ourselves (everything else comes from vcpkg).

find_package(imgui CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)

# ImGui's Vulkan backend, built in "volk mode". The vcpkg build of this backend calls Vulkan
# functions directly, which collides with volk's function pointers of the same names.
# Copied from the vcpkg imgui source (keep it at the same version as the imgui port).
add_library(ember_imgui_vulkan STATIC
    imgui_backends/imgui_impl_vulkan.cpp
    imgui_backends/imgui_impl_vulkan.h
)
target_include_directories(ember_imgui_vulkan SYSTEM PUBLIC imgui_backends)
target_compile_definitions(ember_imgui_vulkan PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(ember_imgui_vulkan PUBLIC imgui::imgui volk::volk Vulkan::Headers)
set_target_properties(ember_imgui_vulkan PROPERTIES FOLDER "external")
