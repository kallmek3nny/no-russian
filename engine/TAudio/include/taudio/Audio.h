#pragma once
// Legend Engine T - TAudio (design doc sections 15, 23 and 24).
// Platform-neutral audio direction: combat cues keyed by attack class, biome music stems,
// and platform mixer budgets. Backends (Opus streaming on Android, desktop stems) sit below this layer.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "tanim/AnimMarkers.h"

namespace lt::audio {

// Section 24: combat audio must communicate attack class without relying only on animation.
enum class CombatCue : std::uint8_t {
    MetallicPreRing,    // guardable strike telegraph
    LowReversedPulse,   // unblockable attack (and grab) telegraph
    DeflectCrack,       // perfect deflection: short glass/metal crack
    PostureBreakImpact  // posture break: spatially widening impact
};
inline constexpr std::size_t kCombatCueCount = 4;

constexpr std::string_view toString(CombatCue c) noexcept {
    switch (c) {
        case CombatCue::MetallicPreRing: return "metallic_pre_ring";
        case CombatCue::LowReversedPulse: return "low_reversed_pulse";
        case CombatCue::DeflectCrack: return "deflect_crack";
        case CombatCue::PostureBreakImpact: return "posture_break_impact";
    }
    return "unknown";
}

// Telegraph cue played at wind-up start. Grabs cannot be guarded, so they share the unblockable pulse.
constexpr CombatCue telegraphCue(lt::anim::AttackClass cls) noexcept {
    return cls == lt::anim::AttackClass::Guardable ? CombatCue::MetallicPreRing : CombatCue::LowReversedPulse;
}

// Spatial shape of a cue: spread in [0,1] interpolated over the cue's duration.
struct CueShape {
    float durationSec = 0.0f;
    float spreadStart = 0.0f;
    float spreadEnd = 0.0f;
    std::uint8_t priority = 0;  // higher survives voice stealing
};

constexpr CueShape shapeOf(CombatCue c) noexcept {
    switch (c) {
        case CombatCue::MetallicPreRing: return {0.25f, 0.2f, 0.2f, 200};
        case CombatCue::LowReversedPulse: return {0.40f, 0.3f, 0.3f, 230};
        case CombatCue::DeflectCrack: return {0.12f, 0.1f, 0.1f, 220};
        case CombatCue::PostureBreakImpact: return {0.60f, 0.1f, 1.0f, 240};
    }
    return {};
}

constexpr float spreadAt(const CueShape& s, float t) noexcept {
    if (s.durationSec <= 0.0f || t >= s.durationSec) return s.spreadEnd;
    if (t <= 0.0f) return s.spreadStart;
    return s.spreadStart + (s.spreadEnd - s.spreadStart) * (t / s.durationSec);
}

// Section 24: each biome has exploration, danger, event, boss and aftermath stems.
enum class Stem : std::uint8_t { Exploration, Danger, Event, Boss, Aftermath };
inline constexpr std::size_t kStemCount = 5;

struct MusicContext {
    bool bossActive = false;
    bool eventActive = false;
    bool inCombat = false;
    bool aftermath = false;  // a boss or event resolved recently
};

// Highest-intensity state wins: boss > event > danger > aftermath > exploration.
constexpr Stem selectStem(const MusicContext& m) noexcept {
    if (m.bossActive) return Stem::Boss;
    if (m.eventActive) return Stem::Event;
    if (m.inCombat) return Stem::Danger;
    if (m.aftermath) return Stem::Aftermath;
    return Stem::Exploration;
}

}  // namespace lt::audio
