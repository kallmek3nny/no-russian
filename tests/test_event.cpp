// Milestone B event tests: lifecycle transitions, resolution outcomes,
// aftermath persistence, cooldowns, compatibility, and ledger save/load.
#include <string>

#include "TestHarness.h"
#include "tworld/Event.h"

using namespace lt::world;

int main() {
    EventDef def;
    def.id = "event.caravan_under_siege";
    def.biomes = {"temperate_forest", "grassland"};
    def.faction = "bandits";
    def.failureResult = "road_bandit_checkpoint";
    def.successResult = "travelling_vendor_and_research_note";
    def.cooldownHours = 48;

    TEST_SECTION("compatibility");
    CHECK(def.compatible("grassland", "bandits"));
    CHECK(!def.compatible("desert", "bandits"));
    CHECK(!def.compatible("grassland", "cultists"));
    EventDef open;
    open.id = "event.open";
    CHECK(open.compatible("anything", "anyone"));

    TEST_SECTION("lifecycle");
    EventLedger ledger;
    EventInstance& e = ledger.spawn(def, 3, -7, 100);
    CHECK(e.state == EventState::Rumour);
    CHECK(!e.resolve(def, true, 100));
    CHECK(e.advance(110));
    CHECK(e.state == EventState::Visible);
    CHECK(e.advance(120));
    CHECK(e.state == EventState::Active);
    CHECK(!e.advance(125));
    CHECK(!e.settle(125));
    CHECK(e.resolve(def, false, 130));
    CHECK(e.state == EventState::Resolved);
    CHECK(e.outcome == EventOutcome::Failure);
    CHECK_EQ(e.aftermath, std::string("road_bandit_checkpoint"));
    CHECK(!e.resolve(def, true, 131));
    CHECK(e.settle(140));
    CHECK(e.state == EventState::Aftermath);
    CHECK(!e.advance(141));
    CHECK(std::string(toString(e.state)) == "aftermath");

    TEST_SECTION("cooldown");
    CHECK(!e.cooldownElapsed(def, 187));
    CHECK(e.cooldownElapsed(def, 188));

    TEST_SECTION("success outcome");
    EventInstance& s = ledger.spawn(def, 0, 0, 0);
    s.advance(1);
    s.advance(2);
    CHECK(s.resolve(def, true, 3));
    CHECK(s.outcome == EventOutcome::Success);
    CHECK_EQ(s.aftermath, std::string("travelling_vendor_and_research_note"));

    TEST_SECTION("serialize round trip");
    auto rt = EventInstance::deserialize(ledger.events()[0].serialize());
    CHECK(rt.has_value());
    CHECK(rt && rt->defId == def.id);
    CHECK(rt && rt->regionX == 3 && rt->regionZ == -7);
    CHECK(rt && rt->state == EventState::Aftermath);
    CHECK(rt && rt->outcome == EventOutcome::Failure);
    CHECK(rt && rt->stateSinceHour == 140);
    CHECK(rt && rt->aftermath == "road_bandit_checkpoint");
    EventInstance fresh;
    fresh.defId = "event.x";
    auto rf = EventInstance::deserialize(fresh.serialize());
    CHECK(rf && rf->aftermath.empty());
    CHECK(!EventInstance::deserialize("garbage").has_value());
    CHECK(!EventInstance::deserialize("event.x 0 0 9 0 0 -").has_value());

    TEST_SECTION("ledger save/load");
    const std::string saved = ledger.save();
    EventLedger reloaded;
    CHECK(reloaded.load(saved));
    CHECK(reloaded.events().size() == 2);
    CHECK(reloaded.save() == saved);
    EventLedger bad;
    CHECK(!bad.load("event.x 0 0 1 0 0 -\nnot valid\n"));
    CHECK(bad.events().empty());

    return ::lt::test::finish("event");
}
