// Section 21 enemy record tests: shipped common families decode to the built-in EnemyDefs, optional keys
// keep the EnemyDef defaults, and strict record errors (unknown keys, roles, resistance channels, bad
// numbers, empty or repeated lists, duplicate ids) name the source and record for the content validator.
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "TestHarness.h"
#include "tcombat/Enemy.h"
#include "tcombat/EnemyRecord.h"

using namespace lt::combat;

namespace {

const std::string kCore =
    R"("id":"enemy.test_hound","role":"ambusher","perception":18,"posture":35,)"
    R"("resistances":{"frost":0.5,"ash":1.25},"attack_set":"hound_v1","drops":["aspect.motion"],)"
    R"("biomes":["frost_crown","salt_dune"],"event_mutations":["champion"])";
const std::string kOptional = R"(,"health":90,"neutral_until_provoked":true)";
const std::string kRecord = "{" + kCore + kOptional + "}";

// kRecord with the first `from` replaced by `to`.
std::string with(std::string_view from, std::string_view to, std::string s = kRecord) {
    const std::size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

EnemyLoadResult load(const std::string& records) { return parseEnemyRecords("[" + records + "]", "t.json"); }

bool failsWith(const std::string& records, std::string_view message) {
    const EnemyLoadResult r = load(records);
    return !r.ok() && r.enemies.empty() && r.error.find(message) != std::string::npos;
}

bool sameEnemy(const EnemyDef& a, const EnemyDef& b) {
    return a.id == b.id && a.role == b.role && a.perception == b.perception && a.posture == b.posture &&
           a.health == b.health && a.resistances == b.resistances && a.attackSet == b.attackSet &&
           a.drops == b.drops && a.biomes == b.biomes && a.eventMutations == b.eventMutations &&
           a.neutralUntilProvoked == b.neutralUntilProvoked;
}

}  // namespace

int main() {
    TEST_SECTION("valid record");
    {
        const EnemyLoadResult r = load(kRecord);
        CHECK(r.ok());
        CHECK_EQ(r.enemies.size(), 1u);
        EnemyDef expected = makeEnemy("enemy.test_hound", EnemyRole::Ambusher, 18, 35, 90, "hound_v1",
                                      {"aspect.motion"}, {"frost_crown", "salt_dune"}, {"champion"});
        setResist(expected, Channel::Frost, 0.5f);
        setResist(expected, Channel::Ash, 1.25f);
        expected.neutralUntilProvoked = true;
        CHECK(r.ok() && sameEnemy(r.enemies[0], expected));
        CHECK(r.ok() && r.enemies[0].resistances[static_cast<std::size_t>(Channel::Physical)] == 1.0f);
        CHECK(r.ok() && !spawnState(r.enemies[0]).provoked);
        CHECK(load("").ok());
    }

    TEST_SECTION("optional keys and neutral resistances");
    {
        const EnemyLoadResult r = load("{" + kCore + "}");
        CHECK(r.ok());
        CHECK(r.ok() && r.enemies[0].health == EnemyDef{}.health);
        CHECK(r.ok() && !r.enemies[0].neutralUntilProvoked && spawnState(r.enemies[0]).provoked);
        const EnemyLoadResult bare = load(with(R"("resistances":{"frost":0.5,"ash":1.25})", R"("resistances":{})"));
        CHECK(bare.ok() && bare.enemies[0].resistances == neutralResist());
    }

    TEST_SECTION("role names");
    for (std::size_t i = 0; i < kEnemyRoleCount; ++i) {
        const EnemyRole role = static_cast<EnemyRole>(i);
        CHECK(enemyRoleFromName(enemyRoleName(role)) == role);
    }
    CHECK(!enemyRoleFromName("boss").has_value());

    TEST_SECTION("record errors");
    CHECK(failsWith("{" + kCore + R"(,"loot_table":"x"})", "t.json: record 1 (enemy.test_hound): unknown key 'loot_table'"));
    CHECK(failsWith(with(R"("id":"enemy.test_hound",)", ""), "t.json: record 1: 'id' must be a non-empty string"));
    CHECK(failsWith(with(R"("role":"ambusher")", R"("role":"boss")"), "unknown role 'boss'"));
    CHECK(failsWith(with(R"("perception":18)", R"("perception":-2)"), "'perception' must be a non-negative number"));
    CHECK(failsWith(with(R"("posture":35)", R"("posture":"high")"), "'posture' must be a non-negative number"));
    CHECK(failsWith(with(R"("health":90)", R"("health":0)"), "'health' must be a positive number"));
    CHECK(failsWith(with(R"("frost":0.5)", R"("fire":0.5)"), "unknown resistance channel 'fire'"));
    CHECK(failsWith(with(R"("ash":1.25)", R"("ash":-1)"), "resistance 'ash' must be a non-negative number"));
    CHECK(failsWith(with(R"({"frost":0.5,"ash":1.25})", "[]"), "'resistances' must be an object"));
    CHECK(failsWith(with(R"("attack_set":"hound_v1",)", ""), "'attack_set' must be a non-empty string"));
    CHECK(failsWith(with(R"(["aspect.motion"])", R"(["aspect.motion",3])"), "'drops' must only hold non-empty strings"));
    CHECK(failsWith(with(R"(["frost_crown","salt_dune"])", "[]"), "'biomes' must name at least one biome"));
    CHECK(failsWith(with(R"("frost_crown","salt_dune")", R"("salt_dune","salt_dune")"), "duplicate 'biomes' entry 'salt_dune'"));
    CHECK(failsWith(with(R"(["champion"])", R"(["champion","champion"])"), "duplicate 'event_mutations' entry 'champion'"));
    CHECK(failsWith(with(R"(provoked":true)", R"(provoked":"yes")"), "'neutral_until_provoked' must be true or false"));
    CHECK(failsWith("42", "t.json: record 1: record must be an object, not number"));
    CHECK(failsWith(kRecord + "," + kRecord,
                    "t.json: record 2 (enemy.test_hound): duplicate id 'enemy.test_hound' (first defined in record 1)"));
    CHECK(failsWith(kRecord + "," + with(R"("posture":35)", R"("posture":"x")"), "t.json: record 2 (enemy.test_hound)"));

    TEST_SECTION("document errors");
    {
        const EnemyLoadResult top = parseEnemyRecords(kRecord, "t.json");
        CHECK(!top.ok() && top.enemies.empty());
        CHECK(top.error.rfind("t.json: top level must be an array of enemy records", 0) == 0);
        const EnemyLoadResult syntax = parseEnemyRecords("[" + kRecord, "t.json");
        CHECK(!syntax.ok() && syntax.enemies.empty());
        CHECK(syntax.error.rfind("t.json:1:", 0) == 0);
        const EnemyLoadResult missing = loadEnemyFile("definitely/not/here.json");
        CHECK(!missing.ok() && missing.enemies.empty());
        CHECK(missing.error.rfind("definitely/not/here.json: ", 0) == 0);
    }

#ifdef LT_DATA_DIR
    TEST_SECTION("shipped common families");
    {
        const EnemyLoadResult r = loadEnemyFile(std::string(LT_DATA_DIR) + "/enemies/common_families.json");
        CHECK_EQ(r.error, std::string());
        const auto builtIn = commonEnemyFamilies();
        CHECK_EQ(r.enemies.size(), builtIn.size());
        for (std::size_t i = 0; i < r.enemies.size() && i < builtIn.size(); ++i)
            CHECK(sameEnemy(r.enemies[i], builtIn[i]));
    }
#endif

    return lt::test::finish("enemy_record");
}
