#pragma once

// Explicit memory: tracked heap allocations plus the specialized allocators engines use instead of
// new/delete on hot paths.
//
//   memory::allocate     general heap, counted per tag (shows where memory goes in the debug UI)
//   LinearAllocator      bump pointer; free everything at once with reset(). Fastest possible.
//   FrameAllocator       two linear allocators, swapped each frame: scratch data that lives one frame
//                        (or two, so the next frame can still read what the previous one produced)
//   PoolAllocator<T>     fixed-size slots with a free list: many objects of one type, no fragmentation

#include "ember/core/Assert.h"
#include "ember/core/Types.h"

#include <cstddef>
#include <new>
#include <utility>
#include <vector>

namespace ember::memory {

enum class Tag : u8 { General, Rhi, Render, Asset, Scene, Jobs, Frame, Count };
const char* tagName(Tag tag);

struct TagStats {
    i64 bytes = 0;       // currently allocated
    i64 allocations = 0; // currently live
    i64 peakBytes = 0;
};
TagStats stats(Tag tag);

// Thread-safe. `alignment` must be a power of two.
void* allocate(usize size, usize alignment = alignof(std::max_align_t), Tag tag = Tag::General);
void deallocate(void* pointer); // nullptr is fine

template <typename T, typename... Args>
T* create(Tag tag, Args&&... args)
{
    void* memory = allocate(sizeof(T), alignof(T), tag);
    return new (memory) T(std::forward<Args>(args)...);
}

template <typename T>
void destroy(T* object)
{
    if (object) {
        object->~T();
        deallocate(object);
    }
}

// std-compatible allocator, so containers can be counted under a tag: std::vector<int, TrackedAllocator<int>>.
template <typename T, Tag kTag = Tag::General>
struct TrackedAllocator {
    using value_type = T;
    TrackedAllocator() = default;
    template <typename U>
    TrackedAllocator(const TrackedAllocator<U, kTag>&) noexcept
    {
    }
    template <typename U>
    struct rebind {
        using other = TrackedAllocator<U, kTag>;
    };
    T* allocate(usize n) { return static_cast<T*>(memory::allocate(n * sizeof(T), alignof(T), kTag)); }
    void deallocate(T* p, usize) noexcept { memory::deallocate(p); }
    friend bool operator==(const TrackedAllocator&, const TrackedAllocator&) { return true; }
};

} // namespace ember::memory

namespace ember {

// Hands out memory by bumping a pointer through one block. Individual frees aren't possible;
// reset() (or rewind() to a marker) frees everything allocated after that point at once.
// Destructors are NOT run: use it for plain data.
class LinearAllocator {
public:
    explicit LinearAllocator(usize capacity, memory::Tag tag = memory::Tag::General);
    ~LinearAllocator();
    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    // Returns nullptr when the block is full.
    void* allocate(usize size, usize alignment = alignof(std::max_align_t));

    template <typename T>
    T* allocateArray(usize count)
    {
        void* memory = allocate(sizeof(T) * count, alignof(T));
        return memory ? new (memory) T[count]() : nullptr;
    }

    using Marker = usize;
    Marker marker() const { return m_offset; }
    void rewind(Marker marker)
    {
        EMBER_ASSERT(marker <= m_offset);
        m_offset = marker;
    }
    void reset() { m_offset = 0; }

    usize used() const { return m_offset; }
    usize capacity() const { return m_capacity; }

private:
    std::byte* m_buffer = nullptr;
    usize m_capacity = 0;
    usize m_offset = 0;
};

// Per-frame scratch memory. Memory from frame N stays valid during frame N+1, then gets reused.
class FrameAllocator {
public:
    explicit FrameAllocator(usize capacityPerFrame);

    void beginFrame(); // switch to the older buffer and clear it
    void* allocate(usize size, usize alignment = alignof(std::max_align_t));

    template <typename T>
    T* allocateArray(usize count)
    {
        return current().allocateArray<T>(count);
    }

    usize used() const { return m_buffers[m_index].used(); }
    usize capacity() const { return m_buffers[m_index].capacity(); }
    usize peakUsed() const { return m_peak; }

private:
    LinearAllocator& current() { return m_buffers[m_index]; }
    LinearAllocator m_buffers[2];
    u32 m_index = 0;
    usize m_peak = 0;
};

// Fixed-size slots for objects of one type, allocated in chunks. create/destroy are O(1).
// Freed slots form a linked list through the slot memory itself.
template <typename T>
class PoolAllocator {
public:
    explicit PoolAllocator(usize slotsPerChunk = 256, memory::Tag tag = memory::Tag::General)
        : m_slotsPerChunk(slotsPerChunk)
        , m_tag(tag)
    {
    }
    ~PoolAllocator()
    {
        EMBER_ASSERT(m_live == 0); // leaked objects would never get their destructors run
        for (void* chunk : m_chunks) {
            memory::deallocate(chunk);
        }
    }
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    template <typename... Args>
    T* create(Args&&... args)
    {
        if (!m_freeList) {
            grow();
        }
        Slot* slot = m_freeList;
        m_freeList = slot->next;
        ++m_live;
        return new (slot) T(std::forward<Args>(args)...);
    }

    void destroy(T* object)
    {
        if (!object) {
            return;
        }
        object->~T();
        Slot* slot = reinterpret_cast<Slot*>(object);
        slot->next = m_freeList;
        m_freeList = slot;
        --m_live;
    }

    usize liveCount() const { return m_live; }
    usize capacity() const { return m_chunks.size() * m_slotsPerChunk; }

private:
    union Slot {
        Slot* next;
        alignas(T) std::byte storage[sizeof(T)];
    };

    void grow()
    {
        auto* chunk = static_cast<Slot*>(memory::allocate(sizeof(Slot) * m_slotsPerChunk, alignof(Slot), m_tag));
        m_chunks.push_back(chunk);
        for (usize i = 0; i < m_slotsPerChunk; ++i) {
            chunk[i].next = i + 1 < m_slotsPerChunk ? &chunk[i + 1] : m_freeList;
        }
        m_freeList = chunk;
    }

    usize m_slotsPerChunk;
    memory::Tag m_tag;
    std::vector<void*> m_chunks;
    Slot* m_freeList = nullptr;
    usize m_live = 0;
};

} // namespace ember
