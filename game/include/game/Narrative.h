#pragma once
// ThaumtMourge game layer - Section 3 narrative structure. Four acts, five regional
// routes taken in any order (Act II), city outcomes in the Council War (Act III), five
// Crown Keys assembled or destroyed (Act IV), Council member fates, and the five
// endings. Act and ending names are localisation keys (Section 26).
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "game/Progression.h" // Faction (Section 11) for city protectors

namespace lt::game {

enum class Act : std::uint8_t { BrokenSeal = 0, FiveRoads, CouncilWar, CrownGate, PostGame };

enum class Ending : std::uint8_t { None = 0, Seal, Throne, WildEquation, AshVerdict, Mourge };

// Act IV: each Crown Key is either still missing, assembled into the gate, or destroyed.
enum class KeyState : std::uint8_t { Missing = 0, Assembled, Destroyed };

// Each surviving Tautmaurg ends killed, redeemed or empowered; the Crown Gate megastructure
// changes state from these fates (its layout stays stable for a given seed).
enum class CouncilFate : std::uint8_t { Unresolved = 0, Killed, Redeemed, Empowered };

// Act III: a city can be defended, abandoned, or placed under a faction's protection.
enum class CityOutcome : std::uint8_t { Contested = 0, Defended, Abandoned, Protected };

inline constexpr std::size_t kActOneNodes = 3;    // aura nodes relit to finish Act I
inline constexpr std::size_t kRouteCount = 5;     // Act II regional routes
inline constexpr std::size_t kCrownKeyCount = 5;  // Act IV keys
inline constexpr std::size_t kCouncilSeats = 6;   // one Tautmaurg per primal aspect discipline
inline constexpr std::size_t kCityCount = 5;      // Section 4.3 cities

constexpr std::string_view toKey(Act a) noexcept {
    switch (a) {
        case Act::BrokenSeal: return "act.broken_seal";
        case Act::FiveRoads: return "act.five_roads";
        case Act::CouncilWar: return "act.council_war";
        case Act::CrownGate: return "act.crown_gate";
        case Act::PostGame: return "act.post_game";
    }
    return "act.unknown";
}

constexpr std::string_view toKey(Ending e) noexcept {
    switch (e) {
        case Ending::None: return "ending.none";
        case Ending::Seal: return "ending.seal";
        case Ending::Throne: return "ending.throne";
        case Ending::WildEquation: return "ending.wild_equation";
        case Ending::AshVerdict: return "ending.ash_verdict";
        case Ending::Mourge: return "ending.mourge";
    }
    return "ending.unknown";
}

// Campaign progress, persisted with the character layer (Section 14, layer 2).
//   Act I   -> Act II   : all three local aura nodes relit.
//   Act II  -> Act III  : all five regional routes completed, in any order.
//   Act III -> Act IV   : every city defended, abandoned, or placed under protection.
//   Act IV  -> post-game: every Crown Key assembled or destroyed, then an ending chosen.
class NarrativeState {
public:
    Act act() const noexcept { return act_; }

    // Act I: relight local aura node 0-2. Returns false for a bad index or a lit node.
    bool relightNode(std::size_t node) noexcept {
        if (node >= kActOneNodes || (nodesLit_ & bit(node)) != 0) return false;
        nodesLit_ = static_cast<std::uint8_t>(nodesLit_ | bit(node));
        if (act_ == Act::BrokenSeal && nodesLitCount() == kActOneNodes) act_ = Act::FiveRoads;
        return true;
    }
    std::size_t nodesLitCount() const noexcept { return countBits(nodesLit_); }

    // Act II: complete route 0-4 in any order. The order is kept so boss resistances and
    // NPC dialogue can react to it. Returns false outside Act II, for a bad index, or for
    // a route that is already complete.
    bool completeRoute(std::size_t route) noexcept {
        if (act_ != Act::FiveRoads || route >= kRouteCount || (routesDone_ & bit(route)) != 0)
            return false;
        routesDone_ = static_cast<std::uint8_t>(routesDone_ | bit(route));
        routeOrder_[routeCount_++] = static_cast<std::uint8_t>(route);
        if (routeCount_ == kRouteCount) act_ = Act::CouncilWar;
        return true;
    }
    std::size_t routesCompleted() const noexcept { return routeCount_; }

    // 0-based position at which a route was completed, or -1 while it is still open.
    int routePosition(std::size_t route) const noexcept {
        for (std::size_t i = 0; i < routeCount_; ++i)
            if (routeOrder_[i] == route) return static_cast<int>(i);
        return -1;
    }

    // Act III: decide a contested city's fate; a decision is final. Protected records the
    // protecting faction. Deciding the last contested city opens the Crown Gate (Act IV).
    bool resolveCity(std::size_t city, CityOutcome outcome,
                     Faction protector = Faction::OrderOfThaumiel) noexcept {
        if (act_ != Act::CouncilWar || city >= kCityCount || outcome == CityOutcome::Contested ||
            cities_[city] != CityOutcome::Contested)
            return false;
        cities_[city] = outcome;
        if (outcome == CityOutcome::Protected) protectors_[city] = protector;
        for (CityOutcome c : cities_)
            if (c == CityOutcome::Contested) return true;
        act_ = Act::CrownGate;
        return true;
    }
    CityOutcome cityOutcome(std::size_t city) const noexcept {
        return city < kCityCount ? cities_[city] : CityOutcome::Contested;
    }
    // Protecting faction of a Protected city (meaningless for any other outcome).
    Faction cityProtector(std::size_t city) const noexcept {
        return city < kCityCount ? protectors_[city] : Faction::OrderOfThaumiel;
    }

    // Council fates (killed, redeemed, empowered) are decided across the campaign and are
    // final once set; nothing changes after the ending has been chosen.
    bool setCouncilFate(std::size_t seat, CouncilFate fate) noexcept {
        if (act_ == Act::PostGame || seat >= kCouncilSeats || fate == CouncilFate::Unresolved ||
            council_[seat] != CouncilFate::Unresolved)
            return false;
        council_[seat] = fate;
        return true;
    }
    CouncilFate councilFate(std::size_t seat) const noexcept {
        return seat < kCouncilSeats ? council_[seat] : CouncilFate::Unresolved;
    }
    std::size_t councilCount(CouncilFate fate) const noexcept {
        std::size_t n = 0;
        for (CouncilFate f : council_)
            if (f == fate) ++n;
        return n;
    }

    // Act IV: assemble or destroy Crown Key 0-4; the decision is final.
    bool decideKey(std::size_t key, KeyState state) noexcept {
        if (act_ != Act::CrownGate || key >= kCrownKeyCount || state == KeyState::Missing ||
            keys_[key] != KeyState::Missing)
            return false;
        keys_[key] = state;
        return true;
    }
    KeyState keyState(std::size_t key) const noexcept {
        return key < kCrownKeyCount ? keys_[key] : KeyState::Missing;
    }
    std::size_t keyCount(KeyState state) const noexcept {
        std::size_t n = 0;
        for (KeyState k : keys_)
            if (k == state) ++n;
        return n;
    }
    bool allKeysDecided() const noexcept { return keyCount(KeyState::Missing) == 0; }

    // Crown Gate megastructure state: 2 bits per Council seat (bits 0-11), then 2 bits per
    // Crown Key (bits 12-21). The layout itself is seeded; this code selects which authored
    // chunk variants the arena swaps in, so identical choices always yield the same gate.
    std::uint32_t gateStateCode() const noexcept {
        std::uint32_t code = 0;
        for (std::size_t i = 0; i < kCouncilSeats; ++i)
            code |= static_cast<std::uint32_t>(council_[i]) << (2 * i);
        for (std::size_t i = 0; i < kCrownKeyCount; ++i)
            code |= static_cast<std::uint32_t>(keys_[i]) << (2 * (kCouncilSeats + i));
        return code;
    }

    // Ending availability in Act IV once every key is decided:
    //   Seal         - all five keys assembled and no Tautmaurg empowered.
    //   Throne       - all five keys assembled.
    //   WildEquation - at least one key assembled and at least one destroyed.
    //   AshVerdict   - a majority (three or more) of the keys destroyed.
    //   Mourge       - always: the player may deliberately feed the gate.
    bool endingAvailable(Ending e) const noexcept {
        if (act_ != Act::CrownGate || !allKeysDecided()) return false;
        const std::size_t assembled = keyCount(KeyState::Assembled);
        const std::size_t destroyed = keyCount(KeyState::Destroyed);
        switch (e) {
            case Ending::None: return false;
            case Ending::Seal:
                return assembled == kCrownKeyCount && councilCount(CouncilFate::Empowered) == 0;
            case Ending::Throne: return assembled == kCrownKeyCount;
            case Ending::WildEquation: return assembled > 0 && destroyed > 0;
            case Ending::AshVerdict: return destroyed * 2 > kCrownKeyCount;
            case Ending::Mourge: return true;
        }
        return false;
    }

    // Chooses an available ending and enters the post-game. Returns false otherwise.
    bool chooseEnding(Ending e) noexcept {
        if (!endingAvailable(e)) return false;
        ending_ = e;
        act_ = Act::PostGame;
        return true;
    }

    // Failing the Crown Gate encounter forces the Mourge ending regardless of the keys.
    bool failGate() noexcept {
        if (act_ != Act::CrownGate) return false;
        ending_ = Ending::Mourge;
        act_ = Act::PostGame;
        return true;
    }

    Ending ending() const noexcept { return ending_; }
    // The Mourge ending unlocks an altered post-game world.
    bool alteredPostGame() const noexcept { return ending_ == Ending::Mourge; }

private:
    static constexpr std::uint8_t bit(std::size_t i) noexcept {
        return static_cast<std::uint8_t>(1u << i);
    }
    static constexpr std::size_t countBits(std::uint8_t v) noexcept {
        std::size_t n = 0;
        for (; v != 0; v = static_cast<std::uint8_t>(v & (v - 1))) ++n;
        return n;
    }

    Act act_ = Act::BrokenSeal;
    std::uint8_t nodesLit_ = 0;
    std::uint8_t routesDone_ = 0;
    std::size_t routeCount_ = 0;
    std::array<std::uint8_t, kRouteCount> routeOrder_{};
    std::array<CityOutcome, kCityCount> cities_{};
    std::array<Faction, kCityCount> protectors_{};
    std::array<KeyState, kCrownKeyCount> keys_{};
    std::array<CouncilFate, kCouncilSeats> council_{};
    Ending ending_ = Ending::None;
};

} // namespace lt::game
