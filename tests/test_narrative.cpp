// Section 3 narrative tests: Act I aura nodes, Act II routes in any order (with the
// completion order recorded), Act III city outcomes and protecting factions, Council
// fates, Act IV Crown Keys, the Crown Gate state code, and ending availability/choice.
#include <array>
#include <cstddef>
#include <cstdint>

#include "TestHarness.h"
#include "game/Narrative.h"

using namespace lt::game;

namespace {

constexpr KeyState kAsm = KeyState::Assembled;
constexpr KeyState kDes = KeyState::Destroyed;

// Plays a fresh campaign up to Act IV: nodes lit, routes done out of order, cities defended.
NarrativeState toCrownGate() {
    NarrativeState s;
    for (std::size_t n = 0; n < kActOneNodes; ++n) s.relightNode(n);
    const std::array<std::size_t, kRouteCount> order{3, 0, 4, 1, 2};
    for (std::size_t r : order) s.completeRoute(r);
    for (std::size_t c = 0; c < kCityCount; ++c) s.resolveCity(c, CityOutcome::Defended);
    return s;
}

void decideKeys(NarrativeState& s, const std::array<KeyState, kCrownKeyCount>& keys) {
    for (std::size_t i = 0; i < kCrownKeyCount; ++i) s.decideKey(i, keys[i]);
}

} // namespace

// Localisation key mapping is constexpr; prove it at compile time.
static_assert(toKey(Act::CrownGate) == "act.crown_gate");
static_assert(toKey(Ending::WildEquation) == "ending.wild_equation");
static_assert(toKey(Ending::Mourge) == "ending.mourge");

int main() {
    TEST_SECTION("act_one");
    NarrativeState s;
    CHECK(s.act() == Act::BrokenSeal);
    CHECK(s.ending() == Ending::None);
    CHECK(!s.completeRoute(0));            // routes open only in Act II
    CHECK(s.relightNode(0));
    CHECK(!s.relightNode(0));              // already lit
    CHECK(!s.relightNode(kActOneNodes));   // bad index
    CHECK(s.relightNode(2));
    CHECK(s.act() == Act::BrokenSeal);
    CHECK(s.relightNode(1));
    CHECK_EQ(s.nodesLitCount(), kActOneNodes);
    CHECK(s.act() == Act::FiveRoads);

    TEST_SECTION("five_roads");
    CHECK(!s.resolveCity(0, CityOutcome::Defended));  // Council War not started yet
    CHECK(s.completeRoute(3));
    CHECK(s.completeRoute(0));
    CHECK(!s.completeRoute(3));            // already complete
    CHECK(!s.completeRoute(kRouteCount));  // bad index
    CHECK(s.completeRoute(4));
    CHECK(s.completeRoute(1));
    CHECK(s.act() == Act::FiveRoads);
    CHECK_EQ(s.routePosition(2), -1);      // still open
    CHECK(s.completeRoute(2));
    CHECK(s.act() == Act::CouncilWar);
    CHECK_EQ(s.routesCompleted(), kRouteCount);
    CHECK_EQ(s.routePosition(3), 0);
    CHECK_EQ(s.routePosition(1), 3);
    CHECK_EQ(s.routePosition(2), 4);

    TEST_SECTION("council_war");
    CHECK(!s.decideKey(0, kAsm));          // keys only in Act IV
    CHECK(!s.resolveCity(0, CityOutcome::Contested));
    CHECK(s.resolveCity(0, CityOutcome::Defended));
    CHECK(!s.resolveCity(0, CityOutcome::Abandoned));  // decision is final
    CHECK(s.resolveCity(1, CityOutcome::Protected, Faction::VerdantCourt));
    CHECK(s.resolveCity(2, CityOutcome::Abandoned));
    CHECK(s.resolveCity(3, CityOutcome::Protected, Faction::KharDuunMiners));
    CHECK(!s.resolveCity(kCityCount, CityOutcome::Defended));
    CHECK(s.act() == Act::CouncilWar);
    CHECK(s.resolveCity(4, CityOutcome::Defended));
    CHECK(s.act() == Act::CrownGate);
    CHECK(s.cityOutcome(2) == CityOutcome::Abandoned);
    CHECK(s.cityOutcome(kCityCount) == CityOutcome::Contested);
    CHECK(s.cityProtector(1) == Faction::VerdantCourt);
    CHECK(s.cityProtector(3) == Faction::KharDuunMiners);

    TEST_SECTION("council_fates");
    CHECK(s.setCouncilFate(0, CouncilFate::Killed));
    CHECK(!s.setCouncilFate(0, CouncilFate::Redeemed));  // fate is final
    CHECK(!s.setCouncilFate(1, CouncilFate::Unresolved));
    CHECK(!s.setCouncilFate(kCouncilSeats, CouncilFate::Killed));
    CHECK(s.setCouncilFate(4, CouncilFate::Redeemed));
    CHECK_EQ(s.councilCount(CouncilFate::Killed), std::size_t{1});
    CHECK_EQ(s.councilCount(CouncilFate::Unresolved), kCouncilSeats - 2);
    CHECK(s.councilFate(4) == CouncilFate::Redeemed);
    CHECK(s.councilFate(kCouncilSeats) == CouncilFate::Unresolved);

    TEST_SECTION("crown_keys");
    CHECK(!s.endingAvailable(Ending::Mourge));  // keys still undecided
    CHECK(!s.chooseEnding(Ending::Mourge));
    CHECK(!s.decideKey(0, KeyState::Missing));
    CHECK(!s.decideKey(kCrownKeyCount, kAsm));
    CHECK(s.decideKey(0, kAsm));
    CHECK(!s.decideKey(0, kDes));               // decision is final
    for (std::size_t k = 1; k < kCrownKeyCount; ++k) CHECK(s.decideKey(k, kAsm));
    CHECK(s.allKeysDecided());
    CHECK_EQ(s.keyCount(kAsm), kCrownKeyCount);
    CHECK(s.keyState(kCrownKeyCount) == KeyState::Missing);

    TEST_SECTION("ending_availability");
    CHECK(s.endingAvailable(Ending::Seal));     // all assembled, nobody empowered
    CHECK(s.endingAvailable(Ending::Throne));
    CHECK(!s.endingAvailable(Ending::WildEquation));
    CHECK(!s.endingAvailable(Ending::AshVerdict));
    CHECK(s.endingAvailable(Ending::Mourge));
    CHECK(!s.endingAvailable(Ending::None));
    {
        NarrativeState e = s;
        CHECK(e.setCouncilFate(5, CouncilFate::Empowered));
        CHECK(!e.endingAvailable(Ending::Seal));  // an empowered Tautmaurg blocks the Seal
        CHECK(e.endingAvailable(Ending::Throne));
    }
    {
        NarrativeState m = toCrownGate();
        decideKeys(m, {kAsm, kDes, kDes, kDes, kAsm});
        CHECK(m.endingAvailable(Ending::WildEquation));
        CHECK(m.endingAvailable(Ending::AshVerdict));  // three of five destroyed
        CHECK(!m.endingAvailable(Ending::Throne));
        CHECK(!m.endingAvailable(Ending::Seal));
    }
    {
        NarrativeState m = toCrownGate();
        decideKeys(m, {kAsm, kDes, kAsm, kDes, kAsm});
        CHECK(m.endingAvailable(Ending::WildEquation));
        CHECK(!m.endingAvailable(Ending::AshVerdict));  // two destroyed is no majority
    }

    TEST_SECTION("gate_state");
    {
        NarrativeState g = toCrownGate();
        CHECK_EQ(g.gateStateCode(), std::uint32_t{0});
        g.setCouncilFate(0, CouncilFate::Killed);     // 1 << 0
        g.setCouncilFate(5, CouncilFate::Empowered);  // 3 << 10
        g.decideKey(0, kAsm);                         // 1 << 12
        g.decideKey(4, kDes);                         // 2 << 20
        CHECK_EQ(g.gateStateCode(), std::uint32_t{1u + (3u << 10) + (1u << 12) + (2u << 20)});
        NarrativeState h = toCrownGate();
        h.setCouncilFate(0, CouncilFate::Killed);
        h.setCouncilFate(5, CouncilFate::Empowered);
        h.decideKey(0, kAsm);
        h.decideKey(4, kDes);
        CHECK_EQ(h.gateStateCode(), g.gateStateCode());  // same choices, same gate
        h.decideKey(1, kAsm);
        CHECK(h.gateStateCode() != g.gateStateCode());
    }

    TEST_SECTION("choose_ending");
    CHECK(!s.chooseEnding(Ending::None));
    CHECK(s.chooseEnding(Ending::Seal));
    CHECK(s.act() == Act::PostGame);
    CHECK(s.ending() == Ending::Seal);
    CHECK(!s.alteredPostGame());
    CHECK(!s.chooseEnding(Ending::Throne));            // ending is final
    CHECK(!s.failGate());
    CHECK(!s.setCouncilFate(1, CouncilFate::Killed));  // fates frozen after the ending
    {
        NarrativeState f = toCrownGate();
        CHECK(f.failGate());                           // failing forces the Mourge ending
        CHECK(f.ending() == Ending::Mourge);
        CHECK(f.alteredPostGame());
        CHECK(f.act() == Act::PostGame);
        NarrativeState fresh;
        CHECK(!fresh.failGate());
        CHECK(fresh.ending() == Ending::None);
    }
    {
        NarrativeState fed = toCrownGate();
        decideKeys(fed, {kDes, kDes, kDes, kDes, kDes});
        CHECK(fed.chooseEnding(Ending::Mourge));       // deliberately feeding the gate
        CHECK(fed.alteredPostGame());
    }

    return lt::test::finish("narrative");
}
