// TAnim attack markers (Sections 7, 16, 19): wind-up/contact/recovery phases,
// frame-rate independent hit frames at 30/60 FPS, dodge-cancel and perfect-deflect windows.
#include <cstdint>
#include <string>

#include "TestHarness.h"
#include "tanim/AnimMarkers.h"

using namespace lt::anim;

namespace {

// Plays a clip at a given render FPS and returns how many hits registered and the tick of the hit.
int playAtFps(const AttackClip& clip, float fps, std::uint32_t& hitTick) {
    AttackPlayer p;
    p.start(clip);
    float carry = 0.0f;
    int hits = 0;
    for (int frame = 0; frame < 10000 && p.active(); ++frame) {
        const std::uint32_t n = AttackPlayer::ticksForFrame(1.0f / fps, carry);
        if (p.advance(n)) {
            ++hits;
            hitTick = p.tick();
        }
    }
    return hits;
}

} // namespace

int main() {
    TEST_SECTION("bake");
    const AttackClip clip{"longsword.light", 0.35f, 0.10f, 0.45f, 0.20f, AttackClass::Guardable};
    const ClipTicks t = bake(clip);
    CHECK_EQ(t.contactStart, 42u);
    CHECK_EQ(t.contactEnd, 54u);
    CHECK_EQ(t.end, 108u);
    CHECK(t.cancellable);
    CHECK_EQ(t.cancel, 78u);

    TEST_SECTION("phases");
    CHECK(phaseAt(t, 0) == Phase::WindUp);
    CHECK(phaseAt(t, 41) == Phase::WindUp);
    CHECK(phaseAt(t, 42) == Phase::Contact);
    CHECK(phaseAt(t, 53) == Phase::Contact);
    CHECK(phaseAt(t, 54) == Phase::Recovery);
    CHECK(phaseAt(t, 107) == Phase::Recovery);
    CHECK(phaseAt(t, 108) == Phase::Done);

    TEST_SECTION("hit once at 30/60/144 FPS");
    std::uint32_t h30 = 0, h60 = 0, h144 = 0;
    CHECK_EQ(playAtFps(clip, 30.0f, h30), 1);
    CHECK_EQ(playAtFps(clip, 60.0f, h60), 1);
    CHECK_EQ(playAtFps(clip, 144.0f, h144), 1);
    // The hit must land inside (or at the frame that crossed into) the contact window.
    CHECK(h30 > t.contactStart && h30 <= t.contactEnd + 4);
    CHECK(h60 > t.contactStart && h60 <= t.contactEnd + 2);

    TEST_SECTION("short window crossed by a large step");
    const AttackClip quick{"gauntlet.jab", 0.10f, 0.01f, 0.10f, -1.0f, AttackClass::Guardable};
    AttackPlayer q;
    q.start(quick);
    CHECK(!q.advance(5));
    CHECK(q.advance(40));   // jumps over the whole 1-tick window: still one hit
    CHECK(!q.advance(40));
    CHECK(!q.active());

    TEST_SECTION("dodge cancel");
    AttackPlayer c;
    c.start(clip);
    c.advance(60);
    CHECK(!c.canDodgeCancel());
    c.advance(20);
    CHECK(c.canDodgeCancel());
    const auto heavy = defaultFamilyClips("greatblade")[1];
    AttackPlayer hv;
    hv.start(heavy);
    hv.advance(bake(heavy).end - 1);
    CHECK(hv.active());
    CHECK(!hv.canDodgeCancel());

    TEST_SECTION("perfect deflection window");
    GuardTiming g;
    CHECK(!g.guarding());
    CHECK(!g.isPerfect(100));
    g.raise(100);
    g.raise(150);  // holding guard does not reset the window
    CHECK(g.guarding());
    CHECK(g.isPerfect(100));
    CHECK(g.isPerfect(118));
    CHECK(!g.isPerfect(119));
    CHECK(!g.isPerfect(99));
    g.lower();
    CHECK(!g.guarding());

    TEST_SECTION("family defaults");
    for (const std::string fam : {"longsword", "greatblade", "hammer", "spear", "axe", "gauntlet", "sickle"}) {
        const auto clips = defaultFamilyClips(fam);
        CHECK_EQ(clips.size(), std::size_t(3));
        CHECK(clips[0].total() < clips[1].total());
        CHECK(clips[1].total() < clips[2].total());
        CHECK(clips[2].cls == AttackClass::Unblockable);
        CHECK(bake(clips[0]).contactEnd > bake(clips[0]).contactStart);
    }
    CHECK(defaultFamilyClips("greatblade")[0].windUp > defaultFamilyClips("spear")[0].windUp);

    return ::lt::test::finish("anim");
}
