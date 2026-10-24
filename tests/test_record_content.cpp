// Sections 21/25 TTools record bridge: decoded weapon, enemy and event records become validator inputs, data/ and
// every mods/<id>/ merge in a stable order, and a file that fails decoding is a load error that adds nothing.
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "TestHarness.h"
#include "ttools/RecordContent.h"

using namespace lt;
using namespace lt::tools;
namespace fs = std::filesystem;

namespace {

const std::string kWeapon =
    R"({"id":"weapon.test_blade","display_name":"Test Blade","family":"longsword","damage":{"physical":10},)"
    R"("posture":5,"vis_cost":1,"weight":2,"sockets":[],"moveset":"longsword_01","aspect_tags":["motion","form"],)"
    R"("lore_key":"item.test_blade.lore","timing":{"windup_ms":300,"active_ms":100,"recovery_ms":400,"stamina":14}})";
const std::string kEnemy =
    R"({"id":"enemy.test_husk","role":"brute","perception":10,"posture":20,"resistances":{},"attack_set":"husk_v1",)"
    R"("drops":["aspect.memory","weapon.test_blade"],"biomes":["underroot"],"event_mutations":[]})";
const std::string kEvent =
    R"({"id":"event.test_raid","requirements":{},"stages":["rumour","raid","aftermath"],)"
    R"("failure_result":"burned_outpost","success_result":"tribunal_patrol","cooldown_hours":12})";

// `s` with the first `from` replaced by `to`.
std::string with(std::string s, std::string_view from, std::string_view to) {
    const std::size_t at = s.find(from);
    if (at != std::string::npos) s.replace(at, from.size(), to);
    return s;
}

// A fresh scratch tree under the system temp directory.
fs::path scratch(const std::string& name) {
    const fs::path root = fs::temp_directory_path() / ("lt_record_content_" + name);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    return root;
}

void put(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

ContentSet withAspects() {
    ContentSet c;
    c.builtinIds = {"aspect.motion", "aspect.form", "aspect.memory"};
    return c;
}

loc::StringTable english() {
    loc::StringTable t;
    t.set("item.test_blade.lore", "A blade that exists only in tests.");
    return t;
}

// True when the report has no load errors; prints them otherwise so CI logs name the bad file.
bool loadedCleanly(const ContentLoadReport& r) {
    for (const std::string& e : r.errors) std::printf("  load error: %s\n", e.c_str());
    return r.ok();
}

}  // namespace

int main() {
    TEST_SECTION("records become validator inputs");
    {
        combat::WeaponDef w;
        w.id = "weapon.test_blade";
        w.aspectTags = {"motion", "form"};
        w.loreKey = "item.test_blade.lore";
        const ContentRecord r = weaponContent(w, "data/items/a.json");
        CHECK(r.id == w.id && r.source == "data/items/a.json");
        CHECK(r.references == std::vector<std::string>({"aspect.motion", "aspect.form"}));
        CHECK(r.locKeys == std::vector<std::string>({"item.test_blade.lore"}));
        combat::EnemyDef e;
        e.id = "enemy.test_husk";
        e.drops = {"aspect.memory", "weapon.test_blade"};
        const ContentRecord er = enemyContent(e, "mods/m/enemies.json");
        CHECK(er.id == e.id && er.source == "mods/m/enemies.json" && er.references == e.drops && er.locKeys.empty());
        world::EventDef ev;
        ev.id = "event.test_raid";
        ev.stages = {"rumour", "raid", "aftermath"};
        const EventTemplate t = eventContent(ev, "data/events/a.json");
        CHECK(t.id == ev.id && t.source == "data/events/a.json" && t.stages == ev.stages);
        CHECK(t.transitions.size() == 2u && t.transitions[0].from == "rumour" && t.transitions[0].to == "raid");
        CHECK(t.transitions.size() == 2u && t.transitions[1].from == "raid" && t.transitions[1].to == "aftermath");
        CHECK(t.terminalStages == std::vector<std::string>({"aftermath"}) && t.requiresResolved.empty());
    }

    TEST_SECTION("data and mods merge in a stable order");
    {
        const fs::path root = scratch("merge");
        put(root / "data/items/swords.json", "[" + kWeapon + "]");
        put(root / "data/items/notes.txt", "not a record file");
        put(root / "data/enemies/husks.json", "[" + kEnemy + "]");
        put(root / "data/events/raids.json", "[" + kEvent + "]");
        put(root / "mods/zeta/items.json", "[" + with(kWeapon, "weapon.test_blade", "weapon.zeta_blade") + "]");
        put(root / "mods/alpha/enemies.json", "[" + with(kEnemy, "enemy.test_husk", "enemy.alpha_husk") + "]");
        put(root / "mods/readme.txt", "mods are folders");
        ContentSet c = withAspects();
        const ContentLoadReport r = addRecordContent(c, root / "data", root / "mods");
        CHECK(loadedCleanly(r));
        CHECK_EQ(r.files, 5u);
        CHECK_EQ(r.records, 5u);
        std::vector<std::string> order;
        for (const ContentRecord& rec : c.records) order.push_back(rec.id + " @ " + rec.source);
        CHECK(order == std::vector<std::string>({"weapon.test_blade @ data/items/swords.json",
                                                 "enemy.test_husk @ data/enemies/husks.json",
                                                 "enemy.alpha_husk @ mods/alpha/enemies.json",
                                                 "weapon.zeta_blade @ mods/zeta/items.json"}));
        CHECK(c.events.size() == 1u && c.events[0].id == "event.test_raid" &&
              c.events[0].source == "data/events/raids.json");
        CHECK(validateContent(c, english()).empty());
    }

    TEST_SECTION("ids are checked across data and mods");
    {
        const fs::path root = scratch("cross");
        put(root / "data/items/swords.json", "[" + kWeapon + "]");
        put(root / "mods/dup/items.json", "[" + kWeapon + "]");
        put(root / "mods/dup/enemies.json", "[" + with(kEnemy, "aspect.memory", "aspect.void") + "]");
        put(root / "mods/dup/events.json", "[" + with(kEvent, "event.test_raid", "event.mod_raid") + "]");
        ContentSet c = withAspects();
        const ContentLoadReport r = addRecordContent(c, root / "data", root / "mods");
        CHECK(loadedCleanly(r));  // each file decodes on its own; the clashes only show up after the merge
        CHECK(c.events.size() == 1u && c.events[0].source == "mods/dup/events.json");
        const auto issues = validateContent(c, english());
        auto has = [&](ContentIssueKind k, const std::string& id, const std::string& detail) {
            for (const ContentIssue& i : issues)
                if (i.kind == k && i.id == id && i.detail == detail) return true;
            return false;
        };
        CHECK_EQ(issues.size(), 2u);
        CHECK(has(ContentIssueKind::DuplicateId, "weapon.test_blade", "mods/dup/items.json"));
        CHECK(has(ContentIssueKind::UnknownId, "enemy.test_husk", "aspect.void"));
    }

    TEST_SECTION("armour and boss records join the bridge");
    {
        combat::ArmourDef a;
        a.id = "armor.test_helm";
        a.aspectTags = {"form"};
        a.loreKey = "item.test_helm.lore";
        const ContentRecord armourRec = armourContent(a, "data/armor/helms.json");
        CHECK(armourRec.id == a.id && armourRec.source == "data/armor/helms.json");
        CHECK(armourRec.references == std::vector<std::string>({"aspect.form"}));
        CHECK(armourRec.locKeys == std::vector<std::string>({"item.test_helm.lore"}));
        combat::BossDef b;
        b.id = "boss.test_warden";
        const ContentRecord bossRec = bossContent(b, "data/bosses/wardens.json");
        CHECK(bossRec.id == b.id && bossRec.source == "data/bosses/wardens.json");
        CHECK(bossRec.references.empty() && bossRec.locKeys.empty());

        const std::string armour =
            R"({"id":"armor.test_helm","display_name":"Test Helm","slot":"head","weight":3,"poise":10,)"
            R"("durability":120,"resistances":{},"sockets":["rune"],"aspect_tags":["form"],)"
            R"("lore_key":"item.test_helm.lore"})";
        const std::string boss =
            R"({"id":"boss.test_warden","display_name":"Test Warden","health":1000,"posture":200,)"
            R"("phase_thresholds":[0.5],"armour":[{"id":"armour.helm","integrity":100,"opens_route":"route.a"}],)"
            R"("attacks":[{"id":"atk.basic","windup_ms":400,"active_ms":100,"recovery_ms":500,"damage":50,)"
            R"("min_phase":1}]})";

        // Enemies drop armour by id; a mod that redefines a shipped boss is a cross-source duplicate.
        const fs::path root = scratch("armour_boss");
        put(root / "data/armor/helms.json", "[" + armour + "]");
        put(root / "data/enemies/husks.json", "[" + with(kEnemy, "weapon.test_blade", "armor.test_helm") + "]");
        put(root / "data/bosses/wardens.json", "[" + boss + "]");
        put(root / "mods/dup/bosses.json", "[" + boss + "]");
        ContentSet c = withAspects();
        const ContentLoadReport r = addRecordContent(c, root / "data", root / "mods");
        CHECK(loadedCleanly(r));
        CHECK_EQ(r.files, 4u);
        CHECK_EQ(r.records, 4u);
        std::vector<std::string> order;
        for (const ContentRecord& rec : c.records) order.push_back(rec.id + " @ " + rec.source);
        CHECK(order == std::vector<std::string>({"armor.test_helm @ data/armor/helms.json",
                                                 "enemy.test_husk @ data/enemies/husks.json",
                                                 "boss.test_warden @ data/bosses/wardens.json",
                                                 "boss.test_warden @ mods/dup/bosses.json"}));
        loc::StringTable en = english();
        en.set("item.test_helm.lore", "A helm that exists only in tests.");
        const std::vector<ContentIssue> issues = validateContent(c, en);
        for (const ContentIssue& i : issues)
            std::printf("  issue: %s %s -> %s\n", contentIssueName(i.kind), i.id.c_str(), i.detail.c_str());
        CHECK_EQ(issues.size(), 1u);
        CHECK(issues.size() == 1u && issues[0].kind == ContentIssueKind::DuplicateId &&
              issues[0].id == "boss.test_warden" && issues[0].detail == "mods/dup/bosses.json");

        // A bad armour or boss file is a per-file load error that adds nothing.
        const fs::path bad = scratch("armour_boss_broken");
        put(bad / "data/bosses/wardens.json", "[" + boss + "]");
        put(bad / "mods/m/armor.json", "[" + with(armour, "\"slot\":\"head\"", "\"slot\":\"tail\"") + "]");
        put(bad / "mods/m/bosses.json", "[" + with(boss, "[0.5]", "[1.5]") + "]");
        ContentSet broken = withAspects();
        const ContentLoadReport badReport = addRecordContent(broken, bad / "data", bad / "mods");
        CHECK(!badReport.ok());
        CHECK_EQ(badReport.files, 3u);
        CHECK_EQ(badReport.records, 1u);
        CHECK_EQ(badReport.errors.size(), 2u);
        CHECK(badReport.errors.size() == 2u &&
              badReport.errors[0] == "mods/m/armor.json: record 1 (armor.test_helm): unknown armour slot 'tail'");
        CHECK(badReport.errors.size() == 2u &&
              badReport.errors[1] == "mods/m/bosses.json: record 1 (boss.test_warden): 'phase_thresholds' entries "
                                     "must be numbers strictly between 0 and 1");
        CHECK(broken.records.size() == 1u && broken.records[0].source == "data/bosses/wardens.json");
    }

    TEST_SECTION("biome records join the bridge");
    {
        world::BiomeDef b;
        b.id = "test_tundra";
        b.enemyFamilies.push_back({"enemy.test_husk", 3});
        b.enemyFamilies.push_back({"enemy.mod_wolf", 1});
        b.events.push_back({"event.test_raid", 2});
        b.weather.push_back({"weather.snowfall", 1});
        const ContentRecord biomeRec = biomeContent(b, "data/biomes/tundra.json");
        CHECK(biomeRec.id == b.id && biomeRec.source == "data/biomes/tundra.json" && biomeRec.locKeys.empty());
        // Only the enemy family and event tables are cross-record references; weather and the rest stay local.
        CHECK(biomeRec.references ==
              std::vector<std::string>({"enemy.test_husk", "enemy.mod_wolf", "event.test_raid"}));

        const std::string biome =
            R"({"id":"test_tundra","name":"Test Tundra","palette":{"top":"snow","filler":"frozen_dirt",)"
            R"("base":"stone","lake":"ice","cave_lining":"packed_ice","filler_depth":3},)"
            R"("terrain":{"base_height":64,"amplitude":40,"valley_depth":28,"feature_size":96,"sea_level":48,)"
            R"("cave_feature_size":24,"cave_threshold":0.7,"cave_min_y":8,"cave_max_y":56},)"
            R"("enemy_families":[{"id":"enemy.test_husk","weight":3}],"events":[{"id":"event.test_raid","weight":2}]})";
        const std::string husk = with(kEnemy, R"(,"weapon.test_blade")", "");

        // Biome tables resolve against enemies and event templates from data and every mod; a mod that
        // redefines a shipped biome is a cross-source duplicate, and a family no source defines is unknown.
        const fs::path root = scratch("biome");
        put(root / "data/enemies/husks.json", "[" + husk + "]");
        put(root / "data/events/raids.json", "[" + kEvent + "]");
        put(root / "data/biomes/tundra.json", "[" + biome + "]");
        put(root / "mods/dup/biomes.json", "[" + with(biome, "enemy.test_husk", "enemy.ghost") + "]");
        put(root / "mods/wolves/enemies.json", "[" + with(husk, "enemy.test_husk", "enemy.mod_wolf") + "]");
        put(root / "mods/wolves/events.json", "[" + with(kEvent, "event.test_raid", "event.mod_raid") + "]");
        put(root / "mods/wolves/biomes.json",
            "[" +
                with(with(with(biome, "\"test_tundra\"", "\"wolf_steppe\""), "enemy.test_husk", "enemy.mod_wolf"),
                     "event.test_raid", "event.mod_raid") +
                "]");
        ContentSet c = withAspects();
        const ContentLoadReport r = addRecordContent(c, root / "data", root / "mods");
        CHECK(loadedCleanly(r));
        CHECK_EQ(r.files, 7u);
        CHECK_EQ(r.records, 7u);
        std::vector<std::string> order;
        for (const ContentRecord& rec : c.records) order.push_back(rec.id + " @ " + rec.source);
        CHECK(order == std::vector<std::string>({"enemy.test_husk @ data/enemies/husks.json",
                                                 "test_tundra @ data/biomes/tundra.json",
                                                 "test_tundra @ mods/dup/biomes.json",
                                                 "enemy.mod_wolf @ mods/wolves/enemies.json",
                                                 "wolf_steppe @ mods/wolves/biomes.json"}));
        CHECK(c.events.size() == 2u && c.events[1].id == "event.mod_raid" &&
              c.events[1].source == "mods/wolves/events.json");
        const std::vector<ContentIssue> issues = validateContent(c, english());
        for (const ContentIssue& i : issues)
            std::printf("  issue: %s %s -> %s\n", contentIssueName(i.kind), i.id.c_str(), i.detail.c_str());
        auto has = [&](ContentIssueKind k, const std::string& id, const std::string& detail) {
            for (const ContentIssue& i : issues)
                if (i.kind == k && i.id == id && i.detail == detail) return true;
            return false;
        };
        CHECK_EQ(issues.size(), 2u);
        CHECK(has(ContentIssueKind::DuplicateId, "test_tundra", "mods/dup/biomes.json"));
        CHECK(has(ContentIssueKind::UnknownId, "test_tundra", "enemy.ghost"));

        // A bad biome file is a per-file load error that adds nothing.
        const fs::path bad = scratch("biome_broken");
        put(bad / "data/biomes/tundra.json", "[" + biome + "]");
        put(bad / "mods/m/biomes.json", "[" + with(biome, R"("top":"snow")", R"("top":"lava")") + "]");
        ContentSet broken = withAspects();
        const ContentLoadReport badReport = addRecordContent(broken, bad / "data", bad / "mods");
        CHECK(!badReport.ok());
        CHECK_EQ(badReport.files, 2u);
        CHECK_EQ(badReport.records, 1u);
        CHECK(badReport.errors.size() == 1u &&
              badReport.errors[0] == "mods/m/biomes.json: record 1 (test_tundra): 'palette': 'top' must name a "
                                     "known block, not 'lava'");
        CHECK(broken.records.size() == 1u && broken.records[0].source == "data/biomes/tundra.json");
    }

    TEST_SECTION("attachment and recipe records join the bridge");
    {
        combat::AttachmentDef a;
        a.mods.id = "attachment.test_core";
        a.aspectTags = {"motion", "form"};
        a.loreKey = "item.test_core.lore";
        const ContentRecord ar = attachmentContent(a, "data/attachments/cores.json");
        CHECK(ar.id == a.mods.id && ar.source == "data/attachments/cores.json");
        CHECK(ar.references == std::vector<std::string>({"aspect.motion", "aspect.form"}));
        CHECK(ar.locKeys == std::vector<std::string>({"item.test_core.lore"}));
        world::RecipeDef rec;
        rec.id = "infusion.test_core";
        rec.output = "attachment.test_core";
        rec.cost[static_cast<std::size_t>(world::Aspect::Memory)] = 1;
        rec.cost[static_cast<std::size_t>(world::Aspect::Motion)] = 2;
        rec.requiredUnlock = "research.test_core";
        const ContentRecord rr = recipeContent(rec, "data/recipes/cores.json");
        CHECK(rr.id == rec.id && rr.source == "data/recipes/cores.json" && rr.locKeys.empty());
        // The output comes first, then every spent primal aspect in enum order; the unlock is no reference.
        CHECK(rr.references == std::vector<std::string>({"attachment.test_core", "aspect.motion", "aspect.memory"}));

        const std::string core =
            R"({"id":"attachment.test_core","display_name":"Test Core","slot":"core","weight":0.5,)"
            R"("durability":60,"aspect_tags":["motion"],"lore_key":"item.test_core.lore"})";
        const std::string recipe =
            R"({"id":"infusion.test_core","output":"attachment.test_core","cost":{"motion":2,"memory":1},)"
            R"("flux":12,"risk":15,"unlock":"research.test_core"})";
        auto renamed = [](const std::string& s, std::string_view id, std::string_view out) {
            return with(with(s, "infusion.test_core", id), "attachment.test_core", out);
        };

        // Recipe outputs resolve against attachments from data and every mod, so a mod recipe may craft a
        // modded attachment; an output no source defines or an aspect nobody provides is unknown, and an
        // attachment whose lore key the English table lacks is a missing loc key.
        const fs::path root = scratch("attachment");
        put(root / "data/attachments/cores.json", "[" + core + "]");
        put(root / "data/recipes/cores.json", "[" + recipe + "]");
        put(root / "mods/lenses/attachments.json",
            "[" + with(with(core, "attachment.test_core", "attachment.mod_lens"), "item.test_core.lore", "item.mod_lens.lore") +
                "]");
        put(root / "mods/lenses/recipes.json",
            "[" + renamed(recipe, "infusion.mod_lens", "attachment.mod_lens") + "," +
                renamed(recipe, "infusion.ghost", "attachment.ghost") + "," +
                with(with(recipe, "infusion.test_core", "infusion.dim"), R"("memory":1)", R"("light":1)") + "]");
        ContentSet c = withAspects();
        const ContentLoadReport r = addRecordContent(c, root / "data", root / "mods");
        CHECK(loadedCleanly(r));
        CHECK_EQ(r.files, 4u);
        CHECK_EQ(r.records, 6u);
        std::vector<std::string> order;
        for (const ContentRecord& cr : c.records) order.push_back(cr.id + " @ " + cr.source);
        CHECK(order == std::vector<std::string>({"attachment.test_core @ data/attachments/cores.json",
                                                 "infusion.test_core @ data/recipes/cores.json",
                                                 "attachment.mod_lens @ mods/lenses/attachments.json",
                                                 "infusion.mod_lens @ mods/lenses/recipes.json",
                                                 "infusion.ghost @ mods/lenses/recipes.json",
                                                 "infusion.dim @ mods/lenses/recipes.json"}));
        loc::StringTable en = english();
        en.set("item.test_core.lore", "A core that exists only in tests.");
        const std::vector<ContentIssue> issues = validateContent(c, en);
        for (const ContentIssue& i : issues)
            std::printf("  issue: %s %s -> %s\n", contentIssueName(i.kind), i.id.c_str(), i.detail.c_str());
        auto has = [&](ContentIssueKind k, const std::string& id, const std::string& detail) {
            for (const ContentIssue& i : issues)
                if (i.kind == k && i.id == id && i.detail == detail) return true;
            return false;
        };
        CHECK_EQ(issues.size(), 3u);
        CHECK(has(ContentIssueKind::UnknownId, "infusion.ghost", "attachment.ghost"));
        CHECK(has(ContentIssueKind::UnknownId, "infusion.dim", "aspect.light"));
        CHECK(has(ContentIssueKind::MissingLocKey, "attachment.mod_lens", "item.mod_lens.lore"));

        // A bad attachment or recipe file is a per-file load error that adds nothing.
        const fs::path bad = scratch("attachment_broken");
        put(bad / "data/attachments/cores.json", "[" + core + "]");
        put(bad / "data/recipes/cores.json", "[" + recipe + "]");
        put(bad / "mods/a/attachments.json", "[" + with(core, R"("slot":"core")", R"("slot":"barrel")") + "]");
        put(bad / "mods/b/recipes.json", "[" + with(recipe, R"("memory":1)", R"("void":1)") + "]");
        ContentSet broken = withAspects();
        const ContentLoadReport badReport = addRecordContent(broken, bad / "data", bad / "mods");
        CHECK(!badReport.ok());
        CHECK_EQ(badReport.files, 4u);
        CHECK_EQ(badReport.records, 2u);
        CHECK(badReport.errors ==
              std::vector<std::string>(
                  {"mods/a/attachments.json: record 1 (attachment.test_core): unknown socket slot 'barrel'",
                   "mods/b/recipes.json: record 1 (infusion.test_core): 'cost' names unknown aspect 'void'"}));
        CHECK(broken.records.size() == 2u && broken.records[0].source == "data/attachments/cores.json" &&
              broken.records[1].source == "data/recipes/cores.json");
    }

    TEST_SECTION("a file that fails decoding adds nothing");
    {
        const fs::path root = scratch("broken");
        put(root / "data/items/swords.json", "[" + kWeapon + "]");
        put(root / "data/enemies/bad.json", "[" + with(kEnemy, R"("role":"brute")", R"("role":"giant")") + "]");
        put(root / "mods/m/items.json", "[" + with(kWeapon, "weapon.test_blade", "weapon.zeta_blade") + "," +
                                            with(kWeapon, R"("family":"longsword")", R"("family":"spoon")") + "]");
        put(root / "mods/m/events.json", "[\n");
        ContentSet c = withAspects();
        const ContentLoadReport r = addRecordContent(c, root / "data", root / "mods");
        CHECK(!r.ok());
        CHECK_EQ(r.files, 4u);
        CHECK_EQ(r.records, 1u);
        CHECK_EQ(r.errors.size(), 3u);
        CHECK(r.errors.size() == 3u &&
              r.errors[0] == "data/enemies/bad.json: record 1 (enemy.test_husk): unknown role 'giant'");
        CHECK(r.errors.size() == 3u &&
              r.errors[1] == "mods/m/items.json: record 2 (weapon.test_blade): unknown family 'spoon'");
        CHECK(r.errors.size() == 3u && r.errors[2].rfind("mods/m/events.json:2:1: ", 0) == 0);
        CHECK(c.records.size() == 1u && c.records[0].id == "weapon.test_blade");
        CHECK(c.events.empty());
    }

    TEST_SECTION("missing roots");
    {
        const fs::path root = scratch("missing");
        ContentSet c;
        const ContentLoadReport r = addRecordContent(c, root / "data", root / "mods");
        CHECK(r.errors.size() == 1u &&
              r.errors[0] == (root / "data").generic_string() + ": data root is not a directory");
        CHECK(r.files == 0u && r.records == 0u && c.records.empty() && c.events.empty());
        fs::create_directories(root / "data");
        // No kind folders and no mods root is a clean, empty merge.
        const ContentLoadReport empty = addRecordContent(c, root / "data", root / "mods");
        CHECK(loadedCleanly(empty) && empty.files == 0u && c.records.empty());
    }

#ifdef LT_DATA_DIR
    TEST_SECTION("shipped data loads through the bridge");
    {
        ContentSet c = withAspects();
        const ContentLoadReport r = addRecordContent(c, LT_DATA_DIR, scratch("shipped") / "mods");
        CHECK(loadedCleanly(r));
        CHECK(r.files >= 3u && !c.records.empty() && !c.events.empty());
        for (const ContentRecord& rec : c.records) CHECK(rec.source.rfind("data/", 0) == 0);
        for (const EventTemplate& e : c.events) CHECK(e.source.rfind("data/events/", 0) == 0);
    }

    TEST_SECTION("shipped data passes the full content gate");
    {
        // All six primal aspects (tworld/Research.h) are engine-provided; English is the source table.
        ContentSet c;
        c.builtinIds = {"aspect.motion", "aspect.form",  "aspect.life",
                        "aspect.decay",  "aspect.light", "aspect.memory"};
        const ContentLoadReport r = addRecordContent(c, LT_DATA_DIR, scratch("gate") / "mods");
        CHECK(loadedCleanly(r));
        std::ifstream in(fs::path(LT_DATA_DIR) / "loc" / "en.txt", std::ios::binary);
        std::string text, error;
        std::getline(in, text, '\0');
        loc::StringTable en;
        const bool parsed = en.parse(text, &error);
        if (!parsed) std::printf("  data/loc/en.txt: %s\n", error.c_str());
        CHECK(!text.empty() && parsed);
        // Desktop and Android C31 tiers: the shipped set must be mergeable on both.
        const TextureBudget tiers[] = {desktopTextureBudget(), androidTextureBudget()};
        for (const TextureBudget& tier : tiers) {
            const std::vector<ContentIssue> issues = validateContent(c, en, tier);
            for (const ContentIssue& i : issues)
                std::printf("  gate issue: %s %s -> %s\n", contentIssueName(i.kind), i.id.c_str(), i.detail.c_str());
            CHECK(issues.empty());
        }
    }
#endif

    return lt::test::finish("record_content");
}
