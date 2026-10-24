// Milestone B combat tests: four vertical-slice weapons, move timing windows,
// frame-rate independent hit markers, and hit resolution against defences.
#include <cmath>
#include <set>
#include <string>

#include "TestHarness.h"
#include "tcombat/Weapon.h"

using namespace lt::combat;

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

constexpr std::size_t ch(Channel c) { return static_cast<std::size_t>(c); }

} // namespace

int main() {
    const auto set = verticalSliceWeapons();

    TEST_SECTION("weapon set");
    CHECK_EQ(set.size(), std::size_t(4));
    std::set<std::string> ids;
    std::set<int> families;
    for (const auto& w : set) {
        ids.insert(w.id);
        families.insert(static_cast<int>(w.family));
        CHECK(!w.displayName.empty());
        CHECK(!w.moveset.empty());
        CHECK(!w.loreKey.empty());
        CHECK(!w.sockets.empty());
        CHECK(w.posture > 0.0f);
        CHECK(w.weight > 0.0f);
        for (std::size_t k = 0; k < kMoveKindCount; ++k) CHECK(w.moves[k].activeMs > 0);
    }
    CHECK_EQ(ids.size(), std::size_t(4));
    CHECK_EQ(families.size(), std::size_t(4));

    TEST_SECTION("lookup");
    const auto fang = findWeapon(set, "weapon.aetherfang");
    const auto sword = findWeapon(set, "weapon.order_longsword");
    const auto spear = findWeapon(set, "weapon.frostreach_spear");
    const auto bow = findWeapon(set, "weapon.wardens_bow");
    CHECK(fang.has_value());
    CHECK(sword.has_value());
    CHECK(spear.has_value());
    CHECK(bow.has_value());
    CHECK(!findWeapon(set, "weapon.missing").has_value());
    if (!fang || !sword || !spear || !bow) return ::lt::test::finish("weapon");
    CHECK(fang->family == Family::Greatblade);
    CHECK(bow->family == Family::Bow);

    TEST_SECTION("move timing");
    const MoveTiming& light = sword->move(MoveKind::Light);
    CHECK_EQ(light.windupMs, 300u);
    CHECK_EQ(light.activeMs, 100u);
    CHECK_EQ(light.totalMs(), 800u);
    CHECK(!isContact(light, 0));
    CHECK(!isContact(light, 299));
    CHECK(isContact(light, 300));
    CHECK(isContact(light, 399));
    CHECK(!isContact(light, 400));
    const MoveTiming& heavy = sword->move(MoveKind::Heavy);
    CHECK(heavy.windupMs > light.windupMs);
    CHECK(heavy.staminaCost > light.staminaCost);
    CHECK(fang->move(MoveKind::Light).windupMs > sword->move(MoveKind::Light).windupMs);
    CHECK(near(sword->move(MoveKind::Execution).postureScale, 0.0f));

    TEST_SECTION("frame markers 30/60 fps");
    CHECK_EQ(msToFrame(300, 30), 9u);
    CHECK_EQ(msToFrame(300, 60), 18u);
    CHECK_EQ(msToFrame(0, 60), 0u);
    CHECK_EQ(msToFrame(1000, 30), 30u);
    CHECK_EQ(msToFrame(1000, 60), 60u);
    // Same wall-clock contact start regardless of tick rate.
    CHECK_EQ(msToFrame(light.windupMs, 60), 2u * msToFrame(light.windupMs, 30));

    TEST_SECTION("hit resolution");
    const DamageVec neutral = neutralResist();
    HitResult open = resolveHit(*fang, MoveKind::Light, neutral, Defence::None);
    CHECK(near(open.healthDamage, 50.0f));
    CHECK(near(open.postureDamageToDefender, 38.0f));
    CHECK(near(open.postureDamageToAttacker, 0.0f));
    HitResult heavyHit = resolveHit(*fang, MoveKind::Heavy, neutral, Defence::None);
    CHECK(near(heavyHit.healthDamage, 80.0f));
    CHECK(near(heavyHit.postureDamageToDefender, 68.4f));

    HitResult guarded = resolveHit(*fang, MoveKind::Light, neutral, Defence::Guard);
    CHECK(near(guarded.healthDamage, 10.0f));
    CHECK(near(guarded.postureDamageToDefender, 22.8f));
    CHECK(near(guarded.staminaCostToDefender, 25.0f));

    HitResult deflected = resolveHit(*fang, MoveKind::Light, neutral, Defence::PerfectDeflect);
    CHECK(near(deflected.healthDamage, 0.0f));
    CHECK(near(deflected.postureDamageToDefender, 0.0f));
    CHECK(near(deflected.postureDamageToAttacker, 28.5f));

    HitResult dodged = resolveHit(*fang, MoveKind::Heavy, neutral, Defence::DodgeInvulnerable);
    CHECK(near(dodged.healthDamage, 0.0f));
    CHECK(near(dodged.postureDamageToDefender, 0.0f));
    CHECK(near(dodged.postureDamageToAttacker, 0.0f));
    CHECK(near(dodged.staminaCostToDefender, 0.0f));

    TEST_SECTION("resistances");
    DamageVec frostWeak = neutral;
    frostWeak[ch(Channel::Frost)] = 1.35f;
    HitResult weak = resolveHit(*spear, MoveKind::Light, frostWeak, Defence::None);
    CHECK(near(weak.healthDamage, 37.5f));
    DamageVec physResist = neutral;
    physResist[ch(Channel::Physical)] = 0.8f;
    HitResult resisted = resolveHit(*sword, MoveKind::Light, physResist, Defence::None);
    CHECK(near(resisted.healthDamage, 22.4f));
    // Resistances scale health damage only, not posture.
    CHECK(near(resisted.postureDamageToDefender, 22.0f));

    TEST_SECTION("determinism");
    const auto again = verticalSliceWeapons();
    for (std::size_t i = 0; i < set.size(); ++i) {
        CHECK(set[i].id == again[i].id);
        CHECK(set[i].damage == again[i].damage);
        CHECK_EQ(set[i].move(MoveKind::Charged).totalMs(), again[i].move(MoveKind::Charged).totalMs());
    }

    return ::lt::test::finish("weapon");
}
