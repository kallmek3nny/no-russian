// Section 21 weapon record tests: the shipped vertical-slice items decode to the built-in WeaponDefs,
// "timing" falls back to the family archetype, and strict record errors (unknown keys, families,
// channels, socket slots, bad numbers, duplicate ids) name the source and record for the validator.
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "TestHarness.h"
#include "tcombat/Arsenal.h"
#include "tcombat/WeaponRecord.h"

using namespace lt::combat;

namespace {

const std::string kCore =
    R"("id":"weapon.test_spear","display_name":"Test Spear","family":"spear","damage":{"physical":24,"frost":10},)"
    R"("posture":18,"vis_cost":6,"weight":4.5,"sockets":["grip","rune"],"moveset":"spear_01",)"
    R"("aspect_tags":["motion"],"lore_key":"item.test_spear.lore")";
const std::string kTiming = R"(,"timing":{"windup_ms":350,"active_ms":100,"recovery_ms":450,"stamina":16})";
const std::string kRecord = "{" + kCore + kTiming + "}";

// kRecord with the first `from` replaced by `to`.
std::string with(std::string_view from, std::string_view to, std::string s = kRecord) {
    const std::size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

WeaponLoadResult load(const std::string& records) { return parseWeaponRecords("[" + records + "]", "t.json"); }

bool failsWith(const std::string& records, std::string_view message) {
    const WeaponLoadResult r = load(records);
    return !r.ok() && r.weapons.empty() && r.error.find(message) != std::string::npos;
}

bool sameMoves(const WeaponDef& a, const WeaponDef& b) {
    for (std::size_t i = 0; i < kMoveKindCount; ++i) {
        const MoveTiming& x = a.moves[i];
        const MoveTiming& y = b.moves[i];
        if (x.windupMs != y.windupMs || x.activeMs != y.activeMs || x.recoveryMs != y.recoveryMs ||
            x.damageScale != y.damageScale || x.postureScale != y.postureScale || x.staminaCost != y.staminaCost)
            return false;
    }
    return true;
}

bool sameWeapon(const WeaponDef& a, const WeaponDef& b) {
    return a.id == b.id && a.displayName == b.displayName && a.family == b.family && a.damage == b.damage &&
           a.posture == b.posture && a.visCost == b.visCost && a.weight == b.weight && a.sockets == b.sockets &&
           a.moveset == b.moveset && a.aspectTags == b.aspectTags && a.loreKey == b.loreKey && sameMoves(a, b);
}

}  // namespace

int main() {
    TEST_SECTION("valid record");
    {
        const WeaponLoadResult r = load(kRecord);
        CHECK(r.ok());
        CHECK_EQ(r.weapons.size(), 1u);
        const WeaponDef& w = r.weapons[0];
        CHECK(w.family == Family::Spear);
        CHECK_EQ(w.damage[static_cast<std::size_t>(Channel::Frost)], 10.0f);
        CHECK_EQ(w.weight, 4.5f);
        CHECK_EQ(w.sockets.size(), 2u);
        CHECK_EQ(w.move(MoveKind::Light).windupMs, 350u);
        CHECK_EQ(w.move(MoveKind::Heavy).windupMs, 700u);
        CHECK(load("").ok());
    }

    TEST_SECTION("archetype timing fallback");
    {
        const WeaponLoadResult r = load(with(R"("family":"spear")", R"("family":"axe")", "{" + kCore + "}"));
        CHECK(r.ok());
        const std::optional<WeaponDef> axe = findWeapon(fullArsenal(), "weapon.ashcleaver_axe");
        CHECK(axe.has_value() && r.ok() && sameMoves(r.weapons[0], *axe));
    }

    TEST_SECTION("record errors");
    CHECK(failsWith("{" + kCore + R"(,"rarity":"epic"})", "t.json: record 1 (weapon.test_spear): unknown key 'rarity'"));
    CHECK(failsWith(with(R"("family":"spear")", R"("family":"halberd")"), "unknown family 'halberd'"));
    CHECK(failsWith(with(R"("frost":10)", R"("fire":10)"), "unknown damage channel 'fire'"));
    CHECK(failsWith(with(R"("damage":{"physical":24,"frost":10})", R"("damage":{})"), "'damage' must be a non-empty"));
    CHECK(failsWith(with(R"("posture":18)", R"("posture":-1)"), "'posture' must be a non-negative number"));
    CHECK(failsWith(with(R"("weight":4.5)", R"("weight":"heavy")"), "'weight' must be a non-negative number"));
    CHECK(failsWith(with(R"("moveset":"spear_01",)", ""), "'moveset' must be a non-empty string"));
    CHECK(failsWith(with(R"("grip","rune")", R"("grip","pocket")"), "unknown socket slot 'pocket'"));
    CHECK(failsWith(with(R"("grip","rune")", R"("rune","rune")"), "duplicate socket slot 'rune'"));
    CHECK(failsWith(with(R"("grip","rune")", R"("optic","rune","core","grip","lens")"), "at most 4 slots"));
    CHECK(failsWith(with(R"("active_ms":100)", R"("active_ms":0)"), "contact window"));
    CHECK(failsWith(with(R"("windup_ms":350)", R"("windup_ms":3.5)"), "integer from 0 to 60000 ms"));
    CHECK(failsWith(with(R"("stamina":16)", R"("stamina":16,"cooldown":2)"), "unknown timing key 'cooldown'"));
    CHECK(failsWith("42", "t.json: record 1: record must be an object, not number"));
    CHECK(failsWith(kRecord + "," + with("Test Spear", "Spear Copy"),
                    "t.json: record 2 (weapon.test_spear): duplicate id 'weapon.test_spear' (first defined in record 1)"));
    CHECK(failsWith(kRecord + "," + with(R"("posture":18)", R"("posture":"x")"), "t.json: record 2 (weapon.test_spear)"));

    TEST_SECTION("document errors");
    {
        const WeaponLoadResult top = parseWeaponRecords(kRecord, "t.json");
        CHECK(!top.ok() && top.weapons.empty());
        CHECK(top.error.rfind("t.json: top level must be an array of weapon records", 0) == 0);
        const WeaponLoadResult syntax = parseWeaponRecords("[" + kRecord, "t.json");
        CHECK(!syntax.ok() && syntax.weapons.empty());
        CHECK(syntax.error.rfind("t.json:1:", 0) == 0);
        const WeaponLoadResult missing = loadWeaponFile("definitely/not/here.json");
        CHECK(!missing.ok() && missing.weapons.empty());
        CHECK(missing.error.rfind("definitely/not/here.json: ", 0) == 0);
    }

#ifdef LT_DATA_DIR
    TEST_SECTION("shipped vertical slice items");
    {
        const WeaponLoadResult r = loadWeaponFile(std::string(LT_DATA_DIR) + "/items/vertical_slice_weapons.json");
        CHECK_EQ(r.error, std::string());
        const auto builtIn = verticalSliceWeapons();
        CHECK_EQ(r.weapons.size(), builtIn.size());
        for (std::size_t i = 0; i < r.weapons.size() && i < builtIn.size(); ++i)
            CHECK(sameWeapon(r.weapons[i], builtIn[i]));
    }
#endif

    return lt::test::finish("weapon_record");
}
