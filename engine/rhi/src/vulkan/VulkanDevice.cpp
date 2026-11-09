#include "VulkanCommon.h"

#include "ember/core/Profile.h"

#if defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#endif

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cstring>
#include <optional>

namespace ember::rhi {

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

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    // The loader reports harmless setup details (like registry lookups) as warnings; skip those.
    if (data->pMessageIdName && std::strcmp(data->pMessageIdName, "Loader Message") == 0 &&
        !(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) {
        return VK_FALSE;
    }
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

// Features every Ember pipeline relies on. Checked when picking the GPU and enabled on the device.
struct FeatureChain {
    VkPhysicalDeviceFeatures2 core{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features v11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};

    FeatureChain()
    {
        core.pNext = &v11;
        v11.pNext = &v12;
        v12.pNext = &v13;
    }
    FeatureChain(const FeatureChain&) = delete;
    FeatureChain& operator=(const FeatureChain&) = delete;

    // Fills in what Ember needs.
    void request()
    {
        core.features.samplerAnisotropy = VK_TRUE;
        core.features.depthClamp = VK_TRUE;
        core.features.shaderInt64 = VK_TRUE;      // 64-bit buffer pointers
        core.features.shaderInt16 = VK_TRUE;
        core.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        core.features.shaderStorageImageReadWithoutFormat = VK_TRUE;
        core.features.multiDrawIndirect = VK_TRUE;
        core.features.drawIndirectFirstInstance = VK_TRUE;
        core.features.fillModeNonSolid = VK_TRUE;
        v11.shaderDrawParameters = VK_TRUE;
        v12.bufferDeviceAddress = VK_TRUE;
        v12.scalarBlockLayout = VK_TRUE; // C++ structs and shader structs share the same tight layout
        v12.timelineSemaphore = VK_TRUE;
        v12.descriptorIndexing = VK_TRUE;
        v12.runtimeDescriptorArray = VK_TRUE;
        v12.descriptorBindingPartiallyBound = VK_TRUE;
        v12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        v12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
        v12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        v12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        v12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
        v12.samplerFilterMinmax = VK_TRUE; // min-reduction for the Hi-Z pyramid (phase 5)
        v12.drawIndirectCount = VK_TRUE;
        v12.shaderFloat16 = VK_TRUE;
        v12.storageBuffer8BitAccess = VK_TRUE;
        v12.vulkanMemoryModel = VK_TRUE;
        v12.vulkanMemoryModelDeviceScope = VK_TRUE;
        v13.dynamicRendering = VK_TRUE;
        v13.synchronization2 = VK_TRUE;
        v13.maintenance4 = VK_TRUE;
        v13.shaderDemoteToHelperInvocation = VK_TRUE; // what `discard` compiles to in SPIR-V 1.6
    }

    // Returns the name of the first requested feature `supported` lacks, or nullptr.
    const char* firstMissing(const FeatureChain& supported) const
    {
#define EMBER_CHECK_FEATURE(field)                       \
    if (field && !supported.field) {                     \
        return #field;                                   \
    }
        EMBER_CHECK_FEATURE(core.features.samplerAnisotropy)
        EMBER_CHECK_FEATURE(core.features.depthClamp)
        EMBER_CHECK_FEATURE(core.features.shaderInt64)
        EMBER_CHECK_FEATURE(core.features.shaderInt16)
        EMBER_CHECK_FEATURE(core.features.shaderStorageImageWriteWithoutFormat)
        EMBER_CHECK_FEATURE(core.features.shaderStorageImageReadWithoutFormat)
        EMBER_CHECK_FEATURE(core.features.multiDrawIndirect)
        EMBER_CHECK_FEATURE(core.features.drawIndirectFirstInstance)
        EMBER_CHECK_FEATURE(core.features.fillModeNonSolid)
        EMBER_CHECK_FEATURE(v11.shaderDrawParameters)
        EMBER_CHECK_FEATURE(v12.bufferDeviceAddress)
        EMBER_CHECK_FEATURE(v12.scalarBlockLayout)
        EMBER_CHECK_FEATURE(v12.timelineSemaphore)
        EMBER_CHECK_FEATURE(v12.descriptorIndexing)
        EMBER_CHECK_FEATURE(v12.runtimeDescriptorArray)
        EMBER_CHECK_FEATURE(v12.descriptorBindingPartiallyBound)
        EMBER_CHECK_FEATURE(v12.descriptorBindingSampledImageUpdateAfterBind)
        EMBER_CHECK_FEATURE(v12.descriptorBindingStorageImageUpdateAfterBind)
        EMBER_CHECK_FEATURE(v12.descriptorBindingUpdateUnusedWhilePending)
        EMBER_CHECK_FEATURE(v12.shaderSampledImageArrayNonUniformIndexing)
        EMBER_CHECK_FEATURE(v12.shaderStorageImageArrayNonUniformIndexing)
        EMBER_CHECK_FEATURE(v12.samplerFilterMinmax)
        EMBER_CHECK_FEATURE(v12.drawIndirectCount)
        EMBER_CHECK_FEATURE(v12.shaderFloat16)
        EMBER_CHECK_FEATURE(v12.storageBuffer8BitAccess)
        EMBER_CHECK_FEATURE(v12.vulkanMemoryModel)
        EMBER_CHECK_FEATURE(v12.vulkanMemoryModelDeviceScope)
        EMBER_CHECK_FEATURE(v13.dynamicRendering)
        EMBER_CHECK_FEATURE(v13.synchronization2)
        EMBER_CHECK_FEATURE(v13.maintenance4)
        EMBER_CHECK_FEATURE(v13.shaderDemoteToHelperInvocation)
#undef EMBER_CHECK_FEATURE
        return nullptr;
    }
};

Format fromVk(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_SRGB: return Format::BGRA8Srgb;
    case VK_FORMAT_R8G8B8A8_SRGB: return Format::RGBA8Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8Unorm;
    case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8Unorm;
    default: return Format::Unknown;
    }
}

} // namespace

bool Device::Impl::init()
{
    pendingWidth = desc.width;
    pendingHeight = desc.height;
    frameCommandListImpl.device = this;

    if (!createInstance() || !createSurface() || !pickGpu() || !createDevice()) {
        return false;
    }
    createBindless();
    createFrames();
    if (!createSwapchain(desc.width, desc.height)) {
        swapchainDirty = true; // e.g. started minimized; retry on the next frame
    }
    return true;
}

bool Device::Impl::createInstance()
{
#if defined(EMBER_VK_LAYER_PATH) && defined(_WIN32)
    // The validation layer comes from vcpkg instead of the Vulkan SDK; tell the loader where it is.
    char* existing = nullptr;
    usize existingLength = 0;
    _dupenv_s(&existing, &existingLength, "VK_ADD_LAYER_PATH");
    if (desc.enableValidation && !existing) {
        _putenv_s("VK_ADD_LAYER_PATH", EMBER_VK_LAYER_PATH);
    }
    std::free(existing);
#endif

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

    // Debug utils also gives object names and command labels in RenderDoc / Nsight, so use it when present.
    if (hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        debugUtils = true;
    }

    std::vector<const char*> layers;
    if (desc.enableValidation) {
        if (hasInstanceLayer("VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        } else {
            EMBER_LOG_WARN("Validation layer not found (expected from vcpkg's vulkan-validationlayers).");
        }
    }

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Ember";
    appInfo.pEngineName = "Ember";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // Chaining the messenger info also reports problems inside vkCreateInstance itself.
    VkDebugUtilsMessengerCreateInfoEXT debugInfo = makeDebugMessengerInfo();
    const bool useMessenger = debugUtils && !layers.empty();

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pNext = useMessenger ? &debugInfo : nullptr;
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

    if (useMessenger) {
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

    FeatureChain required;
    required.request();

    int bestScore = -1;
    for (VkPhysicalDevice candidate : candidates) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }

        FeatureChain supported;
        vkGetPhysicalDeviceFeatures2(candidate, &supported.core);
        if (const char* missing = required.firstMissing(supported)) {
            EMBER_LOG_INFO("Skipping GPU {}: missing feature {}", props.deviceName, missing);
            continue;
        }
        if (!hasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        // One queue family that can draw, compute and present keeps things simple for now.
        u32 familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

        std::optional<u32> family;
        for (u32 i = 0; i < familyCount; ++i) {
            VkBool32 canPresent = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &canPresent);
            const VkQueueFlags needed = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((families[i].queueFlags & needed) == needed && canPresent) {
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
        EMBER_LOG_ERROR("No GPU supports Vulkan 1.3 with the features Ember needs");
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

    FeatureChain features;
    features.request();

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &features.core;
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

void Device::Impl::createBindless()
{
    // One big table for the whole engine. Shaders index it with plain integers (see Bindless.slang).
    const VkDescriptorSetLayoutBinding bindings[] = {
        {kBindingSampled, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxSampledTextures, VK_SHADER_STAGE_ALL, nullptr},
        {kBindingSamplers, VK_DESCRIPTOR_TYPE_SAMPLER, kMaxSamplers, VK_SHADER_STAGE_ALL, nullptr},
        {kBindingStorage, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kMaxStorageImages, VK_SHADER_STAGE_ALL, nullptr},
    };
    // Partially bound: unused slots may stay empty. Update-after-bind: new textures can be added
    // while earlier frames that use the table are still running on the GPU.
    const VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                           VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                           VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    const VkDescriptorBindingFlags bindingFlags[] = {flags, flags, flags};

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flagsInfo.bindingCount = 3;
    flagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    EMBER_VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &bindlessLayout));

    const VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxSampledTextures},
        {VK_DESCRIPTOR_TYPE_SAMPLER, kMaxSamplers},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kMaxStorageImages},
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    EMBER_VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &bindlessPool));

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = bindlessPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &bindlessLayout;
    EMBER_VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &bindlessSet));

    VkPushConstantRange pushRange{VK_SHADER_STAGE_ALL, 0, kMaxPushConstantSize};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &bindlessLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    EMBER_VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));
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

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily;
    EMBER_VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &immediatePool));
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    EMBER_VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &immediateFence));
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
    // TRANSFER_SRC lets screenshots copy the image back to the CPU.
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            (caps.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
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
    swapchainRhiFormat = fromVk(format.format);
    swapchainExtent = extent;
    swapchainMinImageCount = imageCount;

    u32 actualCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &actualCount, nullptr);
    std::vector<VkImage> images(actualCount);
    vkGetSwapchainImagesKHR(device, swapchain, &actualCount, images.data());

    // Swapchain images are registered as ordinary textures, so render passes can target them like any other.
    renderFinished.resize(actualCount);
    for (u32 i = 0; i < actualCount; ++i) {
        VulkanTexture texture;
        texture.image = images[i];
        texture.desc.width = extent.width;
        texture.desc.height = extent.height;
        texture.desc.format = swapchainRhiFormat;
        texture.desc.usage = TextureUsage::ColorAttachment | TextureUsage::TransferDst | TextureUsage::TransferSrc;
        texture.desc.debugName = "Backbuffer";

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        EMBER_VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &texture.view));
        swapchainTextures.push_back(textures.emplace(std::move(texture)));

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        EMBER_VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinished[i]));
    }

    EMBER_LOG_INFO("Swapchain: {}x{}, {} images, {}", extent.width, extent.height, actualCount,
                   presentMode == VK_PRESENT_MODE_FIFO_KHR ? "vsync" : "no vsync");
    return true;
}

void Device::Impl::destroySwapchainObjects()
{
    for (TextureHandle handle : swapchainTextures) {
        vkDestroyImageView(device, texture(handle).view, nullptr);
        textures.remove(handle);
    }
    for (VkSemaphore semaphore : renderFinished) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    swapchainTextures.clear();
    renderFinished.clear();
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

void Device::Impl::runPendingDeletes(bool all)
{
    auto it = pendingDeletes.begin();
    while (it != pendingDeletes.end()) {
        if (all || it->frame <= completedFrame) {
            it->destroy();
            it = pendingDeletes.erase(it);
        } else {
            ++it;
        }
    }
}

void Device::Impl::setDebugName(VkObjectType type, u64 object, const char* name)
{
    if (!debugUtils || !name) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = object;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(device, &info);
}

void Device::Impl::shutdown()
{
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        if (imguiEnabled) {
            ImGui_ImplVulkan_Shutdown();
            imguiEnabled = false;
        }
        runPendingDeletes(true);
        for (FrameData& frame : frames) {
            vkDestroySemaphore(device, frame.imageAvailable, nullptr);
            vkDestroyFence(device, frame.inFlight, nullptr);
            vkDestroyCommandPool(device, frame.commandPool, nullptr); // also frees its command buffer
        }
        vkDestroyCommandPool(device, immediatePool, nullptr);
        vkDestroyFence(device, immediateFence, nullptr);
        destroySwapchainObjects();
        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        }
        if (textures.size() || buffers.size() || pipelines.size()) {
            EMBER_LOG_WARN("RHI shutdown with live resources: {} textures, {} buffers, {} pipelines",
                           textures.size(), buffers.size(), pipelines.size());
        }
        // Samplers are owned by the device for its whole life.
        samplers.forEach([&](VulkanSampler& s) { vkDestroySampler(device, s.sampler, nullptr); });
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorPool(device, bindlessPool, nullptr);
        vkDestroyDescriptorSetLayout(device, bindlessLayout, nullptr);
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
    instance = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------------------------
// Public API: lifetime and frames
// ---------------------------------------------------------------------------------------------

std::unique_ptr<Device> Device::create(const DeviceDesc& desc)
{
    auto impl = std::make_unique<Impl>();
    impl->desc = desc;
    impl->frameCommandList.m_impl = &impl->frameCommandListImpl;
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

CommandList* Device::beginFrame()
{
    Impl& d = *m_impl;
    EMBER_ASSERT(!d.frameActive);

    if (d.swapchainDirty && !d.recreateSwapchain()) {
        return nullptr;
    }

    FrameData& frame = d.frames[d.frameIndex];

    // Wait until the GPU is done with the last use of this frame's resources.
    {
        EMBER_PROFILE_SCOPE("Wait for GPU (fence)");
        EMBER_VK_CHECK(vkWaitForFences(d.device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX));
    }
    d.completedFrame = std::max(d.completedFrame, frame.frameNumber);
    d.runPendingDeletes(false);

    VkResult acquire = VK_SUCCESS;
    {
        EMBER_PROFILE_SCOPE("Acquire swapchain image");
        acquire = vkAcquireNextImageKHR(d.device, d.swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE,
                                        &d.imageIndex);
    }
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        d.swapchainDirty = true;
        return nullptr; // nothing was signaled, so it's safe to skip
    }
    if (acquire == VK_SUBOPTIMAL_KHR) {
        d.swapchainDirty = true; // still usable; rebuild after this frame
    } else if (acquire != VK_SUCCESS) {
        EMBER_VK_CHECK(acquire);
    }

    // Only reset the fence once we know we will submit work that signals it.
    EMBER_VK_CHECK(vkResetFences(d.device, 1, &frame.inFlight));
    EMBER_VK_CHECK(vkResetCommandPool(d.device, frame.commandPool, 0));
    frame.frameNumber = d.frameNumber;

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    EMBER_VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, d.pipelineLayout, 0, 1,
                            &d.bindlessSet, 0, nullptr);
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, d.pipelineLayout, 0, 1,
                            &d.bindlessSet, 0, nullptr);

    // The previous contents of the swapchain image are irrelevant.
    d.texture(d.swapchainTextures[d.imageIndex]).state = ResourceState::Undefined;

    d.frameCommandListImpl.cmd = frame.commandBuffer;
    d.frameActive = true;
    return &d.frameCommandList;
}

void Device::endFrame()
{
    Impl& d = *m_impl;
    EMBER_ASSERT(d.frameActive);
    FrameData& frame = d.frames[d.frameIndex];

    d.frameCommandList.barrier(d.swapchainTextures[d.imageIndex], ResourceState::Present);
    EMBER_VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

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
    ++d.frameNumber;
    d.frameActive = false;
}

void Device::immediateSubmit(const std::function<void(CommandList&)>& record)
{
    EMBER_PROFILE_FUNCTION();
    Impl& d = *m_impl;

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = d.immediatePool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    EMBER_VK_CHECK(vkAllocateCommandBuffers(d.device, &allocInfo, &cmd));

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    EMBER_VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.pipelineLayout, 0, 1, &d.bindlessSet, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d.pipelineLayout, 0, 1, &d.bindlessSet, 0, nullptr);

    CommandList::Impl listImpl{&d, cmd};
    CommandList list;
    list.m_impl = &listImpl;
    record(list);

    EMBER_VK_CHECK(vkEndCommandBuffer(cmd));
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    EMBER_VK_CHECK(vkQueueSubmit2(d.graphicsQueue, 1, &submit, d.immediateFence));
    EMBER_VK_CHECK(vkWaitForFences(d.device, 1, &d.immediateFence, VK_TRUE, UINT64_MAX));
    EMBER_VK_CHECK(vkResetFences(d.device, 1, &d.immediateFence));
    vkFreeCommandBuffers(d.device, d.immediatePool, 1, &cmd);
}

TextureHandle Device::backbuffer() const
{
    EMBER_ASSERT(m_impl->frameActive);
    return m_impl->swapchainTextures[m_impl->imageIndex];
}

Format Device::backbufferFormat() const
{
    return m_impl->swapchainRhiFormat;
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

u32 Device::frameIndex() const
{
    return m_impl->frameIndex;
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
    info.ImageCount = std::max(info.MinImageCount, static_cast<u32>(d.swapchainTextures.size()));
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

void Device::renderImGui(CommandList& cmd)
{
    EMBER_PROFILE_FUNCTION();
    Impl& d = *m_impl;
    EMBER_ASSERT(d.frameActive && d.imguiEnabled);

    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData) {
        return;
    }

    ScopedLabel label(cmd, "ImGui");
    RenderingDesc rendering;
    rendering.colorCount = 1;
    rendering.colors[0].texture = backbuffer();
    rendering.colors[0].load = LoadOp::Load; // draw on top of what's already there
    cmd.beginRendering(rendering);
    ImGui_ImplVulkan_RenderDrawData(drawData, cmd.m_impl->cmd);
    cmd.endRendering();
    // ImGui binds its own descriptor set; put the bindless table back for anything recorded later.
    vkCmdBindDescriptorSets(cmd.m_impl->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.pipelineLayout, 0, 1,
                            &d.bindlessSet, 0, nullptr);
}

} // namespace ember::rhi
