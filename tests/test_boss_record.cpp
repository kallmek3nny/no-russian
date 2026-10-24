// Sections 10/21 boss record tests: a full record decodes into BossDef with intact armour and drives
// BossState, and strict record errors (pools, phase thresholds, armour pieces, attacks, armour gates,
// the no-armour fallback) name the source and record for the content validator.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "TestHarness.h"
#include "tcombat/BossRecord.h"

using namespace lt::combat;

namespace {

const std::string kRecord =
    R"({"id":"boss.test","display_name":"Test Boss","health":1000,"posture":200,"phase_thresholds":[0.6,0.3],)"
    R"("armour":[{"id":"armour.a","integrity":100,"opens_route":"route.a"},)"
    R"({"id":"armour.b","integrity":50,"opens_route":"route.b"}],)"
    R"("attacks":[{"id":"atk.basic","windup_ms":400,"active_ms":100,"recovery_ms":500,"damage":50,"min_phase":1},)"
    R"({"id":"atk.gated","windup_ms":600,"active_ms":150,"recovery_ms":700,"damage":80,"min_phase":2,)"
    R"("requires_armour":"armour.a"},)"
    R"({"id":"atk.finale","windup_ms":1000,"active_ms":200,"recovery_ms":1200,"damage":150,"min_phase":3}]})";

// kRecord with the first `from` replaced by `to`.
std::string with(std::string_view from, std::string_view to, std::string s = kRecord) {
    const std::size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

BossLoadResult load(const std::string& records) { return parseBossRecords("[" + records + "]", "t.json"); }

bool failsWith(const std::string& records, std::string_view message) {
    const BossLoadResult r = load(records);
    return !r.ok() && r.bosses.empty() && r.error.find(message) != std::string::npos;
}

}  // namespace

int main() {
    TEST_SECTION("valid record");
    {
        const BossLoadResult r = load(kRecord);
        CHECK(r.ok());
        CHECK_EQ(r.bosses.size(), 1u);
        if (r.ok() && r.bosses.size() == 1) {
            const BossDef& d = r.bosses[0];
            CHECK_EQ(d.id, std::string("boss.test"));
            CHECK_EQ(d.displayName, std::string("Test Boss"));
            CHECK_EQ(d.maxHealth, 1000.0f);
            CHECK_EQ(d.maxPosture, 200.0f);
            CHECK(d.phaseThresholds == std::vector<float>({0.6f, 0.3f}));
            CHECK_EQ(d.armour.size(), 2u);
            CHECK_EQ(d.attacks.size(), 3u);
            if (d.armour.size() == 2 && d.attacks.size() == 3) {
                CHECK_EQ(d.armour[1].id, std::string("armour.b"));
                CHECK_EQ(d.armour[1].integrity, 50.0f);
                CHECK_EQ(d.armour[1].maxIntegrity, 50.0f);
                CHECK_EQ(d.armour[1].opensRoute, std::string("route.b"));
                const BossAttack& g = d.attacks[1];
                CHECK_EQ(g.id, std::string("atk.gated"));
                CHECK_EQ(g.windupMs, 600u);
                CHECK_EQ(g.activeMs, 150u);
                CHECK_EQ(g.recoveryMs, 700u);
                CHECK_EQ(g.damage, 80.0f);
                CHECK_EQ(g.minPhase, 2);
                CHECK_EQ(g.requiresArmour, std::string("armour.a"));
                CHECK(d.attacks[0].requiresArmour.empty());
            }
            // The decoded boss drives BossState: phase 2 adds the gated attack until its piece breaks.
            BossState boss(d, 3);
            CHECK_EQ(boss.phase(), 1);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(1));
            boss.applyHit(500.0f, 0.0f);
            CHECK_EQ(boss.phase(), 2);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(2));
            boss.applyHit(0.0f, 0.0f, "armour.a", 100.0f);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(1));
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.a"}));
        }
        CHECK(load("").ok());
    }

    TEST_SECTION("record errors");
    CHECK(failsWith(with(R"("posture":200,)", R"("posture":200,"music":"x",)"),
                    "t.json: record 1 (boss.test): unknown key 'music'"));
    CHECK(failsWith(with(R"("id":"boss.test",)", ""), "t.json: record 1: 'id' must be a non-empty string"));
    CHECK(failsWith(with(R"("display_name":"Test Boss")", R"("display_name":3)"), "'display_name' must be a non-empty string"));
    CHECK(failsWith(with(R"("health":1000)", R"("health":0)"), "'health' must be positive"));
    CHECK(failsWith(with(R"("posture":200)", R"("posture":-5)"), "'posture' must be a non-negative number"));
    CHECK(failsWith(with("[0.6,0.3]", "{}"), "'phase_thresholds' must be an array of health fractions"));
    CHECK(failsWith(with("[0.6,0.3]", "[1,0.3]"), "'phase_thresholds' entries must be numbers strictly between 0 and 1"));
    CHECK(failsWith(with("[0.6,0.3]", "[0.6,0]"), "strictly between 0 and 1"));
    CHECK(failsWith(with("[0.6,0.3]", R"([0.6,"low"])"), "strictly between 0 and 1"));
    CHECK(failsWith(with("[0.6,0.3]", "[0.3,0.6]"), "'phase_thresholds' must be strictly descending (phase 3 is unreachable)"));
    CHECK(failsWith(with("[0.6,0.3]", "[0.6,0.6]"), "(phase 3 is unreachable)"));
    CHECK(failsWith("42", "t.json: record 1: record must be an object, not number"));

    return lt::test::finish("boss_record");
}
