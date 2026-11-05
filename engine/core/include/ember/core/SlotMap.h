#pragma once

#include "ember/core/Types.h"

#include <optional>
#include <utility>
#include <vector>

namespace ember {

// A typed reference to an object stored in a SlotMap.
// The generation lets a stale handle (to an object that was removed) be detected
// instead of silently pointing at whatever reused the slot.
template <typename Tag>
struct Handle {
    u32 index = 0;
    u32 generation = 0; // 0 is never used by a live object, so a default handle is invalid

    bool isValid() const { return generation != 0; }
    friend bool operator==(Handle, Handle) = default;
};

// Stores objects in a dense array and hands out Handles to them.
// Lookups are O(1); removed slots are reused.
// Uses std containers for now; custom allocators come with the memory system.
template <typename T>
class SlotMap {
public:
    using HandleType = Handle<T>;

    template <typename... Args>
    HandleType emplace(Args&&... args)
    {
        u32 index = 0;
        if (!m_freeList.empty()) {
            index = m_freeList.back();
            m_freeList.pop_back();
        } else {
            index = static_cast<u32>(m_slots.size());
            m_slots.emplace_back();
        }
        Slot& slot = m_slots[index];
        slot.value.emplace(std::forward<Args>(args)...);
        return HandleType{index, slot.generation};
    }

    bool remove(HandleType handle)
    {
        Slot* slot = find(handle);
        if (!slot) {
            return false;
        }
        slot->value.reset();
        slot->generation = nextGeneration(slot->generation);
        m_freeList.push_back(handle.index);
        return true;
    }

    T* get(HandleType handle)
    {
        Slot* slot = find(handle);
        return slot ? &*slot->value : nullptr;
    }

    const T* get(HandleType handle) const
    {
        return const_cast<SlotMap*>(this)->get(handle);
    }

    bool contains(HandleType handle) const { return get(handle) != nullptr; }
    usize size() const { return m_slots.size() - m_freeList.size(); }

private:
    struct Slot {
        std::optional<T> value;
        u32 generation = 1;
    };

    static u32 nextGeneration(u32 generation)
    {
        ++generation;
        return generation == 0 ? 1 : generation; // skip 0 on wrap-around
    }

    Slot* find(HandleType handle)
    {
        if (handle.index >= m_slots.size()) {
            return nullptr;
        }
        Slot& slot = m_slots[handle.index];
        return (slot.value && slot.generation == handle.generation) ? &slot : nullptr;
    }

    std::vector<Slot> m_slots;
    std::vector<u32> m_freeList;
};

} // namespace ember
