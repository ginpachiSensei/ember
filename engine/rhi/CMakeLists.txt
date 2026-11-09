find_package(VulkanHeaders CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_library(ember_rhi STATIC
    src/vulkan/VulkanCommon.h
    src/vulkan/VulkanDevice.cpp
    src/vulkan/VulkanResources.cpp
    src/vulkan/VulkanCommandList.cpp
    src/vulkan/VmaUsage.cpp
    src/vulkan/VmaUsage.h
)
add_library(ember::rhi ALIAS ember_rhi)

target_include_directories(ember_rhi PUBLIC include)
target_link_libraries(ember_rhi
    PUBLIC ember::core ember::platform
    PRIVATE Vulkan::Headers volk::volk GPUOpen::VulkanMemoryAllocator ember_imgui_vulkan
)
# The validation layer comes from vcpkg (vulkan-validationlayers), so no Vulkan SDK is needed.
# The release build of the layer is used for every configuration: it's much faster.
target_compile_definitions(ember_rhi PRIVATE
    EMBER_VK_LAYER_PATH="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
ember_configure_target(ember_rhi "engine")
