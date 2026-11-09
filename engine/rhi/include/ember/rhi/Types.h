#pragma once

// Plain descriptions of GPU resources. No Vulkan types, so the rest of the engine stays API-agnostic.

#include "ember/core/SlotMap.h"
#include "ember/core/Types.h"

#include <span>

namespace ember::rhi {

inline constexpr u32 kFramesInFlight = 2;
inline constexpr u32 kMaxColorAttachments = 4;
inline constexpr u32 kMaxPushConstantSize = 128; // guaranteed minimum on every Vulkan device

struct BufferTag;
struct TextureTag;
struct SamplerTag;
struct PipelineTag;
using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using SamplerHandle = Handle<SamplerTag>;
using PipelineHandle = Handle<PipelineTag>;

// Lets enum classes be used as bit flags: `Usage::A | Usage::B`, `has(flags, Usage::A)`.
#define EMBER_ENUM_FLAGS(E)                                                                                     \
    constexpr E operator|(E a, E b) { return static_cast<E>(static_cast<u32>(a) | static_cast<u32>(b)); }      \
    constexpr E operator&(E a, E b) { return static_cast<E>(static_cast<u32>(a) & static_cast<u32>(b)); }      \
    constexpr bool has(E flags, E bit) { return (static_cast<u32>(flags) & static_cast<u32>(bit)) != 0; }

enum class Format : u8 {
    Unknown,
    RGBA8Unorm,
    RGBA8Srgb,
    BGRA8Unorm,
    BGRA8Srgb,
    R8Unorm,
    RG8Unorm,
    R16Float,
    RG16Float,
    RGBA16Float,
    R32Float,
    RG32Float,
    RGBA32Float,
    R11G11B10Float,
    D32Float,
};

bool isDepthFormat(Format format);
u32 bytesPerPixel(Format format);

// ---------------------------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------------------------

enum class BufferUsage : u32 {
    None = 0,
    Index = 1 << 0,
    Storage = 1 << 1,  // read/written by shaders through a device address (see Device::deviceAddress)
    Indirect = 1 << 2, // arguments for indirect draws/dispatches
    TransferSrc = 1 << 3,
    TransferDst = 1 << 4,
};
EMBER_ENUM_FLAGS(BufferUsage)

enum class MemoryType : u8 {
    GpuOnly,  // fastest for the GPU; filled with an upload or by shaders
    Upload,   // CPU writes through mappedData(), the GPU reads (per-frame constants, staging)
    Readback, // GPU writes, the CPU reads
};

struct BufferDesc {
    u64 size = 0;
    BufferUsage usage = BufferUsage::Storage;
    MemoryType memory = MemoryType::GpuOnly;
    const char* debugName = nullptr;
};

// ---------------------------------------------------------------------------------------------
// Textures and samplers
// ---------------------------------------------------------------------------------------------

enum class TextureUsage : u32 {
    None = 0,
    Sampled = 1 << 0,         // read in shaders through the bindless table
    Storage = 1 << 1,         // written by compute shaders (one bindless index per mip)
    ColorAttachment = 1 << 2, // render target
    DepthAttachment = 1 << 3,
    TransferSrc = 1 << 4,
    TransferDst = 1 << 5,
};
EMBER_ENUM_FLAGS(TextureUsage)

enum class TextureType : u8 { Texture2D, TextureCube };

struct TextureDesc {
    u32 width = 1;
    u32 height = 1;
    u32 mipLevels = 1;
    TextureType type = TextureType::Texture2D;
    Format format = Format::RGBA8Unorm;
    TextureUsage usage = TextureUsage::Sampled;
    const char* debugName = nullptr;
};

inline u32 mipCount(u32 width, u32 height)
{
    u32 levels = 1;
    while ((width | height) >> levels) {
        ++levels;
    }
    return levels;
}

enum class Filter : u8 { Nearest, Linear };
enum class AddressMode : u8 { Repeat, ClampToEdge, MirroredRepeat };
enum class CompareOp : u8 { Never, Less, LessEqual, Greater, GreaterEqual, Equal, Always };

struct SamplerDesc {
    Filter filter = Filter::Linear; // min, mag and mip filter
    AddressMode address = AddressMode::Repeat;
    f32 maxAnisotropy = 0.0f; // 0 = off
    bool compare = false;     // depth comparison sampler (hardware PCF for shadow maps)
    CompareOp compareOp = CompareOp::Less;
};

// What a texture is used for next. Barriers move a texture between states;
// the RHI tracks the current state and works out the layouts and sync stages.
enum class ResourceState : u8 {
    Undefined,
    ShaderRead,      // sampled in any shader stage
    ShaderReadWrite, // storage image in compute
    ColorAttachment,
    DepthAttachment,
    TransferSrc,
    TransferDst,
    Present,
};

// ---------------------------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------------------------

// A compiled SPIR-V module. One module may hold several entry points (vertex + fragment).
struct ShaderModuleDesc {
    std::span<const u32> spirv;
    const char* entryPoint = "main";
};

enum class CullMode : u8 { None, Back, Front };
enum class Topology : u8 { TriangleList, LineList };
enum class BlendMode : u8 { Opaque, Alpha, Additive };

struct GraphicsPipelineDesc {
    ShaderModuleDesc vertex;
    ShaderModuleDesc fragment; // empty spirv = depth-only pipeline
    Format colorFormats[kMaxColorAttachments] = {};
    u32 colorCount = 0;
    Format depthFormat = Format::Unknown;
    Topology topology = Topology::TriangleList;
    CullMode cull = CullMode::Back;
    BlendMode blend = BlendMode::Opaque;
    bool depthTest = false;
    bool depthWrite = false;
    CompareOp depthCompare = CompareOp::Greater; // reverse-Z
    bool depthClamp = false;                     // keeps shadow casters behind the light's near plane
    f32 depthBiasConstant = 0.0f;
    f32 depthBiasSlope = 0.0f;
    const char* debugName = nullptr;
};

struct ComputePipelineDesc {
    ShaderModuleDesc shader;
    const char* debugName = nullptr;
};

// ---------------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------------

enum class LoadOp : u8 { Load, Clear, DontCare };

struct ColorAttachment {
    TextureHandle texture;
    LoadOp load = LoadOp::Clear;
    f32 clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthAttachment {
    TextureHandle texture;
    LoadOp load = LoadOp::Clear;
    f32 clear = 0.0f; // reverse-Z: 0 is the far plane
};

struct RenderingDesc {
    ColorAttachment colors[kMaxColorAttachments];
    u32 colorCount = 0;
    DepthAttachment depth; // invalid texture = no depth
};

} // namespace ember::rhi
