#include "ember/core/Memory.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace ember::memory {

namespace {

struct AtomicStats {
    std::atomic<i64> bytes{0};
    std::atomic<i64> allocations{0};
    std::atomic<i64> peakBytes{0};
};

AtomicStats g_stats[static_cast<usize>(Tag::Count)];

// Stored right before every block we hand out, so deallocate() knows what to give back.
struct Header {
    void* raw;  // what malloc returned
    usize size; // what the caller asked for
    Tag tag;
};

} // namespace

const char* tagName(Tag tag)
{
    switch (tag) {
    case Tag::General: return "General";
    case Tag::Rhi: return "RHI";
    case Tag::Render: return "Render";
    case Tag::Asset: return "Asset";
    case Tag::Scene: return "Scene";
    case Tag::Jobs: return "Jobs";
    case Tag::Frame: return "Frame";
    case Tag::Count: break;
    }
    return "?";
}

TagStats stats(Tag tag)
{
    const AtomicStats& s = g_stats[static_cast<usize>(tag)];
    return {s.bytes.load(std::memory_order_relaxed), s.allocations.load(std::memory_order_relaxed),
            s.peakBytes.load(std::memory_order_relaxed)};
}

void* allocate(usize size, usize alignment, Tag tag)
{
    EMBER_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0);
    alignment = alignment < alignof(Header) ? alignof(Header) : alignment;

    // [raw ... padding ... Header][user block, aligned]
    void* raw = std::malloc(size + sizeof(Header) + alignment);
    EMBER_VERIFY(raw != nullptr);
    const auto start = reinterpret_cast<uintptr_t>(raw) + sizeof(Header);
    const uintptr_t aligned = (start + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
    auto* header = reinterpret_cast<Header*>(aligned) - 1;
    header->raw = raw;
    header->size = size;
    header->tag = tag;

    AtomicStats& s = g_stats[static_cast<usize>(tag)];
    const i64 now = s.bytes.fetch_add(static_cast<i64>(size), std::memory_order_relaxed) + static_cast<i64>(size);
    s.allocations.fetch_add(1, std::memory_order_relaxed);
    i64 peak = s.peakBytes.load(std::memory_order_relaxed);
    while (now > peak && !s.peakBytes.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {
    }
    return reinterpret_cast<void*>(aligned);
}

void deallocate(void* pointer)
{
    if (!pointer) {
        return;
    }
    const Header* header = static_cast<Header*>(pointer) - 1;
    AtomicStats& s = g_stats[static_cast<usize>(header->tag)];
    s.bytes.fetch_sub(static_cast<i64>(header->size), std::memory_order_relaxed);
    s.allocations.fetch_sub(1, std::memory_order_relaxed);
    std::free(header->raw);
}

} // namespace ember::memory

namespace ember {

LinearAllocator::LinearAllocator(usize capacity, memory::Tag tag)
    : m_buffer(static_cast<std::byte*>(memory::allocate(capacity, 64, tag)))
    , m_capacity(capacity)
{
}

LinearAllocator::~LinearAllocator()
{
    memory::deallocate(m_buffer);
}

void* LinearAllocator::allocate(usize size, usize alignment)
{
    const usize aligned = (m_offset + alignment - 1) & ~(alignment - 1);
    if (aligned + size > m_capacity) {
        return nullptr;
    }
    m_offset = aligned + size;
    return m_buffer + aligned;
}

FrameAllocator::FrameAllocator(usize capacityPerFrame)
    : m_buffers{LinearAllocator(capacityPerFrame, memory::Tag::Frame), LinearAllocator(capacityPerFrame, memory::Tag::Frame)}
{
}

void FrameAllocator::beginFrame()
{
    m_peak = std::max(m_peak, current().used());
    m_index ^= 1;
    current().reset();
}

void* FrameAllocator::allocate(usize size, usize alignment)
{
    void* memory = current().allocate(size, alignment);
    EMBER_VERIFY(memory != nullptr); // frame budget exceeded: raise the capacity
    return memory;
}

} // namespace ember
