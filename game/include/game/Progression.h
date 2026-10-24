#pragma once
// ThaumtMourge game layer - Section 6 (starting classes, four progression tracks) and
// Section 11 (factions). Class is a starting toolkit, not an ability prison: any class
// may learn any weapon school or aspect; class only changes the kit and research cost.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lt::game {

enum class StartClass : std::uint8_t { Warden = 0, Spellblade, Thaumaturge, Ranger, AshenMonk, Golemancer };
inline constexpr std::size_t kStartClassCount = 6;

enum class Faction : std::uint8_t {
    OrderOfThaumiel = 0,
    TautmaurgRemnants,
    KharDuunMiners,
    VeyrSmugglers,
    VerdantCourt,
    AshTribunal,
    FreeCaravans,
    MourgeCults
};
inline constexpr std::size_t kFactionCount = 8;

constexpr std::string_view toString(StartClass c) noexcept {
    switch (c) {
        case StartClass::Warden: return "warden";
        case StartClass::Spellblade: return "spellblade";
        case StartClass::Thaumaturge: return "thaumaturge";
        case StartClass::Ranger: return "ranger";
        case StartClass::AshenMonk: return "ashen_monk";
        case StartClass::Golemancer: return "golemancer";
    }
    return "unknown";
}

constexpr std::string_view toString(Faction f) noexcept {
    switch (f) {
        case Faction::OrderOfThaumiel: return "order_of_thaumiel";
        case Faction::TautmaurgRemnants: return "tautmaurg_remnants";
        case Faction::KharDuunMiners: return "khar_duun_miners";
        case Faction::VeyrSmugglers: return "veyr_smugglers";
        case Faction::VerdantCourt: return "verdant_court";
        case Faction::AshTribunal: return "ash_tribunal";
        case Faction::FreeCaravans: return "free_caravans";
        case Faction::MourgeCults: return "mourge_cults";
    }
    return "unknown";
}

// Starting toolkit. Families use Section 7 names; affinity is the primal aspect this
// class researches at reduced cost (Section 8). Multipliers scale the rank-1 base stats.
struct ClassKit {
    StartClass cls;
    std::string_view mainFamily;
    std::string_view offFamily;  // empty when the class starts with no off-hand
    std::string_view affinityAspect;
    float healthMul;
    float staminaMul;
    float focusMul;
};

inline constexpr std::array<ClassKit, kStartClassCount> kClassKits{{
    {StartClass::Warden, "longsword", "shield", "form", 1.15f, 1.10f, 0.85f},
    {StartClass::Spellblade, "longsword", "arcane_focus", "light", 1.00f, 1.00f, 1.05f},
    {StartClass::Thaumaturge, "arcane_focus", "", "memory", 0.90f, 0.90f, 1.30f},
    {StartClass::Ranger, "bow", "sickle", "life", 0.95f, 1.15f, 0.95f},
    {StartClass::AshenMonk, "gauntlets", "", "motion", 1.00f, 1.20f, 0.90f},
    {StartClass::Golemancer, "hammer", "", "decay", 1.05f, 0.95f, 1.10f},
}};

constexpr const ClassKit& kitFor(StartClass c) noexcept { return kClassKits[static_cast<std::size_t>(c)]; }

// Track 1 - Rank: health, stamina, focus and equipment capacity grow per rank, scaled by
// the starting kit. Rank is capped so a build is never purely a level grind.
inline constexpr std::uint32_t kMaxRank = 99;

struct RankStats {
    float health;
    float stamina;
    float focus;
    float equipCapacity;
};

constexpr RankStats rankStats(StartClass c, std::uint32_t rank) noexcept {
    const ClassKit& k = kitFor(c);
    const std::uint32_t r = rank < 1 ? 1 : (rank > kMaxRank ? kMaxRank : rank);
    const float steps = static_cast<float>(r - 1);
    return RankStats{(100.0f + 12.0f * steps) * k.healthMul, (80.0f + 6.0f * steps) * k.staminaMul,
                     (50.0f + 5.0f * steps) * k.focusMul, 30.0f + 1.5f * steps};
}

// Rank-up currency required to go from rank to rank + 1.
constexpr std::uint64_t rankUpCost(std::uint32_t rank) noexcept {
    const std::uint64_t r = rank < 1 ? 1 : rank;
    return 100 + 25 * r * r;
}

// Track 2 - Research: every aspect is learnable by every class; the class affinity aspect
// costs 25% less and no aspect ever costs more than its base. Returns research points.
constexpr std::uint32_t aspectResearchCost(StartClass c, std::string_view aspect,
                                           std::uint32_t baseCost) noexcept {
    return kitFor(c).affinityAspect == aspect ? baseCost - baseCost / 4 : baseCost;
}

// Track 3 - Masteries: per weapon family experience unlocks Section 7 moves in a fixed
// order and widens the perfect-deflection window. Every family is open to every class.
inline constexpr std::size_t kWeaponFamilyCount = 14;
inline constexpr std::array<std::string_view, kWeaponFamilyCount> kWeaponFamilies{{
    "greatblade", "longsword", "spear", "axe", "hammer", "sickle", "bow",
    "crossbow", "handcannon", "arcane_focus", "chakram", "gauntlets", "shield", "voxel_tool"}};

constexpr int familyIndex(std::string_view family) noexcept {
    for (std::size_t i = 0; i < kWeaponFamilyCount; ++i)
        if (kWeaponFamilies[i] == family) return static_cast<int>(i);
    return -1;
}

enum class MasteryMove : std::uint8_t { Light = 0, Heavy, Sprint, Charged, Air, GuardCounter, Execution };

inline constexpr std::uint32_t kMaxMasteryLevel = 10;

// Total xp for level n is 50 * n * (n + 1), capped at kMaxMasteryLevel.
constexpr std::uint32_t masteryLevelFor(std::uint64_t xp) noexcept {
    std::uint32_t lvl = 0;
    while (lvl < kMaxMasteryLevel &&
           xp >= 50ull * (static_cast<std::uint64_t>(lvl) + 1) * (static_cast<std::uint64_t>(lvl) + 2))
        ++lvl;
    return lvl;
}

constexpr std::uint32_t requiredMasteryLevel(MasteryMove m) noexcept {
    switch (m) {
        case MasteryMove::Light: return 0;
        case MasteryMove::Heavy: return 0;
        case MasteryMove::Sprint: return 1;
        case MasteryMove::Charged: return 2;
        case MasteryMove::Air: return 3;
        case MasteryMove::GuardCounter: return 5;
        case MasteryMove::Execution: return 7;
    }
    return kMaxMasteryLevel;
}

class Masteries {
public:
    // Adds xp (saturating). Returns false for a family Section 7 does not define.
    constexpr bool addXp(std::string_view family, std::uint64_t amount) noexcept {
        const int i = familyIndex(family);
        if (i < 0) return false;
        std::uint64_t& v = xp_[static_cast<std::size_t>(i)];
        v = amount > ~std::uint64_t{0} - v ? ~std::uint64_t{0} : v + amount;
        return true;
    }

    constexpr std::uint64_t xp(std::string_view family) const noexcept {
        const int i = familyIndex(family);
        return i < 0 ? 0 : xp_[static_cast<std::size_t>(i)];
    }

    constexpr std::uint32_t level(std::string_view family) const noexcept { return masteryLevelFor(xp(family)); }

    constexpr bool knows(std::string_view family, MasteryMove m) const noexcept {
        return familyIndex(family) >= 0 && level(family) >= requiredMasteryLevel(m);
    }

    // Extra perfect-deflection ticks: one per four mastery levels (max +2 at level 10).
    constexpr std::uint32_t deflectBonusTicks(std::string_view family) const noexcept {
        return level(family) / 4;
    }

private:
    std::array<std::uint64_t, kWeaponFamilyCount> xp_{};
};

// Track 4 - Reputation: per-faction standing in [-1000, 1000]. Standing tiers drive faction
// access, vendor prices and patrol behaviour (hostile patrols attack on sight).
enum class Standing : std::uint8_t { Hostile = 0, Distrusted, Neutral, Trusted, Honoured };

class Reputation {
public:
    static constexpr std::int32_t kMin = -1000;
    static constexpr std::int32_t kMax = 1000;

    constexpr std::int32_t value(Faction f) const noexcept { return values_[idx(f)]; }

    // Applies a delta clamped to the valid range and returns the new value.
    constexpr std::int32_t adjust(Faction f, std::int32_t delta) noexcept {
        const std::int64_t next = static_cast<std::int64_t>(values_[idx(f)]) + delta;
        values_[idx(f)] = static_cast<std::int32_t>(next < kMin ? kMin : (next > kMax ? kMax : next));
        return values_[idx(f)];
    }

    constexpr Standing standing(Faction f) const noexcept {
        const std::int32_t v = value(f);
        if (v <= -500) return Standing::Hostile;
        if (v < -100) return Standing::Distrusted;
        if (v <= 100) return Standing::Neutral;
        if (v < 500) return Standing::Trusted;
        return Standing::Honoured;
    }

    // Vendor price multiplier; hostile factions refuse trade entirely (returns 0).
    constexpr float priceMultiplier(Faction f) const noexcept {
        switch (standing(f)) {
            case Standing::Hostile: return 0.0f;
            case Standing::Distrusted: return 1.25f;
            case Standing::Neutral: return 1.0f;
            case Standing::Trusted: return 0.9f;
            case Standing::Honoured: return 0.8f;
        }
        return 1.0f;
    }

    constexpr bool canTrade(Faction f) const noexcept { return standing(f) != Standing::Hostile; }
    constexpr bool patrolsHostile(Faction f) const noexcept { return standing(f) == Standing::Hostile; }

private:
    static constexpr std::size_t idx(Faction f) noexcept { return static_cast<std::size_t>(f); }
    std::array<std::int32_t, kFactionCount> values_{};
};

} // namespace lt::game
