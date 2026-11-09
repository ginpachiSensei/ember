#include "ember/core/Memory.h"

#include <doctest/doctest.h>

#include <cstdint>

using namespace ember;

TEST_CASE("tracked allocations are counted per tag and aligned")
{
    const memory::TagStats before = memory::stats(memory::Tag::Scene);
    void* p = memory::allocate(100, 64, memory::Tag::Scene);
    CHECK(reinterpret_cast<std::uintptr_t>(p) % 64 == 0);
    CHECK(memory::stats(memory::Tag::Scene).bytes == before.bytes + 100);
    CHECK(memory::stats(memory::Tag::Scene).allocations == before.allocations + 1);
    memory::deallocate(p);
    CHECK(memory::stats(memory::Tag::Scene).bytes == before.bytes);
}

TEST_CASE("linear allocator bumps, aligns, rewinds and runs out")
{
    LinearAllocator arena(256);
    void* a = arena.allocate(10, 1);
    void* b = arena.allocate(16, 16);
    CHECK(a != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(b) % 16 == 0);
    const auto marker = arena.marker();
    arena.allocate(32);
    arena.rewind(marker);
    CHECK(arena.used() == marker);
    CHECK(arena.allocate(1000) == nullptr); // too big: no crash, just nullptr
    arena.reset();
    CHECK(arena.used() == 0);
}

TEST_CASE("frame allocator keeps the previous frame's data alive")
{
    FrameAllocator frames(1024);
    frames.beginFrame();
    int* first = frames.allocateArray<int>(4);
    first[0] = 42;
    frames.beginFrame(); // next frame: a different buffer
    int* second = frames.allocateArray<int>(4);
    second[0] = 7;
    CHECK(first[0] == 42);
    frames.beginFrame(); // the first buffer is reused now
    CHECK(frames.used() == 0);
}

TEST_CASE("pool allocator reuses freed slots")
{
    struct Thing {
        int value;
        explicit Thing(int v)
            : value(v)
        {
        }
    };
    PoolAllocator<Thing> pool(4);
    Thing* a = pool.create(1);
    Thing* b = pool.create(2);
    CHECK(pool.liveCount() == 2);
    pool.destroy(a);
    Thing* c = pool.create(3);
    CHECK(c == a); // the freed slot came back
    CHECK(b->value == 2);
    for (int i = 0; i < 10; ++i) {
        pool.destroy(pool.create(i)); // growth past one chunk works
    }
    Thing* extra[6];
    for (Thing*& t : extra) {
        t = pool.create(0);
    }
    CHECK(pool.capacity() >= 8);
    for (Thing* t : extra) {
        pool.destroy(t);
    }
    pool.destroy(b);
    pool.destroy(c);
    CHECK(pool.liveCount() == 0);
}
