#pragma once
// Legend Engine T - TTools content validator (Section 25). Runs in CI before an agent's
// content merge over base data and mods/<id>/ records, reporting duplicate IDs, unknown ID
// references and missing localisation keys. Issue order is deterministic so CI diffs stay stable.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "tcore/Localization.h"

namespace lt::tools {

enum class ContentIssueKind : std::uint8_t {
    DuplicateId = 0,
    UnknownId,
    MissingLocKey,
    RecipeCycle,
    OversizedTexture,
    EventDeadlock,
    SaveIncompatible
};

inline const char* contentIssueName(ContentIssueKind k) noexcept {
    switch (k) {
        case ContentIssueKind::DuplicateId: return "duplicate_id";
        case ContentIssueKind::UnknownId: return "unknown_id";
        case ContentIssueKind::MissingLocKey: return "missing_loc_key";
        case ContentIssueKind::RecipeCycle: return "recipe_cycle";
        case ContentIssueKind::OversizedTexture: return "oversized_texture";
        case ContentIssueKind::EventDeadlock: return "event_deadlock";
        case ContentIssueKind::SaveIncompatible: return "save_incompatible";
    }
    return "unknown";
}

struct ContentIssue {
    ContentIssueKind kind = ContentIssueKind::UnknownId;
    std::string id;     // record or recipe that raised the issue
    std::string detail; // offending id, key or source file
};

// A data record as the validator sees it: stable id, origin, referenced ids, needed loc keys.
struct ContentRecord {
    std::string id;
    std::string source; // e.g. "data/items/vertical_slice_weapons.json" or "mods/<id>/items.json"
    std::vector<std::string> references;
    std::vector<std::string> locKeys;
};

struct Recipe {
    std::string id;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
};

// Texture asset listed by base data or a mod (textures/*.ktx2), checked against platform limits.
struct TextureAsset {
    std::string id;
    std::string source;
    int width = 0, height = 0;
    std::size_t bytes = 0; // compressed payload size on disk
};

// Per-platform texture limits (Sections 15, 23): Android C31 runs GLES 3 under a 1.5 GB ceiling.
struct TextureBudget {
    const char* name = "desktop";
    int maxDimension = 4096;
    std::size_t maxBytes = 32u * 1024u * 1024u;
};

inline TextureBudget desktopTextureBudget() { return {}; }
inline TextureBudget androidTextureBudget() { return {"android_c31", 1024, 4u * 1024u * 1024u}; }

// Event template lifecycle graph (Sections 5, 21). A template deadlocks when a stage reachable
// from its entry stage can never reach a terminal stage, or when its start prerequisites can
// never all be resolved (unknown event ids or a prerequisite cycle).
struct EventTransition {
    std::string from;
    std::string to;
};

struct EventTemplate {
    std::string id;
    std::string source;
    std::vector<std::string> stages; // first entry is the entry stage, e.g. "rumour"
    std::vector<EventTransition> transitions;
    std::vector<std::string> terminalStages;   // e.g. "aftermath"
    std::vector<std::string> requiresResolved; // events that must resolve before this one starts
};

// Save compatibility contract (Sections 14, 25): no source may target a newer save schema than
// the build writes, and every id persisted in shipped saves must still resolve after the merge.
struct SaveCompat {
    std::uint32_t buildSchemaVersion = 1;
    std::map<std::string, std::uint32_t> sourceSchemaVersion; // data file or mods/<id> -> schema
    std::set<std::string> savedIds;                           // ids referenced by existing saves
};

struct ContentSet {
    std::vector<ContentRecord> records;
    std::vector<Recipe> recipes;
    std::set<std::string> builtinIds; // engine-provided ids such as aspect.motion
    std::vector<TextureAsset> textures;
    std::vector<EventTemplate> events;
    SaveCompat save;
};

inline std::size_t countIssues(const std::vector<ContentIssue>& v, ContentIssueKind k) {
    return static_cast<std::size_t>(
        std::count_if(v.begin(), v.end(), [k](const ContentIssue& i) { return i.kind == k; }));
}

// Duplicate ids, plus record references and recipe inputs/outputs that name no known id.
inline void checkIds(const ContentSet& c, std::vector<ContentIssue>& out) {
    std::set<std::string> known = c.builtinIds;
    for (const auto& r : c.records)
        if (!known.insert(r.id).second) out.push_back({ContentIssueKind::DuplicateId, r.id, r.source});
    std::set<std::string> recipeIds;
    for (const auto& rc : c.recipes)
        if (!recipeIds.insert(rc.id).second) out.push_back({ContentIssueKind::DuplicateId, rc.id, "recipe"});
    auto need = [&](const std::string& owner, const std::string& ref) {
        if (!known.count(ref)) out.push_back({ContentIssueKind::UnknownId, owner, ref});
    };
    // Record references also resolve against event templates, so a biome's event table (Section 4.2) can name
    // shipped or modded events; duplicate event ids are reported by checkEventDeadlocks, not here.
    std::set<std::string> referable = known;
    for (const auto& e : c.events) referable.insert(e.id);
    for (const auto& r : c.records)
        for (const auto& ref : r.references)
            if (!referable.count(ref)) out.push_back({ContentIssueKind::UnknownId, r.id, ref});
    for (const auto& rc : c.recipes) {
        for (const auto& in : rc.inputs) need(rc.id, in);
        for (const auto& o : rc.outputs) need(rc.id, o);
    }
}

// Every loc key a record needs must be a stable key present in the English source table.
inline void checkLocKeys(const ContentSet& c, const loc::StringTable& source,
                         std::vector<ContentIssue>& out) {
    for (const auto& r : c.records)
        for (const auto& k : r.locKeys) {
            if (!loc::StringTable::validKey(k))
                out.push_back({ContentIssueKind::MissingLocKey, r.id, "invalid key " + k});
            else if (!source.has(k))
                out.push_back({ContentIssueKind::MissingLocKey, r.id, k});
        }
}

// Recipe cycles: an item craftable from itself, directly or through intermediate recipes, lets
// crafting loop forever. Edges run input -> output; each DFS back edge closes a cycle, reported
// once in canonical rotation (smallest id first) so CI output is stable.
inline void checkRecipeCycles(const ContentSet& c, std::vector<ContentIssue>& out) {
    std::map<std::string, std::set<std::string>> graph;
    for (const auto& rc : c.recipes)
        for (const auto& in : rc.inputs)
            for (const auto& o : rc.outputs) graph[in].insert(o);
    std::map<std::string, std::uint8_t> state; // 0 = unvisited, 1 = on DFS path, 2 = finished
    std::vector<std::string> path;
    std::set<std::string> reported;
    auto dfs = [&](auto& self, const std::string& node) -> void {
        state[node] = 1;
        path.push_back(node);
        if (const auto it = graph.find(node); it != graph.end())
            for (const auto& next : it->second) {
                const std::uint8_t s = state[next];
                if (s == 0) {
                    self(self, next);
                    continue;
                }
                if (s != 1) continue;
                std::vector<std::string> cycle(std::find(path.begin(), path.end(), next), path.end());
                std::rotate(cycle.begin(), std::min_element(cycle.begin(), cycle.end()), cycle.end());
                std::string detail;
                for (const auto& n : cycle) detail += n + " -> ";
                detail += cycle.front();
                if (reported.insert(detail).second)
                    out.push_back({ContentIssueKind::RecipeCycle, cycle.front(), detail});
            }
        path.pop_back();
        state[node] = 2;
    };
    for (const auto& entry : graph)
        if (state[entry.first] == 0) dfs(dfs, entry.first);
}

// Oversized textures (Section 25): either dimension or the compressed payload exceeds the
// platform texture budget. CI runs this once per shipping tier (desktop, android_c31).
inline void checkTextures(const ContentSet& c, const TextureBudget& budget, std::vector<ContentIssue>& out) {
    const std::string tier = budget.name;
    for (const auto& t : c.textures) {
        if (t.width <= 0 || t.height <= 0) {
            out.push_back({ContentIssueKind::OversizedTexture, t.id, "invalid dimensions in " + t.source});
            continue;
        }
        if (t.width > budget.maxDimension || t.height > budget.maxDimension)
            out.push_back({ContentIssueKind::OversizedTexture, t.id,
                           std::to_string(t.width) + "x" + std::to_string(t.height) + " exceeds " + tier +
                               " limit " + std::to_string(budget.maxDimension)});
        if (t.bytes > budget.maxBytes)
            out.push_back({ContentIssueKind::OversizedTexture, t.id,
                           std::to_string(t.bytes) + " bytes exceeds " + tier + " limit " +
                               std::to_string(budget.maxBytes)});
    }
}

// Save incompatibilities (Sections 14, 25): a data file or mod targeting a newer save schema than
// this build writes cannot be loaded, and an id persisted by existing saves that no longer
// resolves after the merge would orphan items, entities or event state on reload.
inline void checkSaveCompat(const ContentSet& c, std::vector<ContentIssue>& out) {
    for (const auto& [src, version] : c.save.sourceSchemaVersion)
        if (version > c.save.buildSchemaVersion)
            out.push_back({ContentIssueKind::SaveIncompatible, src,
                           "save schema " + std::to_string(version) + " is newer than build schema " +
                               std::to_string(c.save.buildSchemaVersion)});
    std::set<std::string> known = c.builtinIds;
    for (const auto& r : c.records) known.insert(r.id);
    for (const auto& rc : c.recipes) known.insert(rc.id);
    for (const auto& t : c.textures) known.insert(t.id);
    for (const auto& e : c.events) known.insert(e.id);
    for (const auto& id : c.save.savedIds)
        if (!known.count(id))
            out.push_back({ContentIssueKind::SaveIncompatible, id, "persisted id no longer resolves"});
}

// Event deadlocks (Sections 5, 21, 25): every event template must be able to finish its
// rumour -> ... -> aftermath lifecycle. A template deadlocks when it has no stages or no
// terminal stage, a transition or terminal names an unknown stage, or a stage reachable from
// the entry stage can never reach a terminal stage. Start prerequisites must name events that
// can themselves resolve: unknown ids, prerequisite cycles and prerequisites with a deadlocked
// lifecycle block the dependent event forever. Resolvability is computed as a fixed point so
// transitive blocks are reported too; duplicate event ids are reported as DuplicateId.
inline void checkEventDeadlocks(const ContentSet& c, std::vector<ContentIssue>& out) {
    using Graph = std::map<std::string, std::set<std::string>>;
    auto add = [&](const std::string& id, const std::string& d) {
        out.push_back({ContentIssueKind::EventDeadlock, id, d});
    };
    // Every node reachable from the frontier through g (frontier included).
    auto closure = [](std::vector<std::string> frontier, const Graph& g) {
        std::set<std::string> seen(frontier.begin(), frontier.end());
        while (!frontier.empty()) {
            const std::string n = frontier.back();
            frontier.pop_back();
            if (const auto it = g.find(n); it != g.end())
                for (const auto& m : it->second)
                    if (seen.insert(m).second) frontier.push_back(m);
        }
        return seen;
    };
    auto lifecycleOk = [&](const EventTemplate& e) {
        if (e.stages.empty()) {
            add(e.id, "no stages in " + e.source);
            return false;
        }
        bool ok = true;
        const std::set<std::string> stages(e.stages.begin(), e.stages.end());
        Graph fwd, rev;
        for (const auto& t : e.transitions) {
            if (!stages.count(t.from) || !stages.count(t.to)) {
                add(e.id, "transition " + t.from + " -> " + t.to + " names an unknown stage");
                ok = false;
                continue;
            }
            fwd[t.from].insert(t.to);
            rev[t.to].insert(t.from);
        }
        std::vector<std::string> terminals;
        for (const auto& s : e.terminalStages) {
            if (stages.count(s)) {
                terminals.push_back(s);
                continue;
            }
            add(e.id, "terminal stage " + s + " is not a stage");
            ok = false;
        }
        if (terminals.empty()) {
            add(e.id, "no terminal stage");
            return false;
        }
        const std::set<std::string> reachable = closure({e.stages.front()}, fwd);
        const std::set<std::string> canFinish = closure(terminals, rev);
        for (const auto& s : e.stages) {
            if (!reachable.count(s) || canFinish.count(s)) continue;
            add(e.id, "stage " + s + " cannot reach a terminal stage");
            ok = false;
        }
        return ok;
    };
    std::set<std::string> eventIds, stuck;
    for (const auto& e : c.events) {
        if (!eventIds.insert(e.id).second) out.push_back({ContentIssueKind::DuplicateId, e.id, e.source});
        if (!lifecycleOk(e)) stuck.insert(e.id);
    }
    std::set<std::string> resolvable;
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& e : c.events) {
            if (stuck.count(e.id) || resolvable.count(e.id)) continue;
            const bool ready = std::all_of(e.requiresResolved.begin(), e.requiresResolved.end(),
                                           [&](const std::string& p) { return resolvable.count(p) > 0; });
            if (!ready) continue;
            resolvable.insert(e.id);
            changed = true;
        }
    }
    for (const auto& e : c.events) {
        std::string blocked;
        for (const auto& p : e.requiresResolved)
            if (!resolvable.count(p)) blocked += (eventIds.count(p) ? " " : " unknown ") + p;
        if (!blocked.empty()) add(e.id, "start blocked by" + blocked);
    }
}

// CI entry point: runs every check and returns the combined issue list (empty = mergeable).
// Texture limits default to the desktop tier; pass androidTextureBudget() for the C31 pass.
inline std::vector<ContentIssue> validateContent(const ContentSet& c, const loc::StringTable& source,
                                                 const TextureBudget& textures = desktopTextureBudget()) {
    std::vector<ContentIssue> out;
    checkIds(c, out);
    checkLocKeys(c, source, out);
    checkRecipeCycles(c, out);
    checkTextures(c, textures, out);
    checkSaveCompat(c, out);
    checkEventDeadlocks(c, out);
    return out;
}

} // namespace lt::tools
