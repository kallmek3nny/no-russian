// Game layer tests (Milestone B, Sections 17, 19, 22): input-driven locomotion,
// streaming follows the player, pause, focus-loss release and deterministic replay.
#include <cstdint>

#include "TestHarness.h"
#include "game/PlayerSim.h"

using namespace lt;
using input::Action;

namespace {

void emptyGen(world::Chunk&, std::uint64_t) {}

game::PlayerSimConfig cfg() {
    game::PlayerSimConfig c;
    c.worldSeed = 1234;
    c.streamRadius = 1;
    return c;
}

void frame(input::InputState& in, game::PlayerSim& sim, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        sim.tick(in);
        in.beginFrame();
    }
}

void testWalkForward() {
    TEST_SECTION("walk-forward");
    core::Logger log;
    game::PlayerSim sim(cfg(), emptyGen, log);
    CHECK(sim.streamer().loadedCount() > 0);
    const auto map = input::ActionMap::desktopDefaults();
    input::InputState in(map);
    frame(in, sim, 30);
    CHECK(sim.controller().grounded());
    const double z0 = sim.controller().feet().z;
    in.onButton(input::keyboard('W'), true);
    frame(in, sim, 120);
    const double dz = sim.controller().feet().z - z0;
    CHECK(dz > 3.5 && dz < 5.5);
    CHECK(sim.controller().grounded());
    in.releaseAll();
    const double z1 = sim.controller().feet().z;
    frame(in, sim, 30);
    CHECK(sim.controller().feet().z - z1 < 1e-9);
}

void testStreamingFollows() {
    TEST_SECTION("streaming");
    core::Logger log;
    log.setMinLevel(core::LogLevel::Debug);
    game::PlayerSim sim(cfg(), emptyGen, log);
    const auto map = input::ActionMap::desktopDefaults();
    input::InputState in(map);
    in.onButton(input::keyboard('W'), true);
    frame(in, sim, 120 * 12);
    CHECK(sim.streamUpdates() > 0);
    CHECK(sim.streamer().loadedCount() == 9u);
    CHECK(log.totalLines() > 1u);
}

void testPause() {
    TEST_SECTION("pause");
    core::Logger log;
    game::PlayerSim sim(cfg(), emptyGen, log);
    const auto map = input::ActionMap::desktopDefaults();
    input::InputState in(map);
    in.onButton(input::keyCode(input::keys::kEscape), true);
    sim.tick(in);
    in.beginFrame();
    CHECK(sim.paused());
    in.onButton(input::keyCode(input::keys::kEscape), false);
    in.onButton(input::keyboard('W'), true);
    const auto t = sim.ticks();
    frame(in, sim, 60);
    CHECK(sim.ticks() == t);
    in.onButton(input::keyCode(input::keys::kEscape), true);
    sim.tick(in);
    CHECK(!sim.paused());
}

std::uint64_t replay() {
    core::Logger log;
    game::PlayerSim sim(cfg(), emptyGen, log);
    const auto map = input::ActionMap::desktopDefaults();
    input::InputState in(map);
    in.onButton(input::keyboard('D'), true);
    frame(in, sim, 90);
    in.onButton(input::keyCode(input::keys::kSpace), true);
    frame(in, sim, 5);
    in.onButton(input::keyCode(input::keys::kSpace), false);
    in.onButton(input::keyboard('W'), true);
    frame(in, sim, 200);
    return sim.stateHash();
}

void testDeterminism() {
    TEST_SECTION("determinism");
    CHECK(replay() == replay());
}

} // namespace

int main() {
    testWalkForward();
    testStreamingFollows();
    testPause();
    testDeterminism();
    return lt::test::finish("game");
}
