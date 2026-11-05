#include "ember/rhi/Device.h"

#include "ember/core/Assert.h"
#include "ember/core/Log.h"
#include "ember/core/Profile.h"

#include "VmaUsage.h"

#if defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#endif

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

namespace ember::rhi {

namespace {

constexpr u32 kFramesInFlight = 2;

const char* vkResultName(VkResult result)
{
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "unknown VkResult";
    }
}

} // namespace

// Any failure here is a bug or a broken driver, so we stop immediately with a clear message.
#define EMBER_VK_CHECK(expr)                                                        \
    do {                                                                            \
        const VkResult result_ = (expr);                                            \
        if (result_ != VK_SUCCESS) {                                                \
            EMBER_LOG_FATAL("{} failed: {} ({}:{})", #expr, vkResultName(result_),  \
                            __FILE__, __LINE__);                                    \
            std::abort();                                                           \
        }                                                                           \
    } while (0)

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        EMBER_LOG_ERROR("[vulkan] {}", data->pMessage);
    } else {
        EMBER_LOG_WARN("[vulkan] {}", data->pMessage);
    }
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT makeDebugMessengerInfo()
{
    VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
    return info;
}

bool hasInstanceLayer(const char* name)
{
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::any_of(layers.begin(), layers.end(),
                       [&](const VkLayerProperties& l) { return std::strcmp(l.layerName, name) == 0; });
}

bool hasInstanceExtension(const char* name)
{
    u32 count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(),
                       [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

bool hasDeviceExtension(VkPhysicalDevice gpu, const char* name)
{
    u32 count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(),
                       [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

// Records an image layout transition using synchronization2.
void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

struct FrameData {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;           // signaled when the GPU finished this frame
    VkSemaphore imageAvailable = VK_NULL_HANDLE; // signaled when the swapchain image can be drawn to
};

} // namespace

struct Device::Impl {
    DeviceDesc desc;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties gpuProperties{};
    u32 graphicsFamily = 0;

    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
    u32 swapchainMinImageCount = 0;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkSemaphore> renderFinished; // one per swapchain image, waited on by present

    std::array<FrameData, kFramesInFlight> frames{};
    u32 frameIndex = 0;
    u32 imageIndex = 0;
    bool frameActive = false;

    bool swapchainDirty = false;
    u32 pendingWidth = 0;
    u32 pendingHeight = 0;

    bool imguiEnabled = false;

    bool init();
    void shutdown();

    bool createInstance();
    bool createSurface();
    bool pickGpu();
    bool createDevice();
    void createFrames();
    bool createSwapchain(u32 width, u32 height);
    void destroySwapchainObjects();
    bool recreateSwapchain();
};

bool Device::Impl::init()
{
    pendingWidth = desc.width;
    pendingHeight = desc.height;

    if (!createInstance() || !createSurface() || !pickGpu() || !createDevice()) {
        return false;
    }
    createFrames();
    if (!createSwapchain(desc.width, desc.height)) {
        swapchainDirty = true; // e.g. started minimized; retry on the next frame
    }
    return true;
}

bool Device::Impl::createInstance()
{
    if (volkInitialize() != VK_SUCCESS) {
        EMBER_LOG_ERROR("Vulkan loader not found. Is a GPU driver with Vulkan support installed?");
        return false;
    }

    u32 loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) {
        vkEnumerateInstanceVersion(&loaderVersion);
    }
    if (loaderVersion < VK_API_VERSION_1_3) {
        EMBER_LOG_ERROR("Vulkan 1.3 is required, but the loader only supports {}.{}",
                        VK_API_VERSION_MAJOR(loaderVersion), VK_API_VERSION_MINOR(loaderVersion));
        return false;
    }

    std::vector<const char*> extensions = {VK_KHR_SURFACE_EXTENSION_NAME};
#if defined(_WIN32)
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif

    std::vector<const char*> layers;
    bool useDebugUtils = false;
    if (desc.enableValidation) {
        if (hasInstanceLayer("VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        } else {
            EMBER_LOG_WARN("Validation layer not found. Install the Vulkan SDK to enable it.");
        }
        if (hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            useDebugUtils = true;
        }
    }

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Ember";
    appInfo.pEngineName = "Ember";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // Chaining the messenger info also reports problems inside vkCreateInstance itself.
    VkDebugUtilsMessengerCreateInfoEXT debugInfo = makeDebugMessengerInfo();

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pNext = useDebugUtils ? &debugInfo : nullptr;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = static_cast<u32>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    createInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        EMBER_LOG_ERROR("vkCreateInstance failed: {}", vkResultName(result));
        return false;
    }
    volkLoadInstance(instance);

    if (useDebugUtils) {
        EMBER_VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &debugInfo, nullptr, &debugMessenger));
    }
    EMBER_LOG_INFO("Vulkan instance created (validation {})", layers.empty() ? "off" : "on");
    return true;
}

bool Device::Impl::createSurface()
{
#if defined(_WIN32)
    // Loaded by hand so we don't depend on how volk was compiled.
    const auto createWin32Surface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    if (!createWin32Surface) {
        EMBER_LOG_ERROR("vkCreateWin32SurfaceKHR is not available");
        return false;
    }

    VkWin32SurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    createInfo.hwnd = static_cast<HWND>(desc.window.hwnd);
    createInfo.hinstance = desc.window.hinstance ? static_cast<HINSTANCE>(desc.window.hinstance)
                                                 : GetModuleHandleW(nullptr);

    const VkResult result = createWin32Surface(instance, &createInfo, nullptr, &surface);
    if (result != VK_SUCCESS) {
        EMBER_LOG_ERROR("Failed to create window surface: {}", vkResultName(result));
        return false;
    }
    return true;
#else
    EMBER_LOG_ERROR("Surface creation is only implemented for Windows");
    return false;
#endif
}

bool Device::Impl::pickGpu()
{
    u32 count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> candidates(count);
    vkEnumeratePhysicalDevices(instance, &count, candidates.data());

    int bestScore = -1;
    for (VkPhysicalDevice candidate : candidates) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }

        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        features12.pNext = &features13;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &features12;
        vkGetPhysicalDeviceFeatures2(candidate, &features);

        if (!features13.dynamicRendering || !features13.synchronization2 || !features12.timelineSemaphore ||
            !features12.bufferDeviceAddress) {
            continue;
        }
        if (!hasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        // One queue family that can both draw and present keeps phase 0 simple.
        u32 familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

        std::optional<u32> family;
        for (u32 i = 0; i < familyCount; ++i) {
            VkBool32 canPresent = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &canPresent);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && canPresent) {
                family = i;
                break;
            }
        }
        if (!family) {
            continue;
        }

        const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 1000
                          : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 100
                                                                                        : 10;
        if (score > bestScore) {
            bestScore = score;
            gpu = candidate;
            gpuProperties = props;
            graphicsFamily = *family;
        }
    }

    if (gpu == VK_NULL_HANDLE) {
        EMBER_LOG_ERROR("No GPU supports Vulkan 1.3 with dynamic rendering and synchronization2");
        return false;
    }

    EMBER_LOG_INFO("GPU: {} (Vulkan {}.{}.{})", gpuProperties.deviceName,
                   VK_API_VERSION_MAJOR(gpuProperties.apiVersion), VK_API_VERSION_MINOR(gpuProperties.apiVersion),
                   VK_API_VERSION_PATCH(gpuProperties.apiVersion));
    return true;
}

bool Device::Impl::createDevice()
{
    const f32 queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = graphicsFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    // Modern Vulkan baseline: no render pass objects, simpler barriers,
    // timeline semaphores and GPU pointers (needed later for GPU-driven rendering).
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.pNext = &features13;
    features12.timelineSemaphore = VK_TRUE;
    features12.bufferDeviceAddress = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &features12;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &features;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;

    const VkResult result = vkCreateDevice(gpu, &createInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        EMBER_LOG_ERROR("vkCreateDevice failed: {}", vkResultName(result));
        return false;
    }
    volkLoadDevice(device);
    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);

    VmaVulkanFunctions vmaFunctions{};
    vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.physicalDevice = gpu;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.pVulkanFunctions = &vmaFunctions;
    EMBER_VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator));
    return true;
}

void Device::Impl::createFrames()
{
    for (FrameData& frame : frames) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = graphicsFamily;
        EMBER_VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPool));

        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        EMBER_VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &frame.commandBuffer));

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // so the first wait returns immediately
        EMBER_VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlight));

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        EMBER_VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable));
    }
}

bool Device::Impl::createSwapchain(u32 width, u32 height)
{
    VkSurfaceCapabilitiesKHR caps;
    EMBER_VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) { // the window lets us pick the size
        extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        return false; // minimized
    }

    u32 formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, formats.data());

    // Prefer an sRGB format so the hardware converts our linear colors on write.
    VkSurfaceFormatKHR format = formats[0];
    for (const VkSurfaceFormatKHR& candidate : formats) {
        if ((candidate.format == VK_FORMAT_B8G8R8A8_SRGB || candidate.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = candidate;
            break;
        }
    }

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync; always supported
    if (!desc.vsync) {
        u32 modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, modes.data());
        for (VkPresentModeKHR preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) {
                presentMode = preferred;
                break;
            }
        }
    }

    u32 imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = format.format;
    createInfo.imageColorSpace = format.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = swapchain;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    EMBER_VK_CHECK(vkCreateSwapchainKHR(device, &createInfo, nullptr, &newSwapchain));

    // The caller made sure the GPU is idle, so the old objects can go now.
    destroySwapchainObjects();
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }
    swapchain = newSwapchain;
    swapchainFormat = format.format;
    swapchainExtent = extent;
    swapchainMinImageCount = imageCount;

    u32 actualCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &actualCount, nullptr);
    swapchainImages.resize(actualCount);
    vkGetSwapchainImagesKHR(device, swapchain, &actualCount, swapchainImages.data());

    swapchainViews.resize(actualCount);
    renderFinished.resize(actualCount);
    for (u32 i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        EMBER_VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &swapchainViews[i]));

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        EMBER_VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinished[i]));
    }

    EMBER_LOG_INFO("Swapchain: {}x{}, {} images, {}", extent.width, extent.height, actualCount,
                   presentMode == VK_PRESENT_MODE_FIFO_KHR ? "vsync" : "no vsync");
    return true;
}

void Device::Impl::destroySwapchainObjects()
{
    for (VkImageView view : swapchainViews) {
        vkDestroyImageView(device, view, nullptr);
    }
    for (VkSemaphore semaphore : renderFinished) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    swapchainViews.clear();
    renderFinished.clear();
    swapchainImages.clear();
}

bool Device::Impl::recreateSwapchain()
{
    EMBER_PROFILE_FUNCTION();
    vkDeviceWaitIdle(device);
    if (!createSwapchain(pendingWidth, pendingHeight)) {
        return false; // still minimized; stay dirty and try again next frame
    }
    if (imguiEnabled) {
        ImGui_ImplVulkan_SetMinImageCount(std::max(2u, swapchainMinImageCount));
    }
    swapchainDirty = false;
    return true;
}

void Device::Impl::shutdown()
{
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        if (imguiEnabled) {
            ImGui_ImplVulkan_Shutdown();
            imguiEnabled = false;
        }
        for (FrameData& frame : frames) {
            vkDestroySemaphore(device, frame.imageAvailable, nullptr);
            vkDestroyFence(device, frame.inFlight, nullptr);
            vkDestroyCommandPool(device, frame.commandPool, nullptr); // also frees its command buffer
        }
        destroySwapchainObjects();
        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        }
        vmaDestroyAllocator(allocator);
        vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }
        if (debugMessenger != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }
        vkDestroyInstance(instance, nullptr);
    }
    *this = Impl{};
}

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

std::unique_ptr<Device> Device::create(const DeviceDesc& desc)
{
    auto impl = std::make_unique<Impl>();
    impl->desc = desc;
    if (!impl->init()) {
        impl->shutdown();
        return nullptr;
    }
    return std::unique_ptr<Device>(new Device(std::move(impl)));
}

Device::Device(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

Device::~Device()
{
    m_impl->shutdown();
}

bool Device::beginFrame()
{
    Impl& d = *m_impl;
    EMBER_ASSERT(!d.frameActive);

    if (d.swapchainDirty && !d.recreateSwapchain()) {
        return false;
    }

    FrameData& frame = d.frames[d.frameIndex];

    // Wait until the GPU is done with the last use of this frame's resources.
    {
        EMBER_PROFILE_SCOPE("Wait for GPU (fence)");
        EMBER_VK_CHECK(vkWaitForFences(d.device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX));
    }

    VkResult acquire = VK_SUCCESS;
    {
        EMBER_PROFILE_SCOPE("Acquire swapchain image");
        acquire = vkAcquireNextImageKHR(d.device, d.swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE,
                                        &d.imageIndex);
    }
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        d.swapchainDirty = true;
        return false; // nothing was signaled, so it's safe to skip
    }
    if (acquire == VK_SUBOPTIMAL_KHR) {
        d.swapchainDirty = true; // still usable; rebuild after this frame
    } else if (acquire != VK_SUCCESS) {
        EMBER_VK_CHECK(acquire);
    }

    // Only reset the fence once we know we will submit work that signals it.
    EMBER_VK_CHECK(vkResetFences(d.device, 1, &frame.inFlight));
    EMBER_VK_CHECK(vkResetCommandPool(d.device, frame.commandPool, 0));

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    EMBER_VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    // The previous contents are irrelevant (UNDEFINED); make the image ready to be rendered to.
    transitionImage(frame.commandBuffer, d.swapchainImages[d.imageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    d.frameActive = true;
    return true;
}

void Device::clearBackbuffer(const ClearColor& color)
{
    Impl& d = *m_impl;
    EMBER_ASSERT(d.frameActive);
    VkCommandBuffer cmd = d.frames[d.frameIndex].commandBuffer;

    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = d.swapchainViews[d.imageIndex];
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = {{color.r, color.g, color.b, color.a}};

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, d.swapchainExtent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &attachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    // Draw calls go here in phase 1.
    vkCmdEndRendering(cmd);
}

void Device::endFrame()
{
    Impl& d = *m_impl;
    EMBER_ASSERT(d.frameActive);
    FrameData& frame = d.frames[d.frameIndex];

    transitionImage(frame.commandBuffer, d.swapchainImages[d.imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    EMBER_VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = d.renderFinished[d.imageIndex];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = frame.commandBuffer;

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitInfo;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalInfo;
    EMBER_VK_CHECK(vkQueueSubmit2(d.graphicsQueue, 1, &submit, frame.inFlight));

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &d.renderFinished[d.imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &d.swapchain;
    present.pImageIndices = &d.imageIndex;

    VkResult result = VK_SUCCESS;
    {
        EMBER_PROFILE_SCOPE("Present");
        result = vkQueuePresentKHR(d.graphicsQueue, &present);
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        d.swapchainDirty = true;
    } else if (result != VK_SUCCESS) {
        EMBER_VK_CHECK(result);
    }

    d.frameIndex = (d.frameIndex + 1) % kFramesInFlight;
    d.frameActive = false;
}

void Device::resize(u32 width, u32 height)
{
    m_impl->pendingWidth = width;
    m_impl->pendingHeight = height;
    m_impl->swapchainDirty = true;
}

void Device::waitIdle()
{
    vkDeviceWaitIdle(m_impl->device);
}

const char* Device::gpuName() const
{
    return m_impl->gpuProperties.deviceName;
}

u32 Device::backbufferWidth() const
{
    return m_impl->swapchainExtent.width;
}

u32 Device::backbufferHeight() const
{
    return m_impl->swapchainExtent.height;
}

bool Device::isBackbufferSrgb() const
{
    const VkFormat format = m_impl->swapchainFormat;
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
}

// ---------------------------------------------------------------------------------------------
// Dear ImGui
// ---------------------------------------------------------------------------------------------

namespace {

void checkImGuiVkResult(VkResult result)
{
    if (result < 0) { // negative values are errors; positive ones are status codes
        EMBER_LOG_FATAL("ImGui Vulkan backend error: {}", vkResultName(result));
        std::abort();
    }
}

} // namespace

void Device::initImGui()
{
    Impl& d = *m_impl;
    if (d.imguiEnabled) {
        return;
    }
    EMBER_VERIFY(d.swapchainFormat != VK_FORMAT_UNDEFINED);

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = d.instance;
    info.PhysicalDevice = d.gpu;
    info.Device = d.device;
    info.QueueFamily = d.graphicsFamily;
    info.Queue = d.graphicsQueue;
    info.DescriptorPoolSize = 64; // let the backend create its own small descriptor pool
    info.MinImageCount = std::max(2u, d.swapchainMinImageCount);
    info.ImageCount = std::max(info.MinImageCount, static_cast<u32>(d.swapchainImages.size()));
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &d.swapchainFormat;
    info.MinAllocationSize = 1024 * 1024;
    info.CheckVkResultFn = checkImGuiVkResult;
    EMBER_VERIFY(ImGui_ImplVulkan_Init(&info));
    d.imguiEnabled = true;
}

void Device::shutdownImGui()
{
    Impl& d = *m_impl;
    if (d.imguiEnabled) {
        vkDeviceWaitIdle(d.device);
        ImGui_ImplVulkan_Shutdown();
        d.imguiEnabled = false;
    }
}

void Device::beginImGuiFrame()
{
    ImGui_ImplVulkan_NewFrame();
}

void Device::renderImGui()
{
    EMBER_PROFILE_FUNCTION();
    Impl& d = *m_impl;
    EMBER_ASSERT(d.frameActive && d.imguiEnabled);

    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData) {
        return;
    }
    VkCommandBuffer cmd = d.frames[d.frameIndex].commandBuffer;

    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = d.swapchainViews[d.imageIndex];
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // draw on top of what's already there
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, d.swapchainExtent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &attachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
    vkCmdEndRendering(cmd);
}

} // namespace ember::rhi
