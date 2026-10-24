#pragma once
// Legend Engine T - Milestone C: enemy records and deterministic variants (design doc sections 9 and 21).
// Records follow the section 21 enemy schema. Variants are derived from aspect mutation, faction kit,
// biome adaptation and event scars using a seed-only hash, so the same seed always yields the same encounter.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "tcombat/Weapon.h"

namespace lt::combat {

enum class EnemyRole : std::uint8_t { Brute, Duelist, Ambusher, Caster, Swarm, Burrower, Neutral, Flyer, Titan };

struct EnemyDef {
    std::string id;
    EnemyRole role = EnemyRole::Brute;
    float perception = 0.0f;  // metres
    float posture = 0.0f;
    float health = 100.0f;
    DamageVec resistances = neutralResist();
    std::string attackSet;
    std::vector<std::string> drops;
    std::vector<std::string> biomes;
    std::vector<std::string> eventMutations;
    bool neutralUntilProvoked = false;
};

inline void setResist(EnemyDef& e, Channel c, float v) { e.resistances[static_cast<std::size_t>(c)] = v; }

inline EnemyDef makeEnemy(std::string id, EnemyRole role, float perception, float posture, float health,
                          std::string attackSet, std::vector<std::string> drops, std::vector<std::string> biomes,
                          std::vector<std::string> mutations) {
    EnemyDef e;
    e.id = std::move(id); e.role = role; e.perception = perception; e.posture = posture; e.health = health;
    e.attackSet = std::move(attackSet); e.drops = std::move(drops); e.biomes = std::move(biomes);
    e.eventMutations = std::move(mutations);
    return e;
}

// The eleven common families from section 9.
inline std::vector<EnemyDef> commonEnemyFamilies() {
    std::vector<EnemyDef> v;
    v.push_back(makeEnemy("enemy.mourge_husk", EnemyRole::Brute, 14, 40, 80, "husk_v1",
                          {"aspect.memory"}, {"verdant_reach", "tainted_grove", "underroot"}, {"revenant", "echo"}));
    v.push_back(makeEnemy("enemy.glass_leech", EnemyRole::Ambusher, 10, 30, 60, "leech_v1",
                          {"aspect.light"}, {"mire_of_glass"}, {"mirror_clone", "tainted"}));
    { auto e = makeEnemy("enemy.salt_wraith", EnemyRole::Caster, 30, 25, 70, "wraith_v1",
                         {"aspect.light", "aspect.motion"}, {"salt_dune"}, {"mirage", "champion"});
      setResist(e, Channel::Physical, 0.7f); v.push_back(e); }
    { auto e = makeEnemy("enemy.frostbound_knight", EnemyRole::Duelist, 26, 120, 220, "frost_knight_v2",
                         {"aspect.form", "armor.frostbound_plate"}, {"frost_crown"}, {"revenant", "champion", "tainted"});
      setResist(e, Channel::Frost, 0.8f); setResist(e, Channel::Ash, 1.35f); v.push_back(e); }
    v.push_back(makeEnemy("enemy.tainted_familiar", EnemyRole::Swarm, 18, 10, 30, "familiar_v1",
                          {"aspect.decay"}, {"tainted_grove"}, {"tainted", "aura_thief"}));
    { auto e = makeEnemy("enemy.ash_crawler", EnemyRole::Burrower, 16, 50, 110, "crawler_v1",
                         {"aspect.decay"}, {"ash_barrens"}, {"champion", "molten"});
      setResist(e, Channel::Ash, 0.4f); setResist(e, Channel::Frost, 1.4f); v.push_back(e); }
    { auto e = makeEnemy("enemy.golem_miner", EnemyRole::Neutral, 12, 90, 200, "golem_miner_v1",
                         {"aspect.form"}, {"underroot", "ash_barrens"}, {"uprising"});
      e.neutralUntilProvoked = true; setResist(e, Channel::Lightning, 1.3f); v.push_back(e); }
    v.push_back(makeEnemy("enemy.order_deserter", EnemyRole::Duelist, 22, 80, 140, "deserter_v1",
                          {"aspect.form", "aspect.motion"}, {"verdant_reach", "aether_highlands"}, {"champion", "bandit"}));
    { auto e = makeEnemy("enemy.wyvern", EnemyRole::Flyer, 40, 160, 400, "wyvern_v1",
                         {"aspect.motion", "aspect.life"}, {"aether_highlands", "ash_barrens"}, {"nest_guardian"});
      setResist(e, Channel::Ash, 0.5f); v.push_back(e); }
    { auto e = makeEnemy("enemy.void_moth", EnemyRole::Swarm, 20, 5, 20, "moth_v1",
                         {"aspect.light"}, {"underroot", "tainted_grove"}, {"light_drain"});
      setResist(e, Channel::Void, 0.3f); v.push_back(e); }
    { auto e = makeEnemy("enemy.root_titan", EnemyRole::Titan, 35, 300, 900, "root_titan_v1",
                         {"aspect.life", "aspect.form"}, {"verdant_reach", "underroot"}, {"tainted"});
      setResist(e, Channel::Ash, 1.5f); setResist(e, Channel::Verdant, 0.5f); v.push_back(e); }
    return v;
}

inline std::optional<EnemyDef> findEnemy(const std::vector<EnemyDef>& set, const std::string& id) {
    for (const auto& e : set) if (e.id == id) return e;
    return std::nullopt;
}

// --- Deterministic variants -------------------------------------------------------------

inline std::uint64_t variantHash(std::uint64_t x) {  // splitmix64 finaliser
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline std::uint64_t hashString(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

struct VariantContext {
    std::uint64_t worldSeed = 0;
    std::uint64_t spawnId = 0;      // stable per spawn anchor
    std::string biome;              // biome adaptation
    std::string faction;            // faction kit (empty = none)
    float flux = 0.0f;              // 0..1, drives aspect mutation chance
    std::string eventScar;          // active event mutation, e.g. "tainted" (empty = none)
};

struct EnemyVariant {
    EnemyDef def;
    std::vector<std::string> modifiers;  // human-readable record of what was applied, in order
};

inline EnemyVariant makeVariant(const EnemyDef& base, const VariantContext& ctx) {
    EnemyVariant v{base, {}};
    std::uint64_t h = variantHash(ctx.worldSeed ^ variantHash(ctx.spawnId) ^ hashString(base.id));

    // Aspect mutation: probability scales with flux.
    const float roll = static_cast<float>(h % 10000) / 10000.0f;
    if (roll < 0.1f + ctx.flux * 0.6f) {
        static const Channel kMut[] = {Channel::Frost, Channel::Ash, Channel::Verdant, Channel::Aether, Channel::Void, Channel::Flux};
        const Channel c = kMut[(h >> 16) % 6];
        v.def.resistances[static_cast<std::size_t>(c)] *= 0.6f;
        v.def.posture *= 1.15f;
        v.modifiers.push_back("aspect_mutation:" + std::to_string(static_cast<int>(c)));
    }
    h = variantHash(h);

    // Biome adaptation: off-biome spawns adapt to local hazards.
    bool native = false;
    for (const auto& b : base.biomes) if (b == ctx.biome) native = true;
    if (!native && !ctx.biome.empty()) {
        if (ctx.biome == "frost_crown") v.def.resistances[static_cast<std::size_t>(Channel::Frost)] *= 0.8f;
        else if (ctx.biome == "ash_barrens") v.def.resistances[static_cast<std::size_t>(Channel::Ash)] *= 0.8f;
        else if (ctx.biome == "mire_of_glass") v.def.resistances[static_cast<std::size_t>(Channel::Lightning)] *= 1.25f;  // wet
        v.modifiers.push_back("biome_adaptation:" + ctx.biome);
    }

    // Faction kit.
    if (!ctx.faction.empty()) {
        v.def.health *= 1.1f;
        v.def.attackSet += "+" + ctx.faction;
        v.modifiers.push_back("faction_kit:" + ctx.faction);
    }

    // Event scar: only mutations the record allows.
    if (!ctx.eventScar.empty()) {
        for (const auto& m : base.eventMutations) {
            if (m != ctx.eventScar) continue;
            if (m == "champion") { v.def.health *= 1.5f; v.def.posture *= 1.3f; }
            else if (m == "revenant") { v.def.health *= 1.2f; v.def.resistances[static_cast<std::size_t>(Channel::Void)] *= 0.5f; }
            else if (m == "tainted") { v.def.resistances[static_cast<std::size_t>(Channel::Void)] = -0.5f; }  // heals from void
            v.modifiers.push_back("event_scar:" + m);
            break;
        }
    }
    return v;
}

// --- Runtime state: perception, posture break, health -----------------------------------

struct EnemyState {
    float health = 0.0f;
    float posture = 0.0f;     // accumulated posture damage
    bool postureBroken = false;
    bool provoked = false;
    bool dead() const { return health <= 0.0f; }
};

inline EnemyState spawnState(const EnemyDef& d) { return EnemyState{d.health, 0.0f, false, !d.neutralUntilProvoked}; }

inline bool perceives(const EnemyDef& d, float distanceMetres, bool crouched) {
    return distanceMetres <= (crouched ? d.perception * 0.5f : d.perception);
}

inline void applyHit(const EnemyDef& d, EnemyState& s, const HitResult& r) {
    s.health -= r.healthDamage;
    s.posture += r.postureDamageToDefender;
    if (s.posture >= d.posture) s.postureBroken = true;
    if (r.healthDamage > 0.0f || r.postureDamageToDefender > 0.0f) s.provoked = true;
}

}  // namespace lt::combat
