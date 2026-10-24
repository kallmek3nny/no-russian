#pragma once
// ThaumtMourge game layer - Section 11 NPCs. Every important NPC has a goal, fear,
// relationship graph, faction, schedule, combat role and failure state. NPCs can die,
// move, become vendors, become enemies, or leave a phantom record. Names, goals and
// fears are localisation keys (Section 26): no display text is embedded in C++.
#include <cstdint>
#include <string>
#include <vector>

#include "game/Progression.h"

namespace lt::game {

enum class NpcCombatRole : std::uint8_t { NonCombatant = 0, Support, Duelist, Ranged, Caster, Commander };

// Fate of an NPC. Dead is terminal except for leaving a Phantom record (Section 13).
enum class NpcFate : std::uint8_t { Alive = 0, Moved, Vendor, Enemy, Dead, Phantom };

// One block of the daily schedule: from startHour (0-23) until the next block starts the
// NPC is at `location` doing `activity`. Blocks are sorted by startHour; the last block
// wraps past midnight.
struct ScheduleBlock {
    std::uint8_t startHour = 0;
    std::string location;  // e.g. "aurelia.research_hall"
    std::string activity;  // e.g. "research", "patrol", "trade", "sleep"
};

// Directed edge in the relationship graph; affinity is clamped to [-100, 100].
struct Relationship {
    std::string otherId;
    std::int32_t affinity = 0;
};

struct NpcDef {
    std::string id;       // e.g. "npc.ilyra_voss"
    std::string nameKey;  // e.g. "npc.ilyra_voss.name"
    Faction faction = Faction::OrderOfThaumiel;
    std::string goalKey;
    std::string fearKey;
    NpcCombatRole role = NpcCombatRole::NonCombatant;
    std::string failureState;  // consequence when the NPC's arc fails, e.g. "turns_enemy"
    std::vector<ScheduleBlock> schedule;
    std::vector<Relationship> relations;
};

// Schedule block active at an hour of the day, or nullptr when the NPC has no schedule.
inline const ScheduleBlock* scheduleAt(const NpcDef& d, std::uint32_t hourOfDay) noexcept {
    if (d.schedule.empty()) return nullptr;
    const std::uint32_t h = hourOfDay % 24;
    const ScheduleBlock* cur = &d.schedule.back();
    for (const ScheduleBlock& b : d.schedule) {
        if (b.startHour > h) break;
        cur = &b;
    }
    return cur;
}

// Initial affinity from `d` toward `otherId`; 0 when the graph has no edge.
inline std::int32_t baseAffinity(const NpcDef& d, const std::string& otherId) noexcept {
    for (const Relationship& r : d.relations)
        if (r.otherId == otherId) return r.affinity;
    return 0;
}

inline constexpr std::int32_t kAffinityMin = -100;
inline constexpr std::int32_t kAffinityMax = 100;

// A living NPC (at home, moved, vending or hostile) may switch to any other living fate
// or die. Dead may only leave a Phantom record; Phantom is terminal.
constexpr bool isLiving(NpcFate f) noexcept { return f != NpcFate::Dead && f != NpcFate::Phantom; }

constexpr bool canTransition(NpcFate from, NpcFate to) noexcept {
    if (from == to) return false;
    if (isLiving(from)) return to != NpcFate::Phantom;
    return from == NpcFate::Dead && to == NpcFate::Phantom;
}

// Runtime NPC state, persisted in the world journal (Section 14, layer 4).
class NpcState {
public:
    explicit NpcState(const NpcDef& def) : defId_(def.id), relations_(def.relations) {
        for (Relationship& r : relations_) r.affinity = clampAffinity(r.affinity);
    }

    const std::string& defId() const noexcept { return defId_; }
    NpcFate fate() const noexcept { return fate_; }
    std::uint64_t fateSinceHour() const noexcept { return fateSinceHour_; }
    const std::string& movedTo() const noexcept { return movedTo_; }
    const std::vector<Relationship>& relations() const noexcept { return relations_; }
    bool living() const noexcept { return isLiving(fate_); }

    // Changes fate. Moved requires a destination; returning to Alive restores the home
    // schedule. Returns false (state unchanged) on an illegal transition.
    bool setFate(NpcFate to, std::uint64_t nowHour, const std::string& destination = {}) {
        if (!canTransition(fate_, to)) return false;
        if (to == NpcFate::Moved && destination.empty()) return false;
        fate_ = to;
        fateSinceHour_ = nowHour;
        if (to == NpcFate::Moved) movedTo_ = destination;
        else if (to == NpcFate::Alive) movedTo_.clear();
        return true;
    }

    // Location at an hour of the day: a move overrides the schedule; dead and phantom
    // NPCs are not physically present (empty string).
    std::string currentLocation(const NpcDef& def, std::uint32_t hourOfDay) const {
        if (!living()) return {};
        if (!movedTo_.empty()) return movedTo_;
        const ScheduleBlock* b = scheduleAt(def, hourOfDay);
        return b ? b->location : std::string{};
    }

    std::int32_t affinity(const std::string& otherId) const noexcept {
        for (const Relationship& r : relations_)
            if (r.otherId == otherId) return r.affinity;
        return 0;
    }

    // Adjusts affinity toward another NPC, adding the edge when missing; clamped.
    std::int32_t adjustAffinity(const std::string& otherId, std::int32_t delta) {
        for (Relationship& r : relations_) {
            if (r.otherId == otherId) {
                r.affinity = clampAffinity(static_cast<std::int64_t>(r.affinity) + delta);
                return r.affinity;
            }
        }
        relations_.push_back(Relationship{otherId, clampAffinity(delta)});
        return relations_.back().affinity;
    }

private:
    static constexpr std::int32_t clampAffinity(std::int64_t v) noexcept {
        return static_cast<std::int32_t>(v < kAffinityMin ? kAffinityMin : (v > kAffinityMax ? kAffinityMax : v));
    }

    std::string defId_;
    NpcFate fate_ = NpcFate::Alive;
    std::uint64_t fateSinceHour_ = 0;
    std::string movedTo_;
    std::vector<Relationship> relations_;
};

} // namespace lt::game
