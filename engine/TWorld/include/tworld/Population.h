#pragma once
// Legend Engine T - design doc section 5: world population and autonomous events.
// Every region receives a population budget and a story budget. The filler never places random
// noise: it only picks events compatible with biome, faction, season, recent player history and
// nearby landmarks. Picks are deterministic for a region seed; spawned events enter the
// EventLedger as rumours so the normal lifecycle (rumour -> ... -> aftermath) takes over.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "tcore/Random.h"
#include "tworld/Event.h"

namespace lt::world {

enum class Season : std::uint8_t { Spring = 0, Summer, Autumn, Winter };

inline const char* toString(Season s) {
    switch (s) {
        case Season::Spring: return "spring";
        case Season::Summer: return "summer";
        case Season::Autumn: return "autumn";
        case Season::Winter: return "winter";
    }
    return "unknown";
}

inline constexpr std::uint64_t kDaysPerSeason = 30;
inline Season seasonForDay(std::uint64_t day) noexcept {
    return static_cast<Season>((day / kDaysPerSeason) % 4);
}

// The Event record plus the extra constraints the filler must satisfy before placing it.
struct FillerTemplate {
    EventDef def;
    std::vector<Season> seasons;         // empty = any season
    std::vector<std::string> landmarks;  // one must be nearby; empty = no landmark needed
    std::uint32_t storyCost = 1;         // story points consumed when placed
    std::uint32_t populationCost = 0;    // actors the event spawns (caravan, bandits, wyverns...)
    std::uint32_t weight = 1;
};

struct RegionContext {
    std::int32_t regionX = 0, regionZ = 0;
    std::string biome;
    std::vector<std::string> factions;      // factions present in the region
    Season season = Season::Spring;
    std::vector<std::string> landmarks;     // nearby landmark ids
    std::vector<std::string> recentEvents;  // event ids the player recently witnessed
};

struct RegionBudget {
    std::uint32_t population = 0;  // remaining actor slots
    std::uint32_t story = 0;       // remaining story points
};

inline constexpr std::uint64_t kBudgetSalt = 0x4255444745540000ull;  // "BUDGET"
inline constexpr std::uint64_t kFillerSalt = 0x46494C4C45520000ull;  // "FILLER"

// Deterministic per-region budget: base values varied by up to +/-25% from the region seed.
inline RegionBudget makeRegionBudget(std::uint32_t basePopulation, std::uint32_t baseStory,
                                     std::uint64_t regionSeed) {
    core::Rng rng(core::hashCombine(regionSeed, kBudgetSalt));
    auto vary = [&rng](std::uint32_t base) -> std::uint32_t {
        const std::uint32_t spread = base / 4;
        if (spread == 0) return base;
        return base - spread + static_cast<std::uint32_t>(rng.below(2ull * spread + 1));
    };
    RegionBudget b;
    b.population = vary(basePopulation);
    b.story = vary(baseStory);
    return b;
}

// True when the template may be placed in this region now (budget is checked by the filler).
inline bool eventFits(const FillerTemplate& t, const RegionContext& ctx, const EventLedger& ledger,
                      std::uint64_t nowHour) {
    auto has = [](const std::vector<std::string>& v, const std::string& s) {
        return std::find(v.begin(), v.end(), s) != v.end();
    };
    const EventDef& d = t.def;
    if (!d.biomes.empty() && !has(d.biomes, ctx.biome)) return false;
    if (!d.faction.empty() && !has(ctx.factions, d.faction)) return false;
    if (!t.seasons.empty() &&
        std::find(t.seasons.begin(), t.seasons.end(), ctx.season) == t.seasons.end())
        return false;
    if (!t.landmarks.empty() &&
        std::none_of(t.landmarks.begin(), t.landmarks.end(),
                     [&](const std::string& l) { return has(ctx.landmarks, l); }))
        return false;
    if (has(ctx.recentEvents, d.id)) return false;  // never repeat what the player just saw
    for (const auto& e : ledger.events()) {
        if (e.defId != d.id || e.regionX != ctx.regionX || e.regionZ != ctx.regionZ) continue;
        if (!e.cooldownElapsed(d, nowHour)) return false;  // still running or cooling down
    }
    return true;
}

// Fills one region from the template pool. Each round gathers every template that fits the
// region context (eventFits) and is affordable within the remaining budget, makes one
// weighted pick, spawns it as a rumour in the ledger and deducts its story/population cost.
// Stops when nothing fits or the budget is spent. A template is placed at most once per fill,
// and the result is deterministic for (regionSeed, nowHour) so replays and reloads agree.
inline std::vector<std::string> fillRegion(const std::vector<FillerTemplate>& templates,
                                           const RegionContext& ctx, RegionBudget& budget,
                                           EventLedger& ledger, std::uint64_t regionSeed,
                                           std::uint64_t nowHour) {
    core::Rng rng(core::hashCombine(core::hashCombine(regionSeed, kFillerSalt), nowHour));
    std::vector<bool> used(templates.size(), false);
    std::vector<std::size_t> candidates;
    std::vector<std::string> placed;
    for (;;) {
        candidates.clear();
        std::uint64_t totalWeight = 0;
        for (std::size_t i = 0; i < templates.size(); ++i) {
            const FillerTemplate& t = templates[i];
            if (used[i] || t.weight == 0) continue;
            if (t.storyCost > budget.story || t.populationCost > budget.population) continue;
            if (!eventFits(t, ctx, ledger, nowHour)) continue;
            candidates.push_back(i);
            totalWeight += t.weight;
        }
        if (candidates.empty()) break;
        std::uint64_t roll = rng.below(totalWeight);
        std::size_t chosen = candidates.back();
        for (const std::size_t i : candidates) {
            if (roll < templates[i].weight) {
                chosen = i;
                break;
            }
            roll -= templates[i].weight;
        }
        const FillerTemplate& pick = templates[chosen];
        used[chosen] = true;
        budget.story -= pick.storyCost;
        budget.population -= pick.populationCost;
        ledger.spawn(pick.def, ctx.regionX, ctx.regionZ, nowHour);
        placed.push_back(pick.def.id);
    }
    return placed;
}

}  // namespace lt::world
