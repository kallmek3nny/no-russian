// TAudio (Sections 15, 23, 24): attack-class telegraph cues, deflect/posture-break cue shapes,
// and biome music stem selection priority.
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>

#include "TestHarness.h"
#include "taudio/Audio.h"

using namespace lt::audio;
using lt::anim::AttackClass;

namespace {

bool approx(float a, float b) { return std::fabs(a - b) < 1e-4f; }

}  // namespace

int main() {
    TEST_SECTION("telegraph cue by attack class");
    static_assert(telegraphCue(AttackClass::Guardable) == CombatCue::MetallicPreRing);
    CHECK(telegraphCue(AttackClass::Guardable) == CombatCue::MetallicPreRing);
    CHECK(telegraphCue(AttackClass::Unblockable) == CombatCue::LowReversedPulse);
    CHECK(telegraphCue(AttackClass::Grab) == CombatCue::LowReversedPulse);

    TEST_SECTION("family clips drive cues");
    for (const char* fam : {"longsword", "greatblade", "spear", "gauntlet"}) {
        const auto clips = lt::anim::defaultFamilyClips(std::string(fam));
        CHECK(telegraphCue(clips[0].cls) == CombatCue::MetallicPreRing);
        CHECK(telegraphCue(clips[2].cls) == CombatCue::LowReversedPulse);
    }

    TEST_SECTION("cue names are unique and known");
    std::set<std::string_view> names;
    for (std::size_t i = 0; i < kCombatCueCount; ++i) {
        names.insert(toString(static_cast<CombatCue>(i)));
    }
    CHECK_EQ(names.size(), kCombatCueCount);
    CHECK(names.count("unknown") == 0);

    TEST_SECTION("deflect crack is short");
    const CueShape crack = shapeOf(CombatCue::DeflectCrack);
    CHECK(crack.durationSec > 0.0f);
    for (std::size_t i = 0; i < kCombatCueCount; ++i) {
        const auto c = static_cast<CombatCue>(i);
        if (c != CombatCue::DeflectCrack) {
            CHECK(crack.durationSec < shapeOf(c).durationSec);
        }
    }

    TEST_SECTION("posture break widens spatially");
    const CueShape brk = shapeOf(CombatCue::PostureBreakImpact);
    CHECK(brk.spreadEnd > brk.spreadStart);
    CHECK(approx(spreadAt(brk, 0.0f), brk.spreadStart));
    CHECK(approx(spreadAt(brk, -1.0f), brk.spreadStart));
    CHECK(approx(spreadAt(brk, brk.durationSec), brk.spreadEnd));
    CHECK(approx(spreadAt(brk, 99.0f), brk.spreadEnd));
    CHECK(approx(spreadAt(brk, brk.durationSec * 0.5f), 0.5f * (brk.spreadStart + brk.spreadEnd)));
    CHECK(spreadAt(brk, 0.1f) < spreadAt(brk, 0.3f));
    for (std::size_t i = 0; i < kCombatCueCount; ++i) {
        const auto c = static_cast<CombatCue>(i);
        if (c != CombatCue::PostureBreakImpact) {
            const CueShape s = shapeOf(c);
            CHECK(approx(spreadAt(s, 0.0f), spreadAt(s, s.durationSec)));
        }
    }

    TEST_SECTION("posture break survives voice stealing");
    for (std::size_t i = 0; i < kCombatCueCount; ++i) {
        const auto c = static_cast<CombatCue>(i);
        if (c != CombatCue::PostureBreakImpact) {
            CHECK(brk.priority > shapeOf(c).priority);
        }
    }

    TEST_SECTION("stem priority");
    MusicContext m;
    CHECK(selectStem(m) == Stem::Exploration);
    m.aftermath = true;
    CHECK(selectStem(m) == Stem::Aftermath);
    m.inCombat = true;
    CHECK(selectStem(m) == Stem::Danger);
    m.eventActive = true;
    CHECK(selectStem(m) == Stem::Event);
    m.bossActive = true;
    CHECK(selectStem(m) == Stem::Boss);
    m = MusicContext{};
    m.bossActive = true;
    CHECK(selectStem(m) == Stem::Boss);

    return ::lt::test::finish("audio");
}
