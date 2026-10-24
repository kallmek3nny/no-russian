// Section 21 armour record tests: a full record decodes into ArmourDef, omitted resistances stay
// neutral, slot names round-trip, and strict record errors (unknown keys, slots, channels or socket
// slots, bad numbers, repeated sockets) name the source and record for the content validator.
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "TestHarness.h"
#include "tcombat/ArmourRecord.h"

using namespace lt::combat;

namespace {

const std::string kRecord =
    R"({"id":"armor.test_greaves","display_name":"Test Greaves","slot":"legs","weight":4.5,"poise":12,)"
    R"("durability":150,"resistances":{"frost":0.75,"void":1.5},"sockets":["rune","grip"],)"
    R"("aspect_tags":["form"],"lore_key":"item.test_greaves.lore"})";

// kRecord with the first `from` replaced by `to`.
std::string with(std::string_view from, std::string_view to, std::string s = kRecord) {
    const std::size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

ArmourLoadResult load(const std::string& records) { return parseArmourRecords("[" + records + "]", "t.json"); }

bool failsWith(const std::string& records, std::string_view message) {
    const ArmourLoadResult r = load(records);
    return !r.ok() && r.armour.empty() && r.error.find(message) != std::string::npos;
}

float resist(const ArmourDef& d, Channel c) { return d.resistances[static_cast<std::size_t>(c)]; }

}  // namespace

int main() {
    TEST_SECTION("valid record");
    {
        const ArmourLoadResult r = load(kRecord);
        CHECK(r.ok());
        CHECK_EQ(r.armour.size(), 1u);
        if (r.ok() && r.armour.size() == 1) {
            const ArmourDef& d = r.armour[0];
            CHECK_EQ(d.id, std::string("armor.test_greaves"));
            CHECK_EQ(d.displayName, std::string("Test Greaves"));
            CHECK(d.slot == ArmourSlot::Legs);
            CHECK_EQ(d.weight, 4.5f);
            CHECK_EQ(d.poise, 12.0f);
            CHECK_EQ(d.durability, 150u);
            CHECK_EQ(resist(d, Channel::Frost), 0.75f);
            CHECK_EQ(resist(d, Channel::Void), 1.5f);
            CHECK_EQ(resist(d, Channel::Physical), 1.0f);
            CHECK(d.sockets == std::vector<std::string>({"rune", "grip"}));
            CHECK(d.aspectTags == std::vector<std::string>({"form"}));
            CHECK_EQ(d.loreKey, std::string("item.test_greaves.lore"));
        }
        CHECK(load("").ok());
    }

    TEST_SECTION("slot names");
    for (std::size_t i = 0; i < kArmourSlotCount; ++i) {
        const ArmourSlot slot = static_cast<ArmourSlot>(i);
        CHECK(armourSlotFromName(armourSlotName(slot)) == slot);
    }
    CHECK(!armourSlotFromName("feet").has_value());
    CHECK(!armourSlotFromName("").has_value());

    TEST_SECTION("record errors");
    CHECK(failsWith(with(R"(,"lore_key")", R"(,"set_bonus":"x","lore_key")"),
                    "t.json: record 1 (armor.test_greaves): unknown key 'set_bonus'"));
    CHECK(failsWith(with(R"("id":"armor.test_greaves",)", ""), "t.json: record 1: 'id' must be a non-empty string"));
    CHECK(failsWith(with(R"("display_name":"Test Greaves")", R"("display_name":"")"), "'display_name' must be a non-empty string"));
    CHECK(failsWith(with(R"("slot":"legs")", R"("slot":"feet")"), "unknown armour slot 'feet'"));
    CHECK(failsWith(with(R"("weight":4.5)", R"("weight":-1)"), "'weight' must be a non-negative number"));
    CHECK(failsWith(with(R"("poise":12)", R"("poise":"high")"), "'poise' must be a non-negative number"));
    CHECK(failsWith(with(R"("durability":150)", R"("durability":2.5)"), "'durability' must be an integer from 0 to 100000"));
    CHECK(failsWith(with(R"("durability":150)", R"("durability":-1)"), "'durability' must be an integer"));
    CHECK(failsWith(with(R"("durability":150)", R"("durability":100001)"), "'durability' must be an integer"));
    CHECK(failsWith(with(R"("frost":0.75)", R"("fire":0.75)"), "unknown resistance channel 'fire'"));
    CHECK(failsWith(with(R"("void":1.5)", R"("void":-1)"), "resistance 'void' must be a non-negative number"));
    CHECK(failsWith(with(R"({"frost":0.75,"void":1.5})", "[]"), "'resistances' must be an object of channel multipliers"));
    CHECK(failsWith(with(R"(["rune","grip"])", R"(["rune","pocket"])"), "unknown socket slot 'pocket'"));
    CHECK(failsWith(with(R"(["rune","grip"])", R"(["rune","rune"])"), "duplicate socket slot 'rune'"));
    CHECK(failsWith(with(R"(["rune","grip"])", R"(["rune","grip","core","optic","lens"])"), "'sockets' allows at most 4 slots"));
    CHECK(failsWith(with(R"(["form"])", R"(["form",3])"), "'aspect_tags' must only hold non-empty strings"));
    CHECK(failsWith(with(R"(,"lore_key":"item.test_greaves.lore")", ""), "'lore_key' must be a non-empty string"));
    CHECK(failsWith("42", "t.json: record 1: record must be an object, not number"));

    return lt::test::finish("armour_record");
}
