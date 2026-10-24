#pragma once
// ThaumtMourge game layer (Milestone B vertical slice, Sections 7, 17, 19, 22).
// Headless fixed-tick player simulation: semantic input -> character controller,
// chunk streaming around the player, pause handling and deterministic state hashing.
#include <cstdint>
#include <string>

#include "tcore/Input.h"
#include "tcore/Log.h"
#include "tcore/Random.h"
#include "tphysics/Physics.h"
#include "tworld/Chunk.h"
#include "tworld/WorldCoord.h"

namespace lt::game {

struct PlayerSimConfig {
    std::uint64_t worldSeed = 0;
    int streamRadius = 2;
    world::Vec3 spawn{0.5, 0.0, 0.5};
    physics::ControllerConfig controller{};
};

class PlayerSim {
public:
    PlayerSim(const PlayerSimConfig& cfg, world::ChunkStreamer::Generator gen, core::Logger& log)
        : log_(&log),
          streamer_(cfg.worldSeed, cfg.streamRadius, std::move(gen)),
          controller_(cfg.controller, cfg.spawn),
          solid_(physics::solidFromStreamer(streamer_)) {
        center_ = world::chunkOf(feetBlock());
        const auto r = streamer_.update(center_);
        log_->info("world: initial stream loaded " + std::to_string(r.loaded) + " chunks");
    }

    PlayerSim(const PlayerSim&) = delete;
    PlayerSim& operator=(const PlayerSim&) = delete;

    // One fixed simulation tick (physics::kTickDt). The caller owns InputState frame edges.
    void tick(const input::InputState& in) {
        if (in.pressed(input::Action::Pause)) {
            paused_ = !paused_;
            log_->info(paused_ ? "game: paused" : "game: resumed");
        }
        if (paused_) return;

        const input::Vec2 move = in.axis(input::Action::Move);
        physics::ControllerInput ci;
        ci.moveX = move.x;
        ci.moveZ = move.y;
        ci.jump = in.down(input::Action::Jump);
        controller_.step(ci, solid_);
        ++ticks_;

        const world::ChunkPos c = world::chunkOf(feetBlock());
        if (!(c == center_)) {
            center_ = c;
            const auto r = streamer_.update(center_);
            ++streamUpdates_;
            log_->debug("world: streamed +" + std::to_string(r.loaded) + " -" +
                        std::to_string(r.unloaded) + " chunks");
        }
    }

    bool paused() const noexcept { return paused_; }
    std::uint64_t ticks() const noexcept { return ticks_; }
    std::uint64_t streamUpdates() const noexcept { return streamUpdates_; }
    const physics::CharacterController& controller() const noexcept { return controller_; }
    const world::ChunkStreamer& streamer() const noexcept { return streamer_; }

    // Section 19 simulation gate: identical inputs must give identical hashes.
    std::uint64_t stateHash() const {
        std::uint64_t h = core::hashCombine(controller_.stateHash(), ticks_);
        return core::hashCombine(h, paused_ ? 1u : 0u);
    }

private:
    world::BlockPos feetBlock() const {
        const world::Vec3 f = controller_.feet();
        return world::BlockPos{physics::cellOf(f.x), physics::cellOf(f.y), physics::cellOf(f.z)};
    }

    core::Logger* log_;
    world::ChunkStreamer streamer_;
    physics::CharacterController controller_;
    physics::SolidQuery solid_;
    world::ChunkPos center_{};
    bool paused_ = false;
    std::uint64_t ticks_ = 0;
    std::uint64_t streamUpdates_ = 0;
};

} // namespace lt::game
