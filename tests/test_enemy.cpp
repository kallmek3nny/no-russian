// Milestone C enemy tests: section 9 families, section 21 schema fidelity,
// deterministic variants, perception, and posture/health runtime state.
#include <cmath>
#include <set>
#include <string>

#include "TestHarness.h"
#include "tcombat/Enemy.h"

using namespace lt::combat;

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

constexpr std::size_t ch(Channel c) { return static_cast<std::size_t>(c); }

bool hasModifier(const EnemyVariant& v, const std::string& m) {
    for (const auto& x : v.modifiers) if (x == m) return true;
    return false;
}

bool hasPrefix(const EnemyVariant& v, const std::string& p) {
    for (const auto& x : v.modifiers) if (x.rfind(p, 0) == 0) return true;
    return false;
}

} // namespace

int main() {
    const auto set = commonEnemyFamilies();

    TEST_SECTION("enemy families");
    CHECK_EQ(set.size(), std::size_t(11));
    std::set<std::string> ids;
    for (const auto& e : set) {
        ids.insert(e.id);
        CHECK(e.perception > 0.0f);
        CHECK(e.posture > 0.0f);
        CHECK(e.health > 0.0f);
        CHECK(!e.attackSet.empty());
        CHECK(!e.drops.empty());
        CHECK(!e.biomes.empty());
        CHECK(!e.eventMutations.empty());
    }
    CHECK_EQ(ids.size(), std::size_t(11));

    TEST_SECTION("schema record");
    const auto knight = findEnemy(set, "enemy.frostbound_knight");
    const auto golem = findEnemy(set, "enemy.golem_miner");
    CHECK(knight.has_value());
    CHECK(golem.has_value());
    CHECK(!findEnemy(set, "enemy.missing").has_value());
    if (!knight || !golem) return ::lt::test::finish("enemy");
    CHECK(knight->role == EnemyRole::Duelist);
    CHECK(near(knight->perception, 26.0f));
    CHECK(near(knight->posture, 120.0f));
    CHECK(near(knight->resistances[ch(Channel::Frost)], 0.8f));
    CHECK(near(knight->resistances[ch(Channel::Ash)], 1.35f));
    CHECK(knight->attackSet == "frost_knight_v2");
    CHECK_EQ(knight->drops.size(), std::size_t(2));
    CHECK(knight->drops[1] == "armor.frostbound_plate");
    CHECK(knight->biomes.size() == 1 && knight->biomes[0] == "frost_crown");
    CHECK_EQ(knight->eventMutations.size(), std::size_t(3));

    TEST_SECTION("perception");
    CHECK(perceives(*knight, 26.0f, false));
    CHECK(!perceives(*knight, 27.0f, false));
    CHECK(perceives(*knight, 13.0f, true));
    CHECK(!perceives(*knight, 14.0f, true));

    TEST_SECTION("neutral golem");
    EnemyState gs = spawnState(*golem);
    CHECK(!gs.provoked);
    CHECK(spawnState(*knight).provoked);
    const auto weapons = verticalSliceWeapons();
    const auto fang = findWeapon(weapons, "weapon.aetherfang");
    CHECK(fang.has_value());
    if (!fang) return ::lt::test::finish("enemy");
    applyHit(*golem, gs, resolveHit(*fang, MoveKind::Light, golem->resistances, Defence::DodgeInvulnerable));
    CHECK(!gs.provoked);  // a dodged swing does not count as theft/aggression
    applyHit(*golem, gs, resolveHit(*fang, MoveKind::Light, golem->resistances, Defence::None));
    CHECK(gs.provoked);

    TEST_SECTION("posture break and death");
    EnemyState ks = spawnState(*knight);
    for (int i = 0; i < 3; ++i)
        applyHit(*knight, ks, resolveHit(*fang, MoveKind::Light, knight->resistances, Defence::None));
    CHECK(near(ks.health, 70.0f));
    CHECK(near(ks.posture, 114.0f));
    CHECK(!ks.postureBroken);
    CHECK(!ks.dead());
    applyHit(*knight, ks, resolveHit(*fang, MoveKind::Light, knight->resistances, Defence::None));
    CHECK(ks.postureBroken);
    CHECK(near(ks.health, 20.0f));
    applyHit(*knight, ks, resolveHit(*fang, MoveKind::Light, knight->resistances, Defence::None));
    CHECK(ks.dead());

    TEST_SECTION("variant determinism");
    VariantContext ctx;
    ctx.worldSeed = 0xC0FFEEull;
    ctx.spawnId = 42;
    ctx.biome = "mire_of_glass";
    ctx.faction = "ash_tribunal";
    ctx.flux = 0.5f;
    ctx.eventScar = "champion";
    const EnemyVariant a = makeVariant(*knight, ctx);
    const EnemyVariant b = makeVariant(*knight, ctx);
    CHECK(a.modifiers == b.modifiers);
    CHECK(a.def.resistances == b.def.resistances);
    CHECK(near(a.def.health, b.def.health));
    CHECK(near(a.def.posture, b.def.posture));

    TEST_SECTION("variant layers");
    CHECK(near(a.def.health, 220.0f * 1.1f * 1.5f));
    CHECK(a.def.attackSet == "frost_knight_v2+ash_tribunal");
    CHECK(hasModifier(a, "faction_kit:ash_tribunal"));
    CHECK(hasModifier(a, "event_scar:champion"));
    CHECK(hasModifier(a, "biome_adaptation:mire_of_glass"));
    CHECK(near(a.def.resistances[ch(Channel::Lightning)], 1.25f));  // wet enemies conduct lightning

    VariantContext native = ctx;
    native.biome = "frost_crown";
    native.faction.clear();
    native.eventScar = "molten";  // not in the knight's allowed mutations
    const EnemyVariant n = makeVariant(*knight, native);
    CHECK(!hasPrefix(n, "biome_adaptation:"));
    CHECK(!hasPrefix(n, "faction_kit:"));
    CHECK(!hasPrefix(n, "event_scar:"));
    CHECK(near(n.def.health, 220.0f));
    CHECK(n.def.attackSet == "frost_knight_v2");

    VariantContext tainted = native;
    tainted.eventScar = "tainted";
    const EnemyVariant t = makeVariant(*knight, tainted);
    CHECK(hasModifier(t, "event_scar:tainted"));
    CHECK(t.def.resistances[ch(Channel::Void)] < 0.0f);  // tainted enemies heal from void

    TEST_SECTION("flux drives aspect mutation");
    int calm = 0, stormy = 0, seedDiffers = 0;
    for (std::uint64_t s = 0; s < 1000; ++s) {
        VariantContext c;
        c.worldSeed = 7;
        c.spawnId = s;
        c.flux = 0.0f;
        const bool m0 = hasPrefix(makeVariant(*knight, c), "aspect_mutation:");
        c.flux = 1.0f;
        const bool m1 = hasPrefix(makeVariant(*knight, c), "aspect_mutation:");
        calm += m0 ? 1 : 0;
        stormy += m1 ? 1 : 0;
        c.worldSeed = 8;
        if (hasPrefix(makeVariant(*knight, c), "aspect_mutation:") != m1) ++seedDiffers;
    }
    CHECK(calm > 30 && calm < 200);
    CHECK(stormy > 550 && stormy < 850);
    CHECK(stormy > calm);
    CHECK(seedDiffers > 0);

    return ::lt::test::finish("enemy");
}
