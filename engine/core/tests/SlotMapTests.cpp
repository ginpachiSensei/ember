#include "ember/core/SlotMap.h"

#include <doctest/doctest.h>

#include <string>

using namespace ember;

TEST_CASE("default handle is invalid")
{
    SlotMap<int> map;
    Handle<int> handle;
    CHECK_FALSE(handle.isValid());
    CHECK(map.get(handle) == nullptr);
}

TEST_CASE("emplace and get")
{
    SlotMap<std::string> map;
    auto a = map.emplace("alpha");
    auto b = map.emplace("beta");

    CHECK(a.isValid());
    CHECK(map.size() == 2);
    REQUIRE(map.get(a) != nullptr);
    CHECK(*map.get(a) == "alpha");
    CHECK(*map.get(b) == "beta");
}

TEST_CASE("removed handle becomes stale even after the slot is reused")
{
    SlotMap<int> map;
    auto first = map.emplace(1);
    CHECK(map.remove(first));
    CHECK_FALSE(map.contains(first));
    CHECK_FALSE(map.remove(first));

    auto second = map.emplace(2);
    CHECK(second.index == first.index); // slot reused
    CHECK(second.generation != first.generation);
    CHECK(map.get(first) == nullptr);
    CHECK(*map.get(second) == 2);
    CHECK(map.size() == 1);
}
