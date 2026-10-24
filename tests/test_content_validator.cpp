// Section 25 TTools content validator: the CI gate that runs before an agent's content merge.
// Covers duplicate/unknown ids, missing localisation keys and recipe cycles.
#include <string>
#include <vector>

#include "TestHarness.h"
#include "ttools/ContentValidator.h"

using namespace lt;
using namespace lt::tools;

namespace {

bool hasIssue(const std::vector<ContentIssue>& v, ContentIssueKind k, const std::string& id,
              const std::string& detail = {}) {
    for (const auto& i : v)
        if (i.kind == k && i.id == id && (detail.empty() || i.detail == detail)) return true;
    return false;
}

loc::StringTable englishSource() {
    loc::StringTable t;
    t.set("item.aetherfang.name", "Aetherfang");
    t.set("item.aetherfang.lore", "A greatblade that remembers every arc it has cut.");
    return t;
}

// Lifecycle from Section 5: rumour -> visible -> active -> aftermath.
EventTemplate lifecycleEvent(const std::string& id) {
    EventTemplate e;
    e.id = id;
    e.source = "data/events/" + id + ".json";
    e.stages = {"rumour", "visible", "active", "aftermath"};
    e.transitions = {{"rumour", "visible"}, {"visible", "active"}, {"active", "aftermath"}};
    e.terminalStages = {"aftermath"};
    return e;
}

// Mergeable baseline: every reference resolves and every limit holds on both shipping tiers.
ContentSet cleanSet() {
    ContentSet c;
    c.builtinIds = {"aspect.motion", "aspect.form", "aspect.memory"};
    c.records.push_back({"weapon.aetherfang", "data/items/vertical_slice_weapons.json",
                         {"aspect.motion", "aspect.memory"}, {"item.aetherfang.name", "item.aetherfang.lore"}});
    c.records.push_back({"material.taumium_ingot", "data/items/materials.json", {"aspect.form"}, {}});
    c.recipes.push_back({"recipe.aetherfang", {"material.taumium_ingot"}, {"weapon.aetherfang"}});
    c.textures.push_back({"tex.aetherfang", "data/textures/aetherfang.ktx2", 1024, 1024, 2u * 1024u * 1024u});
    c.events.push_back(lifecycleEvent("event.caravan_under_siege"));
    c.save.buildSchemaVersion = 3;
    c.save.sourceSchemaVersion["data/items/vertical_slice_weapons.json"] = 3;
    c.save.savedIds = {"weapon.aetherfang", "event.caravan_under_siege"};
    return c;
}

} // namespace

int main() {
    TEST_SECTION("clean content is mergeable on every tier");
    {
        const ContentSet c = cleanSet();
        CHECK(validateContent(c, englishSource()).empty());
        CHECK(validateContent(c, englishSource(), androidTextureBudget()).empty());
    }

    TEST_SECTION("duplicate and unknown ids");
    {
        ContentSet c = cleanSet();
        c.records.push_back({"weapon.aetherfang", "mods/frostpack/items.json", {}, {}});
        c.records.push_back({"armor.frostbound_plate", "mods/frostpack/items.json", {"aspect.frost"}, {}});
        c.recipes.push_back({"recipe.aetherfang", {"material.void_shard"}, {"weapon.aetherfang"}});
        const auto issues = validateContent(c, englishSource());
        CHECK_EQ(countIssues(issues, ContentIssueKind::DuplicateId), 2u);
        CHECK(hasIssue(issues, ContentIssueKind::DuplicateId, "weapon.aetherfang", "mods/frostpack/items.json"));
        CHECK(hasIssue(issues, ContentIssueKind::DuplicateId, "recipe.aetherfang", "recipe"));
        CHECK_EQ(countIssues(issues, ContentIssueKind::UnknownId), 2u);
        CHECK(hasIssue(issues, ContentIssueKind::UnknownId, "armor.frostbound_plate", "aspect.frost"));
        CHECK(hasIssue(issues, ContentIssueKind::UnknownId, "recipe.aetherfang", "material.void_shard"));
    }

    TEST_SECTION("missing and invalid localisation keys");
    {
        ContentSet c = cleanSet();
        c.records[0].locKeys.push_back("item.aetherfang.tutorial");
        c.records[1].locKeys.push_back("Item.Bad Key");
        const auto issues = validateContent(c, englishSource());
        CHECK_EQ(countIssues(issues, ContentIssueKind::MissingLocKey), 2u);
        CHECK(hasIssue(issues, ContentIssueKind::MissingLocKey, "weapon.aetherfang", "item.aetherfang.tutorial"));
        CHECK(hasIssue(issues, ContentIssueKind::MissingLocKey, "material.taumium_ingot", "invalid key Item.Bad Key"));
    }

    TEST_SECTION("recipe cycles reported once in canonical rotation");
    {
        ContentSet c = cleanSet();
        c.records.push_back({"material.refined_taumium", "data/items/materials.json", {}, {}});
        c.recipes.push_back({"recipe.refine", {"material.taumium_ingot"}, {"material.refined_taumium"}});
        c.recipes.push_back({"recipe.smelt_back", {"material.refined_taumium"}, {"material.taumium_ingot"}});
        c.recipes.push_back({"recipe.mirror", {"weapon.aetherfang"}, {"weapon.aetherfang"}});
        const auto issues = validateContent(c, englishSource());
        CHECK_EQ(countIssues(issues, ContentIssueKind::RecipeCycle), 2u);
        CHECK(hasIssue(issues, ContentIssueKind::RecipeCycle, "material.refined_taumium",
                       "material.refined_taumium -> material.taumium_ingot -> material.refined_taumium"));
        CHECK(hasIssue(issues, ContentIssueKind::RecipeCycle, "weapon.aetherfang",
                       "weapon.aetherfang -> weapon.aetherfang"));
        CHECK_EQ(countIssues(issues, ContentIssueKind::DuplicateId), 0u);
    }

    TEST_SECTION("oversized textures per shipping tier");
    {
        ContentSet c = cleanSet();
        c.textures.push_back({"tex.frost_warden", "mods/frostpack/textures/frost_warden.ktx2", 2048, 2048,
                              6u * 1024u * 1024u});
        c.textures.push_back({"tex.broken", "mods/frostpack/textures/broken.ktx2", 0, 512, 1024u});
        const auto desktop = validateContent(c, englishSource());
        CHECK_EQ(countIssues(desktop, ContentIssueKind::OversizedTexture), 1u);
        CHECK(hasIssue(desktop, ContentIssueKind::OversizedTexture, "tex.broken",
                       "invalid dimensions in mods/frostpack/textures/broken.ktx2"));
        const auto android = validateContent(c, englishSource(), androidTextureBudget());
        CHECK_EQ(countIssues(android, ContentIssueKind::OversizedTexture), 3u);
        CHECK(hasIssue(android, ContentIssueKind::OversizedTexture, "tex.frost_warden",
                       "2048x2048 exceeds android_c31 limit 1024"));
        CHECK(hasIssue(android, ContentIssueKind::OversizedTexture, "tex.frost_warden",
                       "6291456 bytes exceeds android_c31 limit 4194304"));
        CHECK(hasIssue(android, ContentIssueKind::OversizedTexture, "tex.broken",
                       "invalid dimensions in mods/frostpack/textures/broken.ktx2"));
    }

    TEST_SECTION("save incompatibilities");
    {
        ContentSet c = cleanSet();
        c.save.sourceSchemaVersion["data/items/materials.json"] = 2; // older schemas still load
        c.save.sourceSchemaVersion["mods/frostpack"] = 4;
        c.save.savedIds.insert("armor.removed_plate");
        const auto issues = validateContent(c, englishSource());
        CHECK_EQ(countIssues(issues, ContentIssueKind::SaveIncompatible), 2u);
        CHECK(hasIssue(issues, ContentIssueKind::SaveIncompatible, "mods/frostpack",
                       "save schema 4 is newer than build schema 3"));
        CHECK(hasIssue(issues, ContentIssueKind::SaveIncompatible, "armor.removed_plate",
                       "persisted id no longer resolves"));
    }
    {
        // Saved builtin, recipe and texture ids keep resolving after the merge.
        ContentSet c = cleanSet();
        c.save.savedIds.insert({"aspect.motion", "recipe.aetherfang", "tex.aetherfang"});
        CHECK(validateContent(c, englishSource()).empty());
    }

    TEST_SECTION("event lifecycle deadlocks");
    {
        ContentSet c = cleanSet();
        EventTemplate empty;
        empty.id = "event.empty";
        empty.source = "mods/frostpack/events/empty.json";
        EventTemplate noEnd = lifecycleEvent("event.no_end");
        noEnd.terminalStages = {"resolved"};
        EventTemplate badEdge = lifecycleEvent("event.bad_edge");
        badEdge.transitions.push_back({"active", "riftfall"});
        // "sealed" is reachable but has no way out; "orphan" is unreachable and therefore harmless.
        EventTemplate trap = lifecycleEvent("event.trap");
        trap.stages.push_back("sealed");
        trap.stages.push_back("orphan");
        trap.transitions.push_back({"visible", "sealed"});
        c.events.insert(c.events.end(), {empty, noEnd, badEdge, trap});
        const auto issues = validateContent(c, englishSource());
        CHECK_EQ(issues.size(), 5u);
        CHECK_EQ(countIssues(issues, ContentIssueKind::EventDeadlock), 5u);
        CHECK(hasIssue(issues, ContentIssueKind::EventDeadlock, "event.empty",
                       "no stages in mods/frostpack/events/empty.json"));
        CHECK(hasIssue(issues, ContentIssueKind::EventDeadlock, "event.no_end",
                       "terminal stage resolved is not a stage"));
        CHECK(hasIssue(issues, ContentIssueKind::EventDeadlock, "event.no_end", "no terminal stage"));
        CHECK(hasIssue(issues, ContentIssueKind::EventDeadlock, "event.bad_edge",
                       "transition active -> riftfall names an unknown stage"));
        CHECK(hasIssue(issues, ContentIssueKind::EventDeadlock, "event.trap",
                       "stage sealed cannot reach a terminal stage"));
        CHECK(!hasIssue(issues, ContentIssueKind::EventDeadlock, "event.trap",
                        "stage orphan cannot reach a terminal stage"));
        CHECK(!hasIssue(issues, ContentIssueKind::EventDeadlock, "event.caravan_under_siege"));
    }

    TEST_SECTION("event prerequisite deadlocks");
    {
        ContentSet c = cleanSet();
        EventTemplate escort = lifecycleEvent("event.lost_research_party");
        escort.requiresResolved = {"event.caravan_under_siege"}; // resolvable prerequisite
        EventTemplate orphan = lifecycleEvent("event.shrine_eclipse");
        orphan.requiresResolved = {"event.missing"};
        EventTemplate loopA = lifecycleEvent("event.golem_uprising");
        loopA.requiresResolved = {"event.bandit_siege"};
        EventTemplate loopB = lifecycleEvent("event.bandit_siege");
        loopB.requiresResolved = {"event.golem_uprising"};
        EventTemplate downstream = lifecycleEvent("event.ash_storm");
        downstream.requiresResolved = {"event.golem_uprising", "event.lost_research_party"};
        EventTemplate stuck = lifecycleEvent("event.riftfall");
        stuck.terminalStages.clear();
        EventTemplate waitsOnStuck = lifecycleEvent("event.frost_warden_hunt");
        waitsOnStuck.requiresResolved = {"event.riftfall"};
        EventTemplate dup = lifecycleEvent("event.caravan_under_siege");
        dup.source = "mods/frostpack/events/caravan.json";
        c.events.insert(c.events.end(), {escort, orphan, loopA, loopB, downstream, stuck, waitsOnStuck, dup});
        const auto issues = validateContent(c, englishSource());
        CHECK_EQ(issues.size(), 7u);
        CHECK_EQ(countIssues(issues, ContentIssueKind::EventDeadlock), 6u);
        CHECK(hasIssue(issues, ContentIssueKind::EventDeadlock, "event.shrine_eclipse",
                       "start blocked by unknown event.missing"));
        CHECK(hasIssue(issues, ContentIssueKind::EventDeadlock, "event.golem_uprising",
                       "start blocked by event.bandit_siege"));
        CHECK(hasIssue(issues, ContentIssueKind::EventDeadlock, "event.bandit_siege",
                       "start blocked by event.golem_uprising"));
        CHECK(hasIssue(issues, ContentIssueKind::EventDeadlock, "event.ash_storm",
                       "start blocked by event.golem_uprising"));
        CHECK(hasIssue(issues, ContentIssueKind::EventDeadlock, "event.riftfall", "no terminal stage"));
        CHECK(hasIssue(issues, ContentIssueKind::EventDeadlock, "event.frost_warden_hunt",
                       "start blocked by event.riftfall"));
        CHECK(!hasIssue(issues, ContentIssueKind::EventDeadlock, "event.lost_research_party"));
        CHECK_EQ(countIssues(issues, ContentIssueKind::DuplicateId), 1u);
        CHECK(hasIssue(issues, ContentIssueKind::DuplicateId, "event.caravan_under_siege",
                       "mods/frostpack/events/caravan.json"));
    }

    return lt::test::finish("content_validator");
}
