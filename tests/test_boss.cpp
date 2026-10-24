// Milestone B boss tests: The Frost Warden phases, posture stagger, breakable armour,
// moveset gating, and deterministic attack selection.
#include <cmath>
#include <string>

#include "TestHarness.h"
#include "tcombat/Boss.h"

using namespace lt::combat;

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

} // namespace

int main() {
    TEST_SECTION("definition");
    {
        BossState boss(frostWarden(), 7);
        CHECK(boss.def().id == "boss.frost_warden");
        CHECK(!boss.def().displayName.empty());
        CHECK_EQ(boss.def().armour.size(), std::size_t(3));
        CHECK_EQ(boss.def().attacks.size(), std::size_t(6));
        CHECK(near(boss.health(), 2400.0f));
        CHECK_EQ(boss.phase(), 1);
        CHECK(!boss.dead());
        CHECK(!boss.staggered());
        CHECK_EQ(boss.availableAttacks().size(), std::size_t(3));
    }

    TEST_SECTION("armour break");
    {
        BossState boss(frostWarden(), 7);
        CHECK(boss.armourIntact("armour.helm"));
        boss.applyHit(0.0f, 0.0f, "armour.helm", 150.0f);
        CHECK(boss.armourIntact("armour.helm"));
        CHECK(boss.openRoutes().empty());
        boss.applyHit(0.0f, 0.0f, "armour.helm", 100.0f);
        CHECK(!boss.armourIntact("armour.helm"));
        CHECK_EQ(boss.openRoutes().size(), std::size_t(1));
        CHECK(boss.openRoutes()[0] == "route.upper_rampart");
        boss.applyHit(0.0f, 0.0f, "armour.helm", 100.0f);
        CHECK_EQ(boss.openRoutes().size(), std::size_t(1));
        CHECK_EQ(boss.availableAttacks().size(), std::size_t(2));
        for (const auto* atk : boss.availableAttacks()) CHECK(atk->id != "atk.helm_charge");
        CHECK(!boss.armourIntact("armour.missing"));
    }

    TEST_SECTION("posture and stagger");
    {
        BossState boss(frostWarden(), 7);
        CHECK(!boss.applyHit(10.0f, 100.0f));
        CHECK(!boss.applyHit(10.0f, 100.0f));
        CHECK(boss.applyHit(10.0f, 100.0f));
        CHECK(boss.staggered());
        CHECK(near(boss.posture(), 0.0f));
        CHECK(boss.chooseAttack() == nullptr);
        CHECK(!boss.applyHit(10.0f, 100.0f));
        CHECK(near(boss.posture(), 0.0f));
        CHECK(near(boss.health(), 2360.0f));
        boss.tick(BossState::kStaggerMs - 1);
        CHECK(boss.staggered());
        boss.tick(1);
        CHECK(!boss.staggered());
        CHECK(boss.chooseAttack() != nullptr);
    }

    TEST_SECTION("posture regen");
    {
        BossState boss(frostWarden(), 7);
        boss.applyHit(0.0f, 100.0f);
        boss.tick(1000);
        CHECK(near(boss.posture(), 80.0f));
        boss.tick(10000);
        CHECK(near(boss.posture(), 0.0f));
    }

    TEST_SECTION("phases");
    {
        BossState boss(frostWarden(), 7);
        boss.applyHit(900.0f, 0.0f);
        CHECK_EQ(boss.phase(), 2);
        CHECK_EQ(boss.availableAttacks().size(), std::size_t(5));
        boss.applyHit(800.0f, 0.0f);
        CHECK_EQ(boss.phase(), 3);
        CHECK_EQ(boss.availableAttacks().size(), std::size_t(6));
        boss.applyHit(5000.0f, 0.0f);
        CHECK(boss.dead());
        CHECK(near(boss.health(), 0.0f));
        CHECK(boss.chooseAttack() == nullptr);
        CHECK(!boss.applyHit(10.0f, 1000.0f));
    }

    TEST_SECTION("determinism");
    {
        BossState a(frostWarden(), 42);
        BossState b(frostWarden(), 42);
        for (int i = 0; i < 32; ++i) {
            const auto* x = a.chooseAttack();
            const auto* y = b.chooseAttack();
            CHECK(x != nullptr);
            CHECK(y != nullptr);
            if (x && y) CHECK(x->id == y->id);
        }
    }

    return ::lt::test::finish("boss");
}
