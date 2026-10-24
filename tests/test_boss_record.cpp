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

#ifdef LT_DATA_DIR
    TEST_SECTION("shipped bosses");
    {
        const std::filesystem::path dir = std::filesystem::path(LT_DATA_DIR) / "bosses";
        const auto offers = [](const BossState& b, std::string_view id) {
            for (const BossAttack* a : b.availableAttacks())
                if (a->id == id) return true;
            return false;
        };

        // The Frost Warden (Section 10): breaking armour pieces changes his moveset and opens arena routes.
        const BossLoadResult warden = loadBossFile(dir / "frost_warden.json");
        CHECK(warden.ok());
        CHECK_EQ(warden.bosses.size(), std::size_t(1));
        if (warden.ok() && warden.bosses.size() == 1) {
            BossState boss(warden.bosses[0], 7);
            CHECK_EQ(boss.def().id, std::string("boss.frost_warden"));
            CHECK_EQ(boss.phase(), 1);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            boss.applyHit(0.0f, 0.0f, "armour.shield_arm", 260.0f);
            CHECK(!offers(boss, "atk.rampart_slam"));
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.gatehouse"}));
            boss.applyHit(1700.0f, 0.0f);  // 700 / 2400 health: past both thresholds
            CHECK_EQ(boss.phase(), 3);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(5));
            CHECK(offers(boss, "atk.fortress_collapse") && offers(boss, "atk.frost_stomp"));
        }

        // The First Tautmaurg (Section 10): gravity, copied-technique and orrery attacks hang on breakable
        // pieces; below half health the body falls away and the research pattern attacks join.
        const BossLoadResult first = loadBossFile(dir / "first_tautmaurg.json");
        CHECK(first.ok());
        CHECK_EQ(first.bosses.size(), std::size_t(1));
        if (first.ok() && first.bosses.size() == 1) {
            BossState boss(first.bosses[0], 11);
            CHECK_EQ(boss.def().id, std::string("boss.first_tautmaurg"));
            CHECK_EQ(boss.def().displayName, std::string("The First Tautmaurg"));
            CHECK_EQ(boss.phase(), 1);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(4));
            CHECK(!offers(boss, "atk.research_pattern") && !offers(boss, "atk.proof_of_god"));
            boss.applyHit(1700.0f, 0.0f);  // 1500 / 3200 health: below the 0.5 threshold
            CHECK_EQ(boss.phase(), 2);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(6));
            boss.applyHit(0.0f, 0.0f, "armour.tautmaurg_gravity_lens", 220.0f);
            CHECK(!offers(boss, "atk.gravity_inversion"));
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.inverted_gallery"}));
            boss.applyHit(0.0f, 0.0f, "armour.tautmaurg_measuring_rod", 180.0f);
            boss.applyHit(0.0f, 0.0f, "armour.tautmaurg_orrery_ring", 260.0f);
            CHECK_EQ(boss.openRoutes().size(), std::size_t(3));
            // With every piece broken the ungated moves remain.
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            CHECK(offers(boss, "atk.measured_cut") && offers(boss, "atk.proof_of_god"));
        }

        // Veyra, Glass Saint (Section 10): her reflection moves hang on the mirror veil and glass halo, the
        // poison reed joins in phase two while the censer holds, and the drowned hymn waits for phase three.
        const BossLoadResult veyra = loadBossFile(dir / "veyra_glass_saint.json");
        CHECK(veyra.ok());
        CHECK_EQ(veyra.bosses.size(), std::size_t(1));
        if (veyra.ok() && veyra.bosses.size() == 1) {
            BossState boss(veyra.bosses[0], 13);
            CHECK_EQ(boss.def().id, std::string("boss.veyra_glass_saint"));
            CHECK_EQ(boss.def().displayName, std::string("Veyra, Glass Saint"));
            CHECK_EQ(boss.phase(), 1);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            CHECK(!offers(boss, "atk.veyra_poison_reed") && !offers(boss, "atk.veyra_shatter_chorus"));
            CHECK(!offers(boss, "atk.veyra_drowned_hymn"));
            boss.applyHit(1000.0f, 0.0f);  // 1800 / 2800 health: below the 0.66 threshold
            CHECK_EQ(boss.phase(), 2);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(5));
            CHECK(offers(boss, "atk.veyra_poison_reed") && offers(boss, "atk.veyra_shatter_chorus"));
            CHECK(!offers(boss, "atk.veyra_drowned_hymn"));
            boss.applyHit(0.0f, 0.0f, "armour.veyra_mirror_veil", 200.0f);
            CHECK(!offers(boss, "atk.veyra_mirror_step"));
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.drowned_stacks"}));
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(4));
            boss.applyHit(1000.0f, 0.0f);  // 800 / 2800 health: below the 0.33 threshold
            CHECK_EQ(boss.phase(), 3);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(5));
            CHECK(offers(boss, "atk.veyra_drowned_hymn"));
            boss.applyHit(0.0f, 0.0f, "armour.veyra_glass_halo", 240.0f);
            boss.applyHit(0.0f, 0.0f, "armour.veyra_reed_censer", 160.0f);
            CHECK_EQ(boss.openRoutes().size(), std::size_t(3));
            // With every piece broken the ungated needle, chorus and hymn remain.
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            CHECK(offers(boss, "atk.veyra_glass_needle") && offers(boss, "atk.veyra_shatter_chorus"));
            CHECK(offers(boss, "atk.veyra_drowned_hymn"));
        }

        // Khar-Duun's Furnace Mother (Section 10): the arm crush and conveyor haul hang on her two arms,
        // the molten flood joins in phase two while the boiler plate holds, and the meltdown waits for phase three.
        const BossLoadResult furnace = loadBossFile(dir / "khar_duun_furnace_mother.json");
        CHECK(furnace.ok());
        CHECK_EQ(furnace.bosses.size(), std::size_t(1));
        if (furnace.ok() && furnace.bosses.size() == 1) {
            BossState boss(furnace.bosses[0], 17);
            CHECK_EQ(boss.def().id, std::string("boss.khar_duun_furnace_mother"));
            CHECK_EQ(boss.def().displayName, std::string("Khar-Duun's Furnace Mother"));
            CHECK_EQ(boss.phase(), 1);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            CHECK(offers(boss, "atk.furnace_arm_crush") && offers(boss, "atk.furnace_conveyor_haul"));
            CHECK(!offers(boss, "atk.furnace_molten_flood") && !offers(boss, "atk.furnace_worker_swarm"));
            CHECK(!offers(boss, "atk.furnace_meltdown"));
            boss.applyHit(1200.0f, 0.0f);  // 2400 / 3600 health: below the 0.7 threshold
            CHECK_EQ(boss.phase(), 2);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(5));
            CHECK(offers(boss, "atk.furnace_molten_flood") && offers(boss, "atk.furnace_worker_swarm"));
            CHECK(!offers(boss, "atk.furnace_meltdown"));
            boss.applyHit(0.0f, 0.0f, "armour.furnace_left_arm", 280.0f);
            CHECK(!offers(boss, "atk.furnace_arm_crush"));
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.conveyor_line"}));
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(4));
            boss.applyHit(1200.0f, 0.0f);  // 1200 / 3600 health: below the 0.4 threshold
            CHECK_EQ(boss.phase(), 3);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(5));
            CHECK(offers(boss, "atk.furnace_meltdown"));
            boss.applyHit(0.0f, 0.0f, "armour.furnace_right_arm", 280.0f);
            boss.applyHit(0.0f, 0.0f, "armour.furnace_boiler_plate", 340.0f);
            CHECK_EQ(boss.openRoutes().size(), std::size_t(3));
            // With every piece broken the ungated slag spit, worker swarm and meltdown remain.
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            CHECK(offers(boss, "atk.furnace_slag_spit") && offers(boss, "atk.furnace_worker_swarm"));
            CHECK(offers(boss, "atk.furnace_meltdown"));
        }

        // The Wyvern of the Ninth Nest (Section 10): the flame sweep and cinder volley hang on her two fire
        // glands, the gale beat joins in phase two while the wing membrane holds, and the nest inferno waits
        // for phase three and still needs the right gland intact.
        const BossLoadResult wyvern = loadBossFile(dir / "wyvern_ninth_nest.json");
        CHECK(wyvern.ok());
        CHECK_EQ(wyvern.bosses.size(), std::size_t(1));
        if (wyvern.ok() && wyvern.bosses.size() == 1) {
            BossState boss(wyvern.bosses[0], 19);
            CHECK_EQ(boss.def().id, std::string("boss.wyvern_ninth_nest"));
            CHECK_EQ(boss.def().displayName, std::string("The Wyvern of the Ninth Nest"));
            CHECK_EQ(boss.phase(), 1);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            CHECK(offers(boss, "atk.wyvern_flame_sweep") && offers(boss, "atk.wyvern_cinder_volley"));
            CHECK(!offers(boss, "atk.wyvern_gale_beat") && !offers(boss, "atk.wyvern_tail_lash"));
            CHECK(!offers(boss, "atk.wyvern_nest_inferno"));
            boss.applyHit(1200.0f, 0.0f);  // 1800 / 3000 health: below the 0.65 threshold
            CHECK_EQ(boss.phase(), 2);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(5));
            CHECK(offers(boss, "atk.wyvern_gale_beat") && offers(boss, "atk.wyvern_tail_lash"));
            CHECK(!offers(boss, "atk.wyvern_nest_inferno"));
            boss.applyHit(0.0f, 0.0f, "armour.wyvern_left_fire_gland", 220.0f);
            CHECK(!offers(boss, "atk.wyvern_flame_sweep"));
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.nest_ledge"}));
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(4));
            boss.applyHit(1000.0f, 0.0f);  // 800 / 3000 health: below the 0.3 threshold
            CHECK_EQ(boss.phase(), 3);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(5));
            CHECK(offers(boss, "atk.wyvern_nest_inferno"));
            // Breaking the right gland silences both the cinder volley and the nest inferno.
            boss.applyHit(0.0f, 0.0f, "armour.wyvern_right_fire_gland", 220.0f);
            CHECK(!offers(boss, "atk.wyvern_cinder_volley") && !offers(boss, "atk.wyvern_nest_inferno"));
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            boss.applyHit(0.0f, 0.0f, "armour.wyvern_wing_membrane", 300.0f);
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.nest_ledge", "route.updraft_spire",
                                                                 "route.fallen_nest_floor"}));
            // With every piece broken only the ungated talon dive and tail lash remain.
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(2));
            CHECK(offers(boss, "atk.wyvern_talon_dive") && offers(boss, "atk.wyvern_tail_lash"));
        }

        // The Tautmaurg of Memory (Section 10): the death replay and build mimicry hang on its reliquary and
        // lattice, the choice reckoning joins in phase two while the mirror holds, and total recall waits for
        // phase three and still needs the death reliquary intact.
        const BossLoadResult memory = loadBossFile(dir / "tautmaurg_of_memory.json");
        CHECK(memory.ok());
        CHECK_EQ(memory.bosses.size(), std::size_t(1));
        if (memory.ok() && memory.bosses.size() == 1) {
            BossState boss(memory.bosses[0], 23);
            CHECK_EQ(boss.def().id, std::string("boss.tautmaurg_of_memory"));
            CHECK_EQ(boss.def().displayName, std::string("The Tautmaurg of Memory"));
            CHECK_EQ(boss.phase(), 1);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            CHECK(offers(boss, "atk.memory_death_replay") && offers(boss, "atk.memory_build_mimicry"));
            CHECK(!offers(boss, "atk.memory_choice_reckoning") && !offers(boss, "atk.memory_phantom_echo"));
            CHECK(!offers(boss, "atk.memory_total_recall"));
            boss.applyHit(1200.0f, 0.0f);  // 1600 / 2800 health: below the 0.6 threshold
            CHECK_EQ(boss.phase(), 2);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(5));
            CHECK(offers(boss, "atk.memory_choice_reckoning") && offers(boss, "atk.memory_phantom_echo"));
            CHECK(!offers(boss, "atk.memory_total_recall"));
            boss.applyHit(0.0f, 0.0f, "armour.memory_build_lattice", 260.0f);
            CHECK(!offers(boss, "atk.memory_build_mimicry"));
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.archive_stair"}));
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(4));
            boss.applyHit(1000.0f, 0.0f);  // 600 / 2800 health: below the 0.25 threshold
            CHECK_EQ(boss.phase(), 3);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(5));
            CHECK(offers(boss, "atk.memory_total_recall"));
            // Breaking the death reliquary silences both the death replay and total recall.
            boss.applyHit(0.0f, 0.0f, "armour.memory_death_reliquary", 240.0f);
            CHECK(!offers(boss, "atk.memory_death_replay") && !offers(boss, "atk.memory_total_recall"));
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            boss.applyHit(0.0f, 0.0f, "armour.memory_choice_mirror", 300.0f);
            CHECK(!offers(boss, "atk.memory_choice_reckoning"));
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.archive_stair", "route.echo_gallery",
                                                                 "route.forgotten_vault"}));
            // With every piece broken only the ungated gilded strike and phantom echo remain.
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(2));
            CHECK(offers(boss, "atk.memory_gilded_strike") && offers(boss, "atk.memory_phantom_echo"));
        }

        // The Crown Gate (Sections 3, 10): the final machine carries the five Crown Keys as breakable pieces.
        // The seal lockdown and throne decree start on their keys, the wound tear, equation storm and ash
        // verdict join in phase two, and the Mourge tide and chosen ending wait for phase three. Each broken
        // key silences its ending attack and rearranges the arena by opening that key's route.
        const BossLoadResult gate = loadBossFile(dir / "crown_gate.json");
        CHECK(gate.ok());
        CHECK_EQ(gate.bosses.size(), std::size_t(1));
        if (gate.ok() && gate.bosses.size() == 1) {
            BossState boss(gate.bosses[0], 29);
            CHECK_EQ(boss.def().id, std::string("boss.crown_gate"));
            CHECK_EQ(boss.def().displayName, std::string("The Crown Gate"));
            CHECK_EQ(boss.def().armour.size(), std::size_t(5));
            CHECK_EQ(boss.phase(), 1);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            CHECK(offers(boss, "atk.crown_seal_lockdown") && offers(boss, "atk.crown_throne_decree"));
            CHECK(!offers(boss, "atk.crown_wound_tear") && !offers(boss, "atk.crown_equation_storm"));
            CHECK(!offers(boss, "atk.crown_ash_verdict") && !offers(boss, "atk.crown_mourge_tide"));
            CHECK(!offers(boss, "atk.crown_chosen_ending"));
            boss.applyHit(1300.0f, 0.0f);  // 2300 / 3600 health: below the 0.66 threshold
            CHECK_EQ(boss.phase(), 2);
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(6));
            CHECK(offers(boss, "atk.crown_wound_tear") && offers(boss, "atk.crown_equation_storm"));
            CHECK(offers(boss, "atk.crown_ash_verdict"));
            CHECK(!offers(boss, "atk.crown_mourge_tide") && !offers(boss, "atk.crown_chosen_ending"));
            boss.applyHit(0.0f, 0.0f, "armour.crown_key_seal", 260.0f);
            CHECK(!offers(boss, "atk.crown_seal_lockdown"));
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.crown_seal_span"}));
            boss.applyHit(0.0f, 0.0f, "armour.crown_key_equation", 240.0f);
            CHECK(!offers(boss, "atk.crown_equation_storm"));
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.crown_seal_span", "route.crown_equation_maze"}));
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(4));
            boss.applyHit(1200.0f, 0.0f);  // 1100 / 3600 health: below the 0.33 threshold
            CHECK_EQ(boss.phase(), 3);
            CHECK(!boss.dead());
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(6));
            CHECK(offers(boss, "atk.crown_mourge_tide") && offers(boss, "atk.crown_chosen_ending"));
            // Breaking the Mourge key denies the chosen ending; the throne and verdict keys follow.
            boss.applyHit(0.0f, 0.0f, "armour.crown_key_mourge", 320.0f);
            CHECK(!offers(boss, "atk.crown_chosen_ending"));
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(5));
            boss.applyHit(0.0f, 0.0f, "armour.crown_key_throne", 280.0f);
            boss.applyHit(0.0f, 0.0f, "armour.crown_key_verdict", 300.0f);
            CHECK(!offers(boss, "atk.crown_throne_decree") && !offers(boss, "atk.crown_ash_verdict"));
            CHECK(boss.openRoutes() == std::vector<std::string>({"route.crown_seal_span", "route.crown_equation_maze",
                                                                 "route.crown_mourge_rift", "route.crown_throne_lift",
                                                                 "route.crown_ash_causeway"}));
            // With all five keys broken only the ungated gear sweep, wound tear and Mourge tide remain.
            CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
            CHECK(offers(boss, "atk.crown_gear_sweep") && offers(boss, "atk.crown_wound_tear"));
            CHECK(offers(boss, "atk.crown_mourge_tide"));
        }
    }
#endif

    return lt::test::finish("boss_record");
}
