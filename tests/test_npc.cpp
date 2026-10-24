// Section 11 NPC tests: daily schedules (including the midnight wrap), relationship
// graph clamping, fate transitions (move, vendor, enemy, death, phantom record) and
// the location an NPC occupies for each fate.
#include <cstddef>
#include <cstdint>
#include <string>

#include "TestHarness.h"
#include "game/Npc.h"

using namespace lt::game;

namespace {

NpcDef makeIlyra() {
    NpcDef d;
    d.id = "npc.ilyra_voss";
    d.nameKey = "npc.ilyra_voss.name";
    d.faction = Faction::OrderOfThaumiel;
    d.goalKey = "npc.ilyra_voss.goal";
    d.fearKey = "npc.ilyra_voss.fear";
    d.role = NpcCombatRole::Caster;
    d.failureState = "turns_enemy";
    d.schedule = {{6, "aurelia.research_hall", "research"},
                  {12, "aurelia.courtyard", "train"},
                  {18, "aurelia.archive", "study"},
                  {22, "aurelia.quarters", "sleep"}};
    d.relations = {{"npc.orun_vale", -20}, {"npc.sister_neme", 60}, {"npc.nameless_archivist", 150}};
    return d;
}

} // namespace

// Fate rules are constexpr; prove they evaluate at compile time.
static_assert(isLiving(NpcFate::Vendor));
static_assert(!isLiving(NpcFate::Dead));
static_assert(canTransition(NpcFate::Dead, NpcFate::Phantom));
static_assert(!canTransition(NpcFate::Phantom, NpcFate::Alive));

int main() {
    const NpcDef ilyra = makeIlyra();

    TEST_SECTION("definition");
    CHECK(ilyra.role == NpcCombatRole::Caster);
    CHECK(ilyra.faction == Faction::OrderOfThaumiel);
    CHECK(ilyra.failureState == "turns_enemy");
    CHECK(ilyra.nameKey == "npc.ilyra_voss.name");

    TEST_SECTION("schedule");
    CHECK(scheduleAt(ilyra, 6)->location == "aurelia.research_hall");
    CHECK(scheduleAt(ilyra, 11)->activity == "research");
    CHECK(scheduleAt(ilyra, 12)->location == "aurelia.courtyard");
    CHECK(scheduleAt(ilyra, 21)->location == "aurelia.archive");
    CHECK(scheduleAt(ilyra, 23)->activity == "sleep");
    CHECK(scheduleAt(ilyra, 3)->activity == "sleep");                   // wraps past midnight
    CHECK(scheduleAt(ilyra, 30)->location == "aurelia.research_hall");  // 30 % 24 == 6
    const NpcDef empty;
    CHECK(scheduleAt(empty, 10) == nullptr);

    TEST_SECTION("relationships");
    CHECK_EQ(baseAffinity(ilyra, "npc.sister_neme"), 60);
    CHECK_EQ(baseAffinity(ilyra, "npc.rusk"), 0);
    NpcState s(ilyra);
    CHECK(s.defId() == "npc.ilyra_voss");
    CHECK_EQ(s.affinity("npc.nameless_archivist"), kAffinityMax);  // clamped on load
    CHECK_EQ(s.affinity("npc.orun_vale"), -20);
    CHECK_EQ(s.adjustAffinity("npc.orun_vale", -500), kAffinityMin);
    CHECK_EQ(s.adjustAffinity("npc.rusk", 35), 35);  // new edge added
    CHECK_EQ(s.relations().size(), std::size_t{4});
    CHECK_EQ(s.adjustAffinity("npc.rusk", 10), 45);
    CHECK_EQ(s.adjustAffinity("npc.veyra", 900), kAffinityMax);
    CHECK_EQ(baseAffinity(ilyra, "npc.orun_vale"), -20);  // definition untouched

    TEST_SECTION("fate");
    CHECK(s.fate() == NpcFate::Alive);
    CHECK(s.living());
    CHECK(s.currentLocation(ilyra, 13) == "aurelia.courtyard");
    CHECK(!s.setFate(NpcFate::Alive, 5));    // no self transition
    CHECK(!s.setFate(NpcFate::Phantom, 5));  // only the dead leave phantoms
    CHECK(!s.setFate(NpcFate::Moved, 5));    // a move needs a destination
    CHECK(s.fate() == NpcFate::Alive);
    CHECK_EQ(s.fateSinceHour(), std::uint64_t{0});
    CHECK(s.setFate(NpcFate::Moved, 40, "mournhollow.gate"));
    CHECK(s.movedTo() == "mournhollow.gate");
    CHECK_EQ(s.fateSinceHour(), std::uint64_t{40});
    CHECK(s.currentLocation(ilyra, 13) == "mournhollow.gate");
    CHECK(s.setFate(NpcFate::Vendor, 50));
    CHECK(s.currentLocation(ilyra, 2) == "mournhollow.gate");  // vendor stays where she moved
    CHECK(s.setFate(NpcFate::Alive, 60));
    CHECK(s.movedTo().empty());
    CHECK(s.currentLocation(ilyra, 19) == "aurelia.archive");
    CHECK(s.setFate(NpcFate::Enemy, 70));
    CHECK(s.living());
    CHECK(s.setFate(NpcFate::Dead, 80));
    CHECK(!s.living());
    CHECK(s.currentLocation(ilyra, 13).empty());
    CHECK(!s.setFate(NpcFate::Alive, 81));  // death is final
    CHECK(!s.setFate(NpcFate::Vendor, 81));
    CHECK(s.setFate(NpcFate::Phantom, 90));
    CHECK(!s.setFate(NpcFate::Dead, 91));  // phantom is terminal
    CHECK(s.fate() == NpcFate::Phantom);
    CHECK_EQ(s.fateSinceHour(), std::uint64_t{90});
    CHECK(s.currentLocation(ilyra, 13).empty());

    TEST_SECTION("transition table");
    for (int a = 0; a <= static_cast<int>(NpcFate::Phantom); ++a) {
        for (int b = 0; b <= static_cast<int>(NpcFate::Phantom); ++b) {
            const auto from = static_cast<NpcFate>(a);
            const auto to = static_cast<NpcFate>(b);
            bool expected = false;
            if (from != to) {
                if (isLiving(from)) expected = to != NpcFate::Phantom;
                else expected = from == NpcFate::Dead && to == NpcFate::Phantom;
            }
            CHECK_EQ(canTransition(from, to), expected);
        }
    }

    return ::lt::test::finish("npc");
}
