#pragma once
// Legend Engine T - TTools record bridge (Sections 21, 25): feeds the strictly decoded item, enemy and
// event records into the content validator, so the CI gate checks shipped data/ and every mods/<id>/
// record set together. Weapons reference their aspects ("motion" -> aspect.motion) and need their lore
// key; enemies reference their drops; events become lifecycle templates whose authored stages run in
// order and must reach "aftermath". A file that fails decoding is a load error and adds nothing, so a
// broken file never half-merges.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "tcombat/ArmourRecord.h"
#include "tcombat/AttachmentRecord.h"
#include "tcombat/BossRecord.h"
#include "tcombat/EnemyRecord.h"
#include "tcombat/WeaponRecord.h"
#include "tcore/JsonParser.h"
#include "ttools/ContentValidator.h"
#include "tworld/BiomeRecord.h"
#include "tworld/EventRecord.h"
#include "tworld/RecipeRecord.h"

namespace lt::tools {

inline ContentRecord weaponContent(const combat::WeaponDef& w, const std::string& source) {
    ContentRecord r{w.id, source, {}, {}};
    for (const std::string& tag : w.aspectTags) r.references.push_back("aspect." + tag);
    if (!w.loreKey.empty()) r.locKeys.push_back(w.loreKey);
    return r;
}

inline ContentRecord enemyContent(const combat::EnemyDef& e, const std::string& source) {
    return {e.id, source, e.drops, {}};
}

// Armour (Section 12) references its aspects like weapons do and needs its lore key, so enemy drops such
// as armor.frostbound_plate resolve against the shipped armour records.
inline ContentRecord armourContent(const combat::ArmourDef& a, const std::string& source) {
    ContentRecord r{a.id, source, {}, {}};
    for (const std::string& tag : a.aspectTags) r.references.push_back("aspect." + tag);
    if (!a.loreKey.empty()) r.locKeys.push_back(a.loreKey);
    return r;
}

// Bosses (Section 10) join the cross-source id check, so a mod cannot silently redefine boss.frost_warden.
// Armour piece and attack ids are record-local and "opens_route" names an arena route rather than content,
// so a boss adds no references; "display_name" stays in the record as a weapon's does, so no loc key.
inline ContentRecord bossContent(const combat::BossDef& b, const std::string& source) {
    return {b.id, source, {}, {}};
}

// Biomes (Sections 4.2, 14, 25) join the cross-source id check and reference the enemy families and
// autonomous events they spawn, so a biome table never names an enemy or event that no data or mod defines.
// Ores, vegetation, weather, ambient audio, ruin kits and boss modifiers are record-local tables, and "name"
// stays in the record as a boss's display name does, so a biome adds no loc key.
inline ContentRecord biomeContent(const world::BiomeDef& b, const std::string& source) {
    ContentRecord r{b.id, source, {}, {}};
    for (const world::WeightedId& e : b.enemyFamilies) r.references.push_back(e.id);
    for (const world::WeightedId& e : b.events) r.references.push_back(e.id);
    return r;
}

// Attachments (Sections 7, 12) reference their aspects like weapons and armour do and need their lore key, so
// infusion outputs and drops such as attachment.aether_core resolve against the shipped attachment records.
// Socket slots and compatible families are engine enums checked while decoding, so they add no references.
inline ContentRecord attachmentContent(const combat::AttachmentDef& a, const std::string& source) {
    ContentRecord r{a.mods.id, source, {}, {}};
    for (const std::string& tag : a.aspectTags) r.references.push_back("aspect." + tag);
    if (!a.loreKey.empty()) r.locKeys.push_back(a.loreKey);
    return r;
}

// Infusion recipes (Section 8) join the cross-source id check and reference the item they make plus every
// primal aspect they spend, so a recipe never crafts an item or costs an aspect that no data or mod defines.
// "unlock" names a Thaumonomicon entry rather than a content record, so it adds no reference.
inline ContentRecord recipeContent(const world::RecipeDef& rec, const std::string& source) {
    ContentRecord r{rec.id, source, {rec.output}, {}};
    for (std::size_t n = 0; n < world::kPrimalAspectCount; ++n)
        if (rec.cost[n] > 0)
            r.references.push_back("aspect." + std::string(world::toString(static_cast<world::Aspect>(n))));
    return r;
}

// Stages advance in authored order; the template is terminal only once it reaches "aftermath".
inline EventTemplate eventContent(const world::EventDef& e, const std::string& source) {
    EventTemplate t;
    t.id = e.id;
    t.source = source;
    t.stages = e.stages;
    for (std::size_t i = 1; i < e.stages.size(); ++i) t.transitions.push_back({e.stages[i - 1], e.stages[i]});
    t.terminalStages.push_back(world::toString(world::EventState::Aftermath));
    return t;
}

enum class RecordFile : std::uint8_t { Items, Armour, Enemies, Bosses, Events, Biomes, Attachments, Recipes };
inline constexpr RecordFile kRecordFiles[] = {RecordFile::Items,       RecordFile::Armour, RecordFile::Enemies,
                                              RecordFile::Bosses,      RecordFile::Events, RecordFile::Biomes,
                                              RecordFile::Attachments, RecordFile::Recipes};

// Folder under data/ and file stem under mods/<id>/ for each record kind (Sections 14, 25). Armour keeps the
// "armor" spelling of its data folder and mod file (data/armor/*.json, mods/<id>/armor.json); bosses
// (Section 10) load from data/bosses/*.json and mods/<id>/bosses.json, biomes (Section 4.2) from
// data/biomes/*.json and mods/<id>/biomes.json, attachments (Sections 7, 12) from data/attachments/*.json and
// mods/<id>/attachments.json, and infusion recipes (Section 8) from data/recipes/*.json and mods/<id>/recipes.json.
inline const char* recordFileStem(RecordFile k) noexcept {
    switch (k) {
        case RecordFile::Items: return "items";
        case RecordFile::Armour: return "armor";
        case RecordFile::Enemies: return "enemies";
        case RecordFile::Bosses: return "bosses";
        case RecordFile::Events: return "events";
        case RecordFile::Biomes: return "biomes";
        case RecordFile::Attachments: return "attachments";
        case RecordFile::Recipes: return "recipes";
    }
    return "";
}

struct ContentLoadReport {
    std::size_t files = 0, records = 0;
    std::vector<std::string> errors;  // "source: record N (id): message" or "source:line:col: message"

    bool ok() const noexcept { return errors.empty(); }
};

// Decodes one record file and appends its records to `c`; `source` names it in issues and errors.
inline bool addRecordFile(ContentSet& c, RecordFile kind, const std::filesystem::path& path,
                          const std::string& source, ContentLoadReport& report) {
    ++report.files;
    const core::JsonParseResult p = core::loadJsonFile(path);
    std::string error = p.describe(source);
    std::size_t added = 0;
    if (p.ok() && kind == RecordFile::Items) {
        const combat::WeaponLoadResult r = combat::decodeWeaponRecords(p.value, source);
        for (const auto& w : r.weapons) c.records.push_back(weaponContent(w, source));
        error = r.error;
        added = r.weapons.size();
    } else if (p.ok() && kind == RecordFile::Armour) {
        const combat::ArmourLoadResult r = combat::decodeArmourRecords(p.value, source);
        for (const auto& a : r.armour) c.records.push_back(armourContent(a, source));
        error = r.error;
        added = r.armour.size();
    } else if (p.ok() && kind == RecordFile::Enemies) {
        const combat::EnemyLoadResult r = combat::decodeEnemyRecords(p.value, source);
        for (const auto& e : r.enemies) c.records.push_back(enemyContent(e, source));
        error = r.error;
        added = r.enemies.size();
    } else if (p.ok() && kind == RecordFile::Bosses) {
        const combat::BossLoadResult r = combat::decodeBossRecords(p.value, source);
        for (const auto& b : r.bosses) c.records.push_back(bossContent(b, source));
        error = r.error;
        added = r.bosses.size();
    } else if (p.ok() && kind == RecordFile::Biomes) {
        const world::BiomeLoadResult r = world::decodeBiomeRecords(p.value, source);
        for (const auto& b : r.biomes) c.records.push_back(biomeContent(b, source));
        error = r.error;
        added = r.biomes.size();
    } else if (p.ok() && kind == RecordFile::Attachments) {
        const combat::AttachmentLoadResult r = combat::decodeAttachmentRecords(p.value, source);
        for (const auto& a : r.attachments) c.records.push_back(attachmentContent(a, source));
        error = r.error;
        added = r.attachments.size();
    } else if (p.ok() && kind == RecordFile::Recipes) {
        const world::RecipeLoadResult r = world::decodeRecipeRecords(p.value, source);
        for (const auto& rec : r.recipes) {
            const ContentRecord cr = recipeContent(rec, source);
            // Section 25 recipe-cycle check: the spent primal aspects are the inputs and the crafted item the
            // output, so checkRecipeCycles runs over shipped and modded infusion recipes, not only test sets.
            Recipe edge{rec.id, {}, {rec.output}};
            edge.inputs.assign(cr.references.begin() + 1, cr.references.end());
            c.recipes.push_back(edge);
            c.records.push_back(cr);
        }
        error = r.error;
        added = r.recipes.size();
    } else if (p.ok()) {
        const world::EventLoadResult r = world::decodeEventRecords(p.value, source);
        for (const auto& e : r.events) c.events.push_back(eventContent(e, source));
        error = r.error;
        added = r.events.size();
    }
    if (!error.empty()) {
        report.errors.push_back(std::move(error));
        return false;
    }
    report.records += added;
    return true;
}

namespace detail {
// Entries directly in `dir` (sub-directories, or regular *.json files), sorted by name so record order
// and CI output stay stable on every platform. A missing or unreadable `dir` yields nothing.
inline std::vector<std::filesystem::path> sortedEntries(const std::filesystem::path& dir, bool directories) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code typeEc;
        if (directories ? it->is_directory(typeEc)
                        : it->is_regular_file(typeEc) && it->path().extension() == ".json")
            out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}
}  // namespace detail

// Base data (Section 14 layout): data/items/*.json, data/enemies/*.json and data/events/*.json, with
// sources named "data/<kind>/<file>". A missing kind folder adds nothing; a missing data root is an error.
inline void addDataRecords(ContentSet& c, const std::filesystem::path& dataRoot, ContentLoadReport& report) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dataRoot, ec)) {
        report.errors.push_back(dataRoot.generic_string() + ": data root is not a directory");
        return;
    }
    for (RecordFile kind : kRecordFiles) {
        const std::string stem = recordFileStem(kind);
        for (const std::filesystem::path& file : detail::sortedEntries(dataRoot / stem, false))
            addRecordFile(c, kind, file, "data/" + stem + "/" + file.filename().generic_string(), report);
    }
}

// One mod (Section 25): mods/<id>/items.json, enemies.json and events.json, each optional, with sources
// named "mods/<id>/<kind>.json".
inline void addModRecords(ContentSet& c, const std::filesystem::path& modDir, const std::string& modId,
                          ContentLoadReport& report) {
    for (RecordFile kind : kRecordFiles) {
        const std::string file = std::string(recordFileStem(kind)) + ".json";
        const std::filesystem::path path = modDir / file;
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) addRecordFile(c, kind, path, "mods/" + modId + "/" + file, report);
    }
}

// Every mods/<id>/ folder in id order; the folder name is the mod id. No mods root means no mods.
inline void addAllModRecords(ContentSet& c, const std::filesystem::path& modsRoot, ContentLoadReport& report) {
    for (const std::filesystem::path& dir : detail::sortedEntries(modsRoot, true))
        addModRecords(c, dir, dir.filename().generic_string(), report);
}

// CI gate input: base data first, then every mod, merged into one ContentSet so duplicate and unknown
// ids are checked across sources. Run validateContent(c, ...) only when the report is ok().
inline ContentLoadReport addRecordContent(ContentSet& c, const std::filesystem::path& dataRoot,
                                          const std::filesystem::path& modsRoot) {
    ContentLoadReport report;
    addDataRecords(c, dataRoot, report);
    addAllModRecords(c, modsRoot, report);
    return report;
}

}  // namespace lt::tools
