find_package(VulkanHeaders CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_library(ember_rhi STATIC
    src/vulkan/VulkanDevice.cpp
    src/vulkan/VmaUsage.cpp
    src/vulkan/VmaUsage.h
)
add_library(ember::rhi ALIAS ember_rhi)

target_include_directories(ember_rhi PUBLIC include)
target_link_libraries(ember_rhi
    PUBLIC ember::core ember::platform
    PRIVATE Vulkan::Headers volk::volk GPUOpen::VulkanMemoryAllocator ember_imgui_vulkan
)
ember_configure_target(ember_rhi "engine")
