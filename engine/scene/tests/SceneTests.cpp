#include "ember/core/Jobs.h"
#include "ember/scene/Scene.h"

#include <doctest/doctest.h>

using namespace ember;
using namespace ember::scene;

TEST_CASE("world transforms follow the hierarchy")
{
    Scene scene;
    const Entity parent = scene.create("parent");
    const Entity child = scene.create("child", parent);
    scene.registry().get<Transform>(parent).position = vec3(10, 0, 0);
    scene.registry().get<Transform>(child).position = vec3(0, 1, 0);
    scene.updateTransforms();
    const vec3 world = vec3(scene.registry().get<WorldTransform>(child).matrix[3]);
    CHECK(world == vec3(10, 1, 0));
}

TEST_CASE("reparenting refuses cycles and destroy removes children")
{
    Scene scene;
    const Entity a = scene.create("a");
    const Entity b = scene.create("b", a);
    const Entity c = scene.create("c", b);
    CHECK_FALSE(scene.setParent(a, c));
    CHECK(scene.setParent(c, kNullEntity));
    CHECK(scene.parent(c) == kNullEntity);
    scene.setParent(c, b);
    scene.destroy(a);
    CHECK_FALSE(scene.valid(b));
    CHECK_FALSE(scene.valid(c));
}

TEST_CASE("scenes round-trip through JSON")
{
    Scene scene;
    const Entity root = scene.create("root");
    const Entity child = scene.create("child", root);
    scene.registry().get<Transform>(child).position = vec3(1, 2, 3);
    scene.registry().emplace<Spin>(child, Spin{vec3(1, 0, 0), 2.0f});
    const Guid childId = scene.registry().get<IdComponent>(child).id;

    Scene loaded;
    REQUIRE(loaded.load(scene.save()));
    const Entity e = loaded.findById(childId);
    REQUIRE(e != kNullEntity);
    CHECK(loaded.name(e) == "child");
    CHECK(loaded.name(loaded.parent(e)) == "root");
    CHECK(loaded.registry().get<Transform>(e).position == vec3(1, 2, 3));
    REQUIRE(loaded.registry().all_of<Spin>(e));
    CHECK(loaded.registry().get<Spin>(e).speed == 2.0f);
}

TEST_CASE("parallel transform update matches the serial one")
{
    Scene scene;
    for (int i = 0; i < 500; ++i) {
        const Entity root = scene.create("root");
        scene.registry().get<Transform>(root).position = vec3(static_cast<f32>(i), 0, 0);
        scene.create("child", root);
    }
    JobSystem jobs(4);
    scene.updateTransforms(&jobs);
    bool ok = true;
    for (const auto [e, h, w] : scene.registry().view<Hierarchy, WorldTransform>().each()) {
        if (h.parent != kNullEntity) {
            ok = ok && w.matrix == scene.registry().get<WorldTransform>(h.parent).matrix;
        }
    }
    CHECK(ok);
}
