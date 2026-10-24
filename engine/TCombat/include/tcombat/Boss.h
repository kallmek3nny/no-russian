#pragma once
// Legend Engine T - Milestone B: one vertical-slice boss (design doc section 10, The Frost Warden).
// Deterministic boss state: health phases, posture break, and breakable armour pieces.
// Breaking armour removes attacks from the moveset and opens alternate arena routes.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace lt::combat {

struct BossAttack {
    std::string id;
    std::uint32_t windupMs = 0;
    std::uint32_t activeMs = 0;
    std::uint32_t recoveryMs = 0;
    float damage = 0.0f;
    int minPhase = 1;                 // first phase the attack is allowed in
    std::string requiresArmour;       // empty = always available; else needs this piece intact
};

struct ArmourPiece {
    std::string id;
    float integrity = 0.0f;
    float maxIntegrity = 0.0f;
    std::string opensRoute;           // arena route unlocked when broken
    bool broken() const { return integrity <= 0.0f; }
};

struct BossDef {
    std::string id;
    std::string displayName;
    float maxHealth = 0.0f;
    float maxPosture = 0.0f;
    std::vector<float> phaseThresholds;   // descending health fractions, e.g. {0.66, 0.33}
    std::vector<ArmourPiece> armour;
    std::vector<BossAttack> attacks;
};

inline BossDef frostWarden() {
    BossDef b;
    b.id = "boss.frost_warden";
    b.displayName = "The Frost Warden";
    b.maxHealth = 2400.0f;
    b.maxPosture = 300.0f;
    b.phaseThresholds = {0.66f, 0.33f};
    b.armour = {{"armour.helm", 200, 200, "route.upper_rampart"},
                {"armour.shield_arm", 260, 260, "route.gatehouse"},
                {"armour.greaves", 180, 180, "route.frozen_moat"}};
    b.attacks = {{"atk.rampart_slam", 700, 133, 800, 140, 1, "armour.shield_arm"},
                 {"atk.glacier_sweep", 500, 166, 600, 95, 1, ""},
                 {"atk.helm_charge", 600, 200, 900, 120, 1, "armour.helm"},
                 {"atk.ice_lance", 400, 100, 500, 80, 2, ""},
                 {"atk.fortress_collapse", 1200, 300, 1400, 260, 3, ""},
                 {"atk.frost_stomp", 450, 133, 550, 90, 2, "armour.greaves"}};
    return b;
}

class BossState {
public:
    explicit BossState(BossDef def, std::uint64_t seed = 0)
        : def_(std::move(def)), health_(def_.maxHealth), rng_(seed ? seed : 0x9E3779B97F4A7C15ull) {}

    const BossDef& def() const { return def_; }
    float health() const { return health_; }
    float posture() const { return posture_; }
    bool dead() const { return health_ <= 0.0f; }
    bool staggered() const { return staggerMs_ > 0; }

    int phase() const {
        const float frac = def_.maxHealth > 0 ? health_ / def_.maxHealth : 0.0f;
        int p = 1;
        for (float t : def_.phaseThresholds)
            if (frac <= t) ++p;
        return p;
    }

    // Applies damage, posture, and optional armour damage. Returns true if this hit caused a stagger.
    bool applyHit(float damage, float postureDamage, const std::string& armourId = {}, float armourDamage = 0.0f) {
        if (dead()) return false;
        health_ = std::max(0.0f, health_ - damage);
        if (!armourId.empty()) {
            for (auto& a : def_.armour) {
                if (a.id == armourId && !a.broken()) {
                    a.integrity = std::max(0.0f, a.integrity - armourDamage);
                    if (a.broken()) openRoutes_.push_back(a.opensRoute);
                }
            }
        }
        if (staggered()) return false;
        posture_ += postureDamage;
        if (posture_ >= def_.maxPosture) {
            posture_ = 0.0f;
            staggerMs_ = kStaggerMs;
            return true;
        }
        return false;
    }

    // Advances timers; posture recovers slowly while not staggered.
    void tick(std::uint32_t dtMs) {
        if (staggerMs_ > 0) {
            staggerMs_ = dtMs >= staggerMs_ ? 0 : staggerMs_ - dtMs;
            return;
        }
        posture_ = std::max(0.0f, posture_ - kPostureRegenPerSec * static_cast<float>(dtMs) / 1000.0f);
    }

    bool armourIntact(const std::string& id) const {
        for (const auto& a : def_.armour)
            if (a.id == id) return !a.broken();
        return false;
    }

    // Attacks usable in the current phase given current armour state.
    std::vector<const BossAttack*> availableAttacks() const {
        std::vector<const BossAttack*> out;
        const int p = phase();
        for (const auto& atk : def_.attacks) {
            if (atk.minPhase > p) continue;
            if (!atk.requiresArmour.empty() && !armourIntact(atk.requiresArmour)) continue;
            out.push_back(&atk);
        }
        return out;
    }

    // Deterministic attack choice from the seeded stream; null when dead, staggered, or no options.
    const BossAttack* chooseAttack() {
        if (dead() || staggered()) return nullptr;
        auto opts = availableAttacks();
        if (opts.empty()) return nullptr;
        return opts[static_cast<std::size_t>(next() % opts.size())];
    }

    const std::vector<std::string>& openRoutes() const { return openRoutes_; }

    static constexpr std::uint32_t kStaggerMs = 3000;
    static constexpr float kPostureRegenPerSec = 20.0f;

private:
    std::uint64_t next() {  // splitmix64
        std::uint64_t z = (rng_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    BossDef def_;
    float health_ = 0.0f;
    float posture_ = 0.0f;
    std::uint32_t staggerMs_ = 0;
    std::uint64_t rng_ = 0;
    std::vector<std::string> openRoutes_;
};

} // namespace lt::combat
