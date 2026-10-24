// TPhysics tests (Sections 7, 15, 19): voxel collision, character controller,
// deterministic replay, streamer-backed solidity and anchored destruction.
#include <cmath>
#include <cstdint>

#include "TestHarness.h"
#include "tphysics/Physics.h"

using namespace lt;
using physics::CharacterController;
using physics::ControllerConfig;
using physics::ControllerInput;
using physics::SolidQuery;
using world::BlockPos;
using world::Vec3;

namespace {

bool near(double a, double b, double eps = 1e-3) { return std::abs(a - b) <= eps; }

const SolidQuery kFlatFloor = [](BlockPos p) { return p.y < 0; };

void run(CharacterController& c, const ControllerInput& in, const SolidQuery& s, int ticks) {
    for (int i = 0; i < ticks; ++i) c.step(in, s);
}

void testAabbAndSweep() {
    TEST_SECTION("aabb-sweep");
    const auto box = physics::AABB::fromFeet({0.5, 0.0, 0.5}, 0.3, 1.8);
    CHECK(near(box.min.x, 0.2) && near(box.max.x, 0.8));
    CHECK(near(box.min.y, 0.0) && near(box.max.y, 1.8));
    CHECK(!physics::overlapsSolid(box, kFlatFloor));
    auto sunk = physics::AABB::fromFeet({0.5, -0.5, 0.5}, 0.3, 1.8);
    CHECK(physics::overlapsSolid(sunk, kFlatFloor));

    auto falling = physics::AABB::fromFeet({0.5, 0.25, 0.5}, 0.3, 1.8);
    const double got = physics::sweepAxis(falling, 1, -1.0, kFlatFloor);
    CHECK(got < 0.0 && got > -1.0);
    CHECK(near(falling.min.y, 0.0));
    CHECK(!physics::overlapsSolid(falling, kFlatFloor));

    auto free = physics::AABB::fromFeet({0.5, 0.0, 0.5}, 0.3, 1.8);
    CHECK(near(physics::sweepAxis(free, 0, 0.5, kFlatFloor), 0.5, 1e-12));
    CHECK(physics::sweepAxis(free, 0, 0.0, kFlatFloor) == 0.0);
}

void testFallAndLand() {
    TEST_SECTION("fall-land");
    CharacterController c({}, {0.5, 5.0, 0.5});
    c.step({}, kFlatFloor);
    CHECK(!c.grounded());
    CHECK(c.velocity().y < 0.0);
    run(c, {}, kFlatFloor, 600);
    CHECK(c.grounded());
    CHECK(near(c.feet().y, 0.0));
    CHECK(c.velocity().y == 0.0 || c.velocity().y > -1.0);
    CHECK_EQ(c.ticks(), std::uint64_t{601});

    // Terminal velocity clamp on a long fall.
    CharacterController d({}, {0.5, 10000.0, 0.5});
    run(d, {}, kFlatFloor, 1200);
    CHECK(d.velocity().y >= physics::kTerminalFall - 1e-9);
    CHECK(near(d.velocity().y, physics::kTerminalFall, 1e-6));
}

void testWallAndWalk() {
    TEST_SECTION("walk-wall");
    const SolidQuery wall = [](BlockPos p) { return p.y < 0 || p.x >= 3; };
    CharacterController c({}, {0.5, 0.0, 0.5});
    run(c, {}, wall, 5);
    CHECK(c.grounded());
    ControllerInput fwd;
    fwd.moveX = 1.0;
    run(c, fwd, wall, 600);
    CHECK(c.feet().x <= 3.0 - 0.3 + 1e-6);
    CHECK(c.feet().x > 2.6);
    CHECK(near(c.feet().y, 0.0));
    CHECK(c.grounded());

    // Walk distance per second matches walkSpeed; diagonal input is normalised.
    CharacterController w({}, {0.5, 0.0, 0.5});
    run(w, {}, kFlatFloor, 5);
    const Vec3 start = w.feet();
    run(w, fwd, kFlatFloor, 120);
    CHECK(near(w.feet().x - start.x, ControllerConfig{}.walkSpeed, 1e-6));

    CharacterController diag({}, {0.5, 0.0, 0.5});
    run(diag, {}, kFlatFloor, 5);
    ControllerInput dIn;
    dIn.moveX = 1.0;
    dIn.moveZ = 1.0;
    const Vec3 s2 = diag.feet();
    run(diag, dIn, kFlatFloor, 120);
    const double dx = diag.feet().x - s2.x, dz = diag.feet().z - s2.z;
    CHECK(near(std::sqrt(dx * dx + dz * dz), ControllerConfig{}.walkSpeed, 1e-6));

    // Sprint is faster; armour weight slows locomotion (Section 12).
    CharacterController sprint({}, {0.5, 0.0, 0.5});
    run(sprint, {}, kFlatFloor, 5);
    ControllerInput sIn = fwd;
    sIn.sprint = true;
    run(sprint, sIn, kFlatFloor, 120);
    CHECK(sprint.feet().x > w.feet().x);

    ControllerConfig heavy;
    heavy.armourWeight = 50.0;
    CharacterController h(heavy, {0.5, 0.0, 0.5});
    run(h, {}, kFlatFloor, 5);
    run(h, fwd, kFlatFloor, 120);
    CHECK(h.feet().x < w.feet().x);
}

void testJumpAndLedge() {
    TEST_SECTION("jump-ledge");
    CharacterController c({}, {0.5, 0.0, 0.5});
    run(c, {}, kFlatFloor, 5);
    ControllerInput jump;
    jump.jump = true;
    c.step(jump, kFlatFloor);
    CHECK(!c.grounded());
    CHECK(c.velocity().y > 0.0);
    double apex = 0.0;
    for (int i = 0; i < 240; ++i) {
        c.step({}, kFlatFloor);
        apex = std::max(apex, c.feet().y);
    }
    CHECK(apex > 1.2 && apex < 1.7);  // v^2 / 2g = 8.5^2 / 48 ~= 1.5 m
    CHECK(c.grounded());
    CHECK(near(c.feet().y, 0.0));

    // Jumping while airborne does nothing.
    CharacterController air({}, {0.5, 3.0, 0.5});
    air.step({}, kFlatFloor);
    const double vy = air.velocity().y;
    air.step(jump, kFlatFloor);
    CHECK(air.velocity().y < vy);

    // A full-block ledge is taller than stepHeight: walking is blocked, jumping clears it.
    const SolidQuery ledge = [](BlockPos p) { return p.y < 0 || (p.x >= 2 && p.y <= 0); };
    CharacterController walker({}, {0.5, 0.0, 0.5});
    run(walker, {}, ledge, 5);
    ControllerInput fwd;
    fwd.moveX = 1.0;
    run(walker, fwd, ledge, 240);
    CHECK(near(walker.feet().y, 0.0));
    CHECK(walker.feet().x <= 2.0 - 0.3 + 1e-6);

    // Hold jump only long enough for a single hop (held jump re-triggers on landing).
    ControllerInput hop = fwd;
    hop.jump = true;
    run(walker, hop, ledge, 30);
    run(walker, fwd, ledge, 120);
    CHECK(walker.feet().x > 2.3);
    CHECK(near(walker.feet().y, 1.0));
    CHECK(walker.grounded());
}

void testStepUp() {
    TEST_SECTION("step-up");
    // Larger step height lets the controller climb a 1-block rise without jumping.
    ControllerConfig cfg;
    cfg.stepHeight = 1.05;
    const SolidQuery ledge = [](BlockPos p) { return p.y < 0 || (p.x >= 2 && p.y <= 0); };
    CharacterController c(cfg, {0.5, 0.0, 0.5});
    run(c, {}, ledge, 5);
    ControllerInput fwd;
    fwd.moveX = 1.0;
    run(c, fwd, ledge, 240);
    CHECK(c.feet().x > 2.3);
    CHECK(near(c.feet().y, 1.0, 0.05));
    run(c, {}, ledge, 120);
    CHECK(c.grounded());
    CHECK(near(c.feet().y, 1.0));

    // Ceiling blocks step-up: no room above the ledge.
    const SolidQuery capped = [](BlockPos p) {
        return p.y < 0 || (p.x >= 2 && p.y <= 0) || (p.x >= 2 && p.y >= 1 && p.y <= 2);
    };
    CharacterController b(cfg, {0.5, 0.0, 0.5});
    run(b, {}, capped, 5);
    run(b, fwd, capped, 240);
    CHECK(b.feet().x <= 2.0 - 0.3 + 1e-6);
    CHECK(near(b.feet().y, 0.0));
}

void testDeterminism() {
    TEST_SECTION("determinism");
    const SolidQuery terrain = [](BlockPos p) {
        return p.y < 0 || (p.x >= 4 && p.y <= 0) || (p.z >= 6 && p.y <= 1);
    };
    auto script = [](int i) {
        ControllerInput in;
        in.moveX = std::sin(i * 0.05);
        in.moveZ = std::cos(i * 0.031);
        in.sprint = (i / 90) % 2 == 0;
        in.jump = i % 47 == 0;
        return in;
    };
    CharacterController a({}, {0.5, 2.0, 0.5}), b({}, {0.5, 2.0, 0.5});
    bool same = true;
    for (int i = 0; i < 2400; ++i) {
        a.step(script(i), terrain);
        b.step(script(i), terrain);
        same = same && a.stateHash() == b.stateHash();
    }
    CHECK(same);
    CHECK_EQ(a.ticks(), b.ticks());

    CharacterController c({}, {0.5, 2.0, 0.5});
    for (int i = 0; i < 2400; ++i) c.step(script(i + 1), terrain);
    CHECK(c.stateHash() != a.stateHash());

    a.teleport({10.5, 3.0, 10.5});
    CHECK(near(a.feet().x, 10.5) && near(a.feet().y, 3.0));
    CHECK(a.velocity().y == 0.0);
    CHECK(!a.grounded());
}

void testStreamerSolidity() {
    TEST_SECTION("streamer-solid");
    constexpr world::BlockId kStone = 1;
    world::ChunkStreamer streamer(
        0xC0FFEEull, 1,
        [](world::Chunk& chunk, std::uint64_t) {
            const std::int64_t bx = chunk.pos().x * world::kChunkSizeX;
            const std::int64_t bz = chunk.pos().z * world::kChunkSizeZ;
            for (std::int64_t z = 0; z < world::kChunkSizeZ; ++z)
                for (std::int64_t x = 0; x < world::kChunkSizeX; ++x)
                    chunk.set(world::localInChunk(BlockPos{bx + x, 0, bz + z}), kStone);
        });
    streamer.update(world::ChunkPos{0, 0});
    const auto solid = physics::solidFromStreamer(streamer);
    CHECK(solid(BlockPos{0, 0, 0}));
    CHECK(solid(BlockPos{-5, 0, 7}));
    CHECK(!solid(BlockPos{0, 5, 0}));
    CHECK(solid(BlockPos{0, -1, 0}));
    CHECK(solid(BlockPos{10000, 50, 10000}));  // unloaded chunk treated as solid

    CharacterController c({}, {4.5, 6.0, 4.5});
    run(c, {}, solid, 600);
    CHECK(c.grounded());
    CHECK(near(c.feet().y, 1.0));
}

void testDestruction() {
    TEST_SECTION("destruction");
    constexpr world::BlockId kStone = 1, kWood = 2, kBedrock = 3;
    physics::DestructionSystem d;
    d.setMaterial(kStone, {100.0f, 0.5f, 0.0f, true});
    d.setMaterial(kWood, {40.0f, 1.0f, 2.0f, true});
    d.setMaterial(kBedrock, {1.0f, 1.0f, 1.0f, false});
    d.addAnchor({10, 0, 10}, {12, 4, 12});

    const BlockPos stone{0, 0, 0};
    CHECK(!d.damage(stone, kStone, 100.0f));
    CHECK(std::abs(d.damageAt(stone) - 50.0f) < 1e-4f);
    CHECK(!d.damage(stone, kStone, 0.0f, 1000.0f));  // stone ignores ash
    CHECK(d.damage(stone, kStone, 100.0f));
    CHECK(d.damageAt(stone) == 0.0f);

    const BlockPos wood{1, 0, 0};
    CHECK(d.damage(wood, kWood, 0.0f, 20.0f));  // wood burns at 2x

    CHECK(!d.damage({2, 0, 0}, kBedrock, 1e6f));
    CHECK(!d.damage({3, 0, 0}, world::kAir, 1e6f));

    CHECK(d.anchored({11, 2, 11}));
    CHECK(!d.anchored({13, 2, 11}));
    CHECK(!d.damage({11, 2, 11}, kWood, 1e6f));
    CHECK(d.damageAt({11, 2, 11}) == 0.0f);

    // Unknown block ids use the default material (100 health, 1x).
    CHECK(!d.damage({5, 0, 5}, 99, 99.0f));
    CHECK(d.damage({5, 0, 5}, 99, 1.0f));

    const auto broken = d.drainBroken();
    CHECK_EQ(broken.size(), std::size_t{3});
    if (broken.size() == 3) {
        CHECK(broken[0].pos == stone && broken[0].previous == kStone);
        CHECK(broken[1].pos == wood && broken[1].previous == kWood);
        CHECK(broken[2].previous == 99);
    }
    CHECK(d.drainBroken().empty());
}

}  // namespace

int main() {
    testAabbAndSweep();
    testFallAndLand();
    testWallAndWalk();
    testJumpAndLedge();
    testStepUp();
    testDeterminism();
    testStreamerSolidity();
    testDestruction();
    return lt::test::finish("physics");
}
