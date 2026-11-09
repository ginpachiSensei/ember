#include "ember/core/Serialize.h"

#include <doctest/doctest.h>

namespace test {
enum class Mode : ember::i32 { Off, Slow, Fast };
struct Inner {
    ember::f32 weight = 1.0f;
};
struct Thing {
    std::string name = "thing";
    ember::vec3 position{0.0f};
    ember::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool enabled = true;
    ember::u32 count = 3;
    Mode mode = Mode::Slow;
    ember::Guid id;
    Inner inner;
};
} // namespace test

EMBER_REFLECT_ENUM(test::Mode, "Off", "Slow", "Fast")
EMBER_REFLECT_DECLARE(test::Inner)
EMBER_REFLECT_DECLARE(test::Thing)

EMBER_REFLECT_BEGIN(test::Inner)
    EMBER_FIELD(weight)
EMBER_REFLECT_END(test::Inner)

EMBER_REFLECT_BEGIN(test::Thing)
    EMBER_FIELD(name)
    EMBER_FIELD(position)
    EMBER_FIELD(rotation)
    EMBER_FIELD(enabled)
    EMBER_FIELD(count, {.min = 0.0f, .max = 10.0f})
    EMBER_FIELD(mode)
    EMBER_FIELD(id)
    EMBER_FIELD(inner)
EMBER_REFLECT_END(test::Thing)

using namespace ember;

TEST_CASE("reflection lists fields in order with kinds and hints")
{
    const TypeInfo& type = typeOf<test::Thing>();
    REQUIRE(type.fields.size() == 8);
    CHECK(std::string(type.fields[0].name) == "name");
    CHECK(type.fields[1].kind == FieldKind::Vec3);
    CHECK(type.fields[4].hints.max == 10.0f);
    CHECK(type.fields[5].kind == FieldKind::Enum);
    CHECK(type.fields[7].kind == FieldKind::Struct);
}

TEST_CASE("round-trips through JSON and tolerates missing fields")
{
    test::Thing a;
    a.name = "lamp";
    a.position = vec3(1, 2, 3);
    a.rotation = glm::angleAxis(0.5f, vec3(0, 1, 0));
    a.enabled = false;
    a.count = 9;
    a.mode = test::Mode::Fast;
    a.id = Guid::generate();
    a.inner.weight = 2.5f;

    const Json json = toJson(a);
    CHECK(json["mode"] == "Fast");
    const test::Thing b = fromJson<test::Thing>(json);
    CHECK(b.name == "lamp");
    CHECK(b.position == a.position);
    CHECK(std::abs(glm::dot(b.rotation, a.rotation)) == doctest::Approx(1.0f));
    CHECK(b.enabled == false);
    CHECK(b.count == 9);
    CHECK(b.mode == test::Mode::Fast);
    CHECK(b.id == a.id);
    CHECK(b.inner.weight == 2.5f);

    const test::Thing c = fromJson<test::Thing>(Json{{"name", "partial"}});
    CHECK(c.name == "partial");
    CHECK(c.count == 3); // default kept
}

TEST_CASE("guids parse what they print")
{
    const Guid g = Guid::generate();
    CHECK(Guid::parse(g.toString()) == g);
    CHECK(!Guid::parse("not a guid"));
}
