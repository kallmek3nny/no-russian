#pragma once
// Legend Engine T - Milestone B: four vertical-slice weapons (design doc sections 7 and 21).
// Plain, deterministic data: damage channels, posture, stamina/vis cost, and
// wind-up/contact/recovery markers in milliseconds so hit frames match at 30 and 60 FPS.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lt::combat {

enum class Channel : std::uint8_t { Physical = 0, Frost, Ash, Verdant, Aether, Void, Lightning, Poison, Flux, Count };
constexpr std::size_t kChannelCount = static_cast<std::size_t>(Channel::Count);
using DamageVec = std::array<float, kChannelCount>;

// Section 7 weapon families. Order is append-only: existing values are persisted in saves.
enum class Family : std::uint8_t {
    Greatblade, Longsword, Spear, Bow,
    Axe, Hammer, Sickle, Crossbow, Handcannon, ArcaneFocus, Chakram, Gauntlet, Shield, VoxelTool,
    Count
};
enum class MoveKind : std::uint8_t { Light = 0, Heavy, Sprint, Charged, Air, GuardCounter, Execution, Count };
constexpr std::size_t kMoveKindCount = static_cast<std::size_t>(MoveKind::Count);

struct MoveTiming {
    std::uint32_t windupMs = 0;    // telegraph before contact
    std::uint32_t activeMs = 0;    // contact window length
    std::uint32_t recoveryMs = 0;  // commitment after contact
    float damageScale = 1.0f;
    float postureScale = 1.0f;
    float staminaCost = 0.0f;

    std::uint32_t totalMs() const { return windupMs + activeMs + recoveryMs; }
};

// Convert a millisecond marker to the frame index at a fixed tick rate (rounds down).
inline std::uint32_t msToFrame(std::uint32_t ms, std::uint32_t fps) { return static_cast<std::uint32_t>((std::uint64_t(ms) * fps) / 1000u); }

// True when elapsedMs falls inside the contact window of a move.
inline bool isContact(const MoveTiming& m, std::uint32_t elapsedMs) {
    return elapsedMs >= m.windupMs && elapsedMs < m.windupMs + m.activeMs;
}

struct WeaponDef {
    std::string id;
    std::string displayName;
    Family family = Family::Longsword;
    DamageVec damage{};
    float posture = 0.0f;
    float visCost = 0.0f;
    float weight = 0.0f;
    std::vector<std::string> sockets;
    std::string moveset;
    std::vector<std::string> aspectTags;
    std::string loreKey;
    std::array<MoveTiming, kMoveKindCount> moves{};

    const MoveTiming& move(MoveKind k) const { return moves[static_cast<std::size_t>(k)]; }
};

inline std::array<MoveTiming, kMoveKindCount> makeMoves(std::uint32_t baseWindup, std::uint32_t baseActive,
                                                       std::uint32_t baseRecovery, float baseStamina) {
    std::array<MoveTiming, kMoveKindCount> m{};
    m[0] = {baseWindup, baseActive, baseRecovery, 1.0f, 1.0f, baseStamina};                               // light
    m[1] = {baseWindup * 2, baseActive, baseRecovery * 2, 1.6f, 1.8f, baseStamina * 1.8f};                // heavy
    m[2] = {baseWindup, baseActive + 33, baseRecovery + 100, 1.2f, 1.2f, baseStamina * 1.3f};             // sprint
    m[3] = {baseWindup * 3, baseActive + 33, baseRecovery * 2, 2.2f, 2.5f, baseStamina * 2.5f};           // charged
    m[4] = {baseWindup / 2 + 50, baseActive, baseRecovery + 150, 1.1f, 1.0f, baseStamina * 1.2f};         // air
    m[5] = {baseWindup / 2, baseActive, baseRecovery, 1.3f, 2.0f, baseStamina};                           // guard counter
    m[6] = {300, 100, 900, 4.0f, 0.0f, 0.0f};                                                             // execution
    return m;
}

// Milestone B weapon set: one per family the slice needs (heavy melee, balanced melee, reach, ranged).
inline std::vector<WeaponDef> verticalSliceWeapons() {
    std::vector<WeaponDef> w;
    {
        WeaponDef d; d.id = "weapon.aetherfang"; d.displayName = "Aetherfang"; d.family = Family::Greatblade;
        d.damage[static_cast<std::size_t>(Channel::Physical)] = 42; d.damage[static_cast<std::size_t>(Channel::Aether)] = 8;
        d.posture = 38; d.visCost = 8; d.weight = 7.2f; d.sockets = {"core", "guard", "rune"};
        d.moveset = "greatblade_02"; d.aspectTags = {"motion", "memory"}; d.loreKey = "item.aetherfang.lore";
        d.moves = makeMoves(500, 133, 600, 24); w.push_back(d);
    }
    {
        WeaponDef d; d.id = "weapon.order_longsword"; d.displayName = "Order Longsword"; d.family = Family::Longsword;
        d.damage[static_cast<std::size_t>(Channel::Physical)] = 28;
        d.posture = 22; d.visCost = 4; d.weight = 3.8f; d.sockets = {"guard", "rune"};
        d.moveset = "longsword_01"; d.aspectTags = {"form"}; d.loreKey = "item.order_longsword.lore";
        d.moves = makeMoves(300, 100, 400, 14); w.push_back(d);
    }
    {
        WeaponDef d; d.id = "weapon.frostreach_spear"; d.displayName = "Frostreach Spear"; d.family = Family::Spear;
        d.damage[static_cast<std::size_t>(Channel::Physical)] = 24; d.damage[static_cast<std::size_t>(Channel::Frost)] = 10;
        d.posture = 18; d.visCost = 6; d.weight = 4.5f; d.sockets = {"grip", "rune"};
        d.moveset = "spear_01"; d.aspectTags = {"motion", "life"}; d.loreKey = "item.frostreach_spear.lore";
        d.moves = makeMoves(350, 100, 450, 16); w.push_back(d);
    }
    {
        WeaponDef d; d.id = "weapon.wardens_bow"; d.displayName = "Warden's Bow"; d.family = Family::Bow;
        d.damage[static_cast<std::size_t>(Channel::Physical)] = 20; d.damage[static_cast<std::size_t>(Channel::Verdant)] = 4;
        d.posture = 8; d.visCost = 3; d.weight = 2.1f; d.sockets = {"optic", "grip"};
        d.moveset = "bow_01"; d.aspectTags = {"light"}; d.loreKey = "item.wardens_bow.lore";
        d.moves = makeMoves(400, 33, 300, 10); w.push_back(d);
    }
    return w;
}

inline std::optional<WeaponDef> findWeapon(const std::vector<WeaponDef>& set, const std::string& id) {
    for (const auto& d : set) if (d.id == id) return d;
    return std::nullopt;
}

// Defender stance at the moment of contact.
enum class Defence : std::uint8_t { None, Guard, PerfectDeflect, DodgeInvulnerable };

struct HitResult {
    float healthDamage = 0.0f;
    float postureDamageToDefender = 0.0f;
    float postureDamageToAttacker = 0.0f;
    float staminaCostToDefender = 0.0f;
};

// Resistances multiply per channel (e.g. 1.35 = weak, 0.8 = resistant). Guard reduces damage but costs stamina;
// perfect deflection negates damage and pushes posture back onto the attacker; dodge i-frames negate everything.
inline HitResult resolveHit(const WeaponDef& w, MoveKind kind, const DamageVec& resist, Defence def) {
    const MoveTiming& m = w.move(kind);
    float raw = 0.0f;
    for (std::size_t i = 0; i < kChannelCount; ++i) raw += w.damage[i] * m.damageScale * resist[i];
    const float posture = w.posture * m.postureScale;
    HitResult r;
    switch (def) {
        case Defence::None: r.healthDamage = raw; r.postureDamageToDefender = posture; break;
        case Defence::Guard: r.healthDamage = raw * 0.2f; r.postureDamageToDefender = posture * 0.6f; r.staminaCostToDefender = raw * 0.5f; break;
        case Defence::PerfectDeflect: r.postureDamageToAttacker = posture * 0.75f; break;
        case Defence::DodgeInvulnerable: break;
    }
    return r;
}

inline DamageVec neutralResist() { DamageVec r; r.fill(1.0f); return r; }

}  // namespace lt::combat
