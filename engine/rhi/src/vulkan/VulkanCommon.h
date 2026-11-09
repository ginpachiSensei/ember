#pragma once

// Internal to the Vulkan backend: the real definitions behind Device::Impl and CommandList::Impl.

#include "ember/core/Assert.h"
#include "ember/core/Log.h"
#include "ember/core/SlotMap.h"
#include "ember/rhi/Device.h"

#include "VmaUsage.h"

#include <array>
#include <cstdlib>
#include <functional>
#include <vector>

namespace ember::rhi {

const char* vkResultName(VkResult result);

// Any failure here is a bug or a broken driver, so we stop immediately with a clear message.
#define EMBER_VK_CHECK(expr)                                                                       \
    do {                                                                                           \
        const VkResult result_ = (expr);                                                           \
        if (result_ != VK_SUCCESS) {                                                               \
            EMBER_LOG_FATAL("{} failed: {} ({}:{})", #expr, ::ember::rhi::vkResultName(result_),   \
                            __FILE__, __LINE__);                                                   \
            std::abort();                                                                          \
        }                                                                                          \
    } while (0)

// Sizes of the bindless tables (descriptor set 0). Must match shaders/core/Bindless.slang.
inline constexpr u32 kMaxSampledTextures = 16384;
inline constexpr u32 kMaxSamplers = 128;
inline constexpr u32 kMaxStorageImages = 4096;
inline constexpr u32 kBindingSampled = 0;
inline constexpr u32 kBindingSamplers = 1;
inline constexpr u32 kBindingStorage = 2;
inline constexpr u32 kInvalidIndex = ~0u;

VkFormat toVk(Format format);

struct VulkanBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceAddress address = 0;
    BufferDesc desc;
};

struct VulkanTexture {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE; // null for swapchain images (owned by the swapchain)
    VkImageView view = VK_NULL_HANDLE;         // full view, used for sampling and attachments
    std::vector<VkImageView> mipViews;         // per-mip storage views
    std::vector<u32> storageIndices;           // per-mip bindless storage indices
    u32 sampledIndex = kInvalidIndex;
    ResourceState state = ResourceState::Undefined;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    TextureDesc desc;
};

struct VulkanSampler {
    VkSampler sampler = VK_NULL_HANDLE;
    u32 index = kInvalidIndex;
};

struct VulkanPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
};

// Hands out indices into a bindless table and recycles freed ones.
class IndexAllocator {
public:
    explicit IndexAllocator(u32 capacity)
        : m_capacity(capacity)
    {
    }
    u32 allocate()
    {
        if (!m_free.empty()) {
            const u32 index = m_free.back();
            m_free.pop_back();
            return index;
        }
        EMBER_VERIFY(m_next < m_capacity);
        return m_next++;
    }
    void release(u32 index) { m_free.push_back(index); }

private:
    u32 m_capacity;
    u32 m_next = 0;
    std::vector<u32> m_free;
};

struct FrameData {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;           // signaled when the GPU finished this frame
    VkSemaphore imageAvailable = VK_NULL_HANDLE; // signaled when the swapchain image can be drawn to
    u64 frameNumber = 0;                         // which frame last used this slot
};

struct CommandList::Impl {
    Device::Impl* device = nullptr;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
};

struct Device::Impl {
    DeviceDesc desc;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    bool debugUtils = false;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties gpuProperties{};
    u32 graphicsFamily = 0;

    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    // Bindless table + the one pipeline layout every pipeline uses.
    VkDescriptorSetLayout bindlessLayout = VK_NULL_HANDLE;
    VkDescriptorPool bindlessPool = VK_NULL_HANDLE;
    VkDescriptorSet bindlessSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    IndexAllocator sampledIndices{kMaxSampledTextures};
    IndexAllocator samplerIndices{kMaxSamplers};
    IndexAllocator storageIndices{kMaxStorageImages};

    SlotMap<VulkanBuffer, BufferTag> buffers;
    SlotMap<VulkanTexture, TextureTag> textures;
    SlotMap<VulkanSampler, SamplerTag> samplers;
    SlotMap<VulkanPipeline, PipelineTag> pipelines;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    Format swapchainRhiFormat = Format::Unknown;
    VkExtent2D swapchainExtent{};
    u32 swapchainMinImageCount = 0;
    std::vector<TextureHandle> swapchainTextures;
    std::vector<VkSemaphore> renderFinished; // one per swapchain image, waited on by present

    std::array<FrameData, kFramesInFlight> frames{};
    u32 frameIndex = 0;
    u32 imageIndex = 0;
    u64 frameNumber = 1;      // the frame being recorded (or about to be)
    u64 completedFrame = 0;   // every frame up to this one has finished on the GPU
    bool frameActive = false;
    CommandList frameCommandList;
    CommandList::Impl frameCommandListImpl;

    // Destruction waits until the GPU finished the frame that might still use the resource.
    struct PendingDelete {
        u64 frame;
        std::function<void()> destroy;
    };
    std::vector<PendingDelete> pendingDeletes;

    VkCommandPool immediatePool = VK_NULL_HANDLE;
    VkFence immediateFence = VK_NULL_HANDLE;

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
    void createBindless();
    void createFrames();
    bool createSwapchain(u32 width, u32 height);
    void destroySwapchainObjects();
    bool recreateSwapchain();

    void deferDelete(std::function<void()> fn) { pendingDeletes.push_back({frameNumber, std::move(fn)}); }
    void runPendingDeletes(bool all);
    void destroyTextureNow(VulkanTexture& texture);
    void setDebugName(VkObjectType type, u64 object, const char* name);

    VulkanBuffer& buffer(BufferHandle handle)
    {
        VulkanBuffer* b = buffers.get(handle);
        EMBER_VERIFY(b != nullptr);
        return *b;
    }
    VulkanTexture& texture(TextureHandle handle)
    {
        VulkanTexture* t = textures.get(handle);
        EMBER_VERIFY(t != nullptr);
        return *t;
    }
};

} // namespace ember::rhi
