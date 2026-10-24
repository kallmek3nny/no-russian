// Section 21 event records: shipped autonomous events decode into the EventDefs that drive the Section 5
// lifecycle, omitted requirements mean "anywhere", and strict record errors (unknown keys, bad requirement
// lists, stages that skip rumour or aftermath, bad cooldowns, duplicate ids) name the source and record.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "TestHarness.h"
#include "tworld/Event.h"
#include "tworld/EventRecord.h"

using namespace lt::world;

namespace {

const std::string kRequirements = R"({"biome":["frost_crown","salt_dune"],"faction":"ash_tribunal"})";
const std::string kRecord = R"({"id":"event.test_raid","requirements":)" + kRequirements +
                            R"(,"stages":["rumour","raid","aftermath"],"failure_result":"burned_outpost",)"
                            R"("success_result":"tribunal_patrol","cooldown_hours":12})";

// kRecord with the first `from` replaced by `to`.
std::string with(std::string_view from, std::string_view to, std::string s = kRecord) {
    const std::size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

EventLoadResult load(const std::string& records) { return parseEventRecords("[" + records + "]", "t.json"); }

bool failsWith(const std::string& records, std::string_view message) {
    const EventLoadResult r = load(records);
    return !r.ok() && r.events.empty() && r.error.find(message) != std::string::npos;
}

EventDef makeEvent(std::string id, std::vector<std::string> biomes, std::string faction, std::vector<std::string> stages,
                   std::string failure, std::string success, std::uint32_t cooldownHours) {
    EventDef d;
    d.id = std::move(id);
    d.biomes = std::move(biomes);
    d.faction = std::move(faction);
    d.stages = std::move(stages);
    d.failureResult = std::move(failure);
    d.successResult = std::move(success);
    d.cooldownHours = cooldownHours;
    return d;
}

bool sameEvent(const EventDef& a, const EventDef& b) {
    return a.id == b.id && a.biomes == b.biomes && a.faction == b.faction && a.stages == b.stages &&
           a.failureResult == b.failureResult && a.successResult == b.successResult &&
           a.cooldownHours == b.cooldownHours;
}

}  // namespace

int main() {
    TEST_SECTION("valid record");
    {
        const EventLoadResult r = load(kRecord);
        CHECK(r.ok());
        CHECK_EQ(r.events.size(), 1u);
        const EventDef expected = makeEvent("event.test_raid", {"frost_crown", "salt_dune"}, "ash_tribunal",
                                            {"rumour", "raid", "aftermath"}, "burned_outpost", "tribunal_patrol", 12);
        CHECK(r.ok() && sameEvent(r.events[0], expected));
        CHECK(r.ok() && r.events[0].compatible("salt_dune", "ash_tribunal"));
        CHECK(r.ok() && !r.events[0].compatible("salt_dune", "free_caravans"));
        CHECK(r.ok() && !r.events[0].compatible("verdant_reach", "ash_tribunal"));
        CHECK(load("").ok() && load("").events.empty());
    }

    TEST_SECTION("omitted requirements fit anywhere");
    {
        const EventLoadResult open = load(with(kRequirements, "{}"));
        CHECK(open.ok() && open.events[0].biomes.empty() && open.events[0].faction.empty());
        CHECK(open.ok() && open.events[0].compatible("underroot", "mourge_cults"));
        const EventLoadResult factionOnly = load(with(R"("biome":["frost_crown","salt_dune"],)", ""));
        CHECK(factionOnly.ok() && factionOnly.events[0].biomes.empty());
        CHECK(factionOnly.ok() && factionOnly.events[0].compatible("ash_barrens", "ash_tribunal"));
        CHECK(factionOnly.ok() && !factionOnly.events[0].compatible("ash_barrens", "veyr_smugglers"));
        const EventLoadResult biomeOnly = load(with(R"(,"faction":"ash_tribunal")", ""));
        CHECK(biomeOnly.ok() && biomeOnly.events[0].faction.empty());
        CHECK(biomeOnly.ok() && biomeOnly.events[0].compatible("frost_crown", "free_caravans"));
        CHECK(load(with(R"("cooldown_hours":12)", R"("cooldown_hours":0)")).ok());
        const EventLoadResult longest = load(with(R"("cooldown_hours":12)", R"("cooldown_hours":8760)"));
        CHECK(longest.ok() && longest.events[0].cooldownHours == kMaxEventCooldownHours);
        CHECK(load(with(R"(["rumour","raid","aftermath"])", R"(["rumour","aftermath"])")).ok());
    }

    TEST_SECTION("record errors");
    const std::string kStageOrder = "'stages' must open with \"rumour\" and close with \"aftermath\"";
    const std::string kCooldownRange =
        "'cooldown_hours' must be an integer from 0 to " + std::to_string(kMaxEventCooldownHours);
    CHECK(failsWith(with(R"("cooldown_hours":12})", R"("cooldown_hours":12,"weight":3})"),
                    "t.json: record 1 (event.test_raid): unknown key 'weight'"));
    CHECK(failsWith(with(R"("id":"event.test_raid",)", ""), "t.json: record 1: 'id' must be a non-empty string"));
    CHECK(failsWith(with(R"("id":"event.test_raid")", R"("id":"")"), "t.json: record 1: 'id' must be a non-empty string"));
    CHECK(failsWith(with(R"("requirements":)" + kRequirements + ",", ""),
                    "'requirements' must be an object (use {} for anywhere)"));
    CHECK(failsWith(with(kRequirements, "[]"), "'requirements' must be an object (use {} for anywhere)"));
    CHECK(failsWith(with(R"("faction":"ash_tribunal"})", R"("faction":"ash_tribunal","season":"winter"})"),
                    "unknown requirements key 'season'"));
    CHECK(failsWith(with(R"(["frost_crown","salt_dune"])", R"("frost_crown")"),
                    "'requirements.biome' must be an array of strings"));
    CHECK(failsWith(with(R"(["frost_crown","salt_dune"])", R"(["frost_crown",7])"),
                    "'requirements.biome' must only hold non-empty strings"));
    CHECK(failsWith(with(R"(["frost_crown","salt_dune"])", "[]"),
                    "'requirements.biome' must name a biome; omit it for every biome"));
    CHECK(failsWith(with(R"("frost_crown","salt_dune")", R"("salt_dune","salt_dune")"),
                    "duplicate 'requirements.biome' entry 'salt_dune'"));
    CHECK(failsWith(with(R"("faction":"ash_tribunal")", R"("faction":"")"),
                    "'requirements.faction' must be a non-empty string"));
    CHECK(failsWith(with(R"("faction":"ash_tribunal")", R"("faction":["ash_tribunal"])"),
                    "'requirements.faction' must be a non-empty string"));
    CHECK(failsWith(with(R"(["rumour","raid","aftermath"])", R"(["raid","aftermath"])"), kStageOrder));
    CHECK(failsWith(with(R"(["rumour","raid","aftermath"])", R"(["rumour","raid"])"), kStageOrder));
    CHECK(failsWith(with(R"(["rumour","raid","aftermath"])", R"(["rumour"])"), kStageOrder));
    CHECK(failsWith(with(R"(["rumour","raid","aftermath"])", "[]"), kStageOrder));
    CHECK(failsWith(with(R"("raid","aftermath")", R"("raid","raid","aftermath")"), "duplicate 'stages' entry 'raid'"));
    CHECK(failsWith(with(R"(["rumour","raid","aftermath"])", R"("rumour")"), "'stages' must be an array of strings"));
    CHECK(failsWith(with(R"("failure_result":"burned_outpost",)", ""), "'failure_result' must be a non-empty string"));
    CHECK(failsWith(with(R"("success_result":"tribunal_patrol")", R"("success_result":5)"),
                    "'success_result' must be a non-empty string"));
    CHECK(failsWith(with(R"("cooldown_hours":12)", R"("cooldown_hours":-1)"), kCooldownRange));
    CHECK(failsWith(with(R"("cooldown_hours":12)", R"("cooldown_hours":8761)"), kCooldownRange));
    CHECK(failsWith(with(R"("cooldown_hours":12)", R"("cooldown_hours":1.5)"), kCooldownRange));
    CHECK(failsWith(with(R"("cooldown_hours":12)", R"("cooldown_hours":"12")"), kCooldownRange));
    CHECK(failsWith(with(R"(,"cooldown_hours":12)", ""), kCooldownRange));
    CHECK(failsWith("42", "t.json: record 1: record must be an object, not number"));
    CHECK(failsWith(kRecord + "," + kRecord,
                    "t.json: record 2 (event.test_raid): duplicate id 'event.test_raid' (first defined in record 1)"));
    CHECK(failsWith(kRecord + "," + with(R"("cooldown_hours":12)", R"("cooldown_hours":"x")"),
                    "t.json: record 2 (event.test_raid)"));

    TEST_SECTION("document errors");
    {
        const EventLoadResult top = parseEventRecords(kRecord, "t.json");
        CHECK(!top.ok() && top.events.empty());
        CHECK(top.error.rfind("t.json: top level must be an array of event records, not object", 0) == 0);
        const EventLoadResult syntax = parseEventRecords("[" + kRecord, "t.json");
        CHECK(!syntax.ok() && syntax.events.empty());
        CHECK(syntax.error.rfind("t.json:1:", 0) == 0);
        const EventLoadResult missing = loadEventFile("definitely/not/here.json");
        CHECK(!missing.ok() && missing.events.empty());
        CHECK(missing.error.rfind("definitely/not/here.json: ", 0) == 0);
    }

#ifdef LT_DATA_DIR
    TEST_SECTION("shipped autonomous events");
    {
        const EventLoadResult r = loadEventFile(std::string(LT_DATA_DIR) + "/events/autonomous_events.json");
        CHECK_EQ(r.error, std::string());
        CHECK_EQ(r.events.size(), 7u);
        const auto byId = [&r](std::string_view id) -> const EventDef* {
            for (const EventDef& e : r.events)
                if (e.id == id) return &e;
            return nullptr;
        };
        // The Section 5 / 21 example record, field for field.
        const EventDef caravan =
            makeEvent("event.caravan_under_siege", {"verdant_reach", "salt_dune"}, "free_caravans",
                      {"rumour", "ambush", "rescue", "aftermath"}, "road_bandit_checkpoint",
                      "travelling_vendor_and_research_note", 18);
        const EventDef* siege = byId("event.caravan_under_siege");
        CHECK(siege != nullptr && sameEvent(*siege, caravan));
        const EventDef* riftfall = byId("event.riftfall");
        CHECK(riftfall != nullptr && riftfall->biomes.empty() && riftfall->faction.empty());
        CHECK(riftfall != nullptr && riftfall->compatible("underroot", "mourge_cults"));
        const EventDef* duel = byId("event.tautmaurg_duel");
        CHECK(duel != nullptr && duel->compatible("frost_crown", "tautmaurg_remnants"));
        CHECK(duel != nullptr && !duel->compatible("frost_crown", "free_caravans"));
        const EventDef* nest = byId("event.wyvern_nest");
        CHECK(nest != nullptr && nest->compatible("ash_barrens", "veyr_smugglers"));
        CHECK(nest != nullptr && !nest->compatible("salt_dune", "veyr_smugglers"));
        for (const EventDef& e : r.events) {
            CHECK(e.id.rfind("event.", 0) == 0);
            CHECK(e.stages.size() >= 3);
            CHECK(e.failureResult != e.successResult);
            CHECK(e.cooldownHours > 0);
        }
    }
#endif

    return lt::test::finish("event_record");
}
