#pragma once
// Legend Engine T - TPhysics (design doc sections 7, 15, 19 and 27).
// Voxel AABB collision, a fixed-tick character controller (gravity, jump, step-up, ground probe),
// and material-health destruction with structural anchors that protect authored spaces.
// All stepping is fixed-tick and branch-deterministic so replays produce identical state.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "tcore/Random.h"
#include "tworld/Chunk.h"
#include "tworld/WorldCoord.h"

namespace lt::physics {

using world::BlockId;
using world::BlockPos;
using world::Vec3;

// Simulation runs at the same fixed 120 Hz as TAnim so hit frames and movement share one clock.
inline constexpr double kTickDt = 1.0 / 120.0;
inline constexpr double kGravity = -24.0;     // m/s^2, tuned for weighty third-person jumps
inline constexpr double kTerminalFall = -55.0;
inline constexpr double kSkin = 1e-4;

// Answers "is this cell solid?" so physics never depends on how chunks are stored or streamed.
using SolidQuery = std::function<bool(BlockPos)>;

// Builds a SolidQuery over a ChunkStreamer; unloaded chunks are treated as solid so actors never
// fall through the world while streaming catches up.
inline SolidQuery solidFromStreamer(const world::ChunkStreamer& s) {
    return [&s](BlockPos p) {
        const world::Chunk* c = s.find(world::chunkOf(p));
        if (!c) return true;
        if (p.y < 0) return true;
        return c->get(world::localInChunk(p)) != world::kAir;
    };
}

struct AABB {
    Vec3 min, max;
    static AABB fromFeet(Vec3 feet, double halfWidth, double height) {
        return {{feet.x - halfWidth, feet.y, feet.z - halfWidth},
                {feet.x + halfWidth, feet.y + height, feet.z + halfWidth}};
    }
};

inline std::int64_t cellOf(double v) { return static_cast<std::int64_t>(std::floor(v)); }

inline bool overlapsSolid(const AABB& b, const SolidQuery& solid) {
    for (std::int64_t y = cellOf(b.min.y + kSkin); y <= cellOf(b.max.y - kSkin); ++y)
        for (std::int64_t z = cellOf(b.min.z + kSkin); z <= cellOf(b.max.z - kSkin); ++z)
            for (std::int64_t x = cellOf(b.min.x + kSkin); x <= cellOf(b.max.x - kSkin); ++x)
                if (solid({x, y, z})) return true;
    return false;
}

// Sweeps one axis at a time against unit voxel cells; returns the allowed displacement.
inline double sweepAxis(AABB& b, int axis, double delta, const SolidQuery& solid) {
    if (delta == 0.0) return 0.0;
    double* lo = axis == 0 ? &b.min.x : axis == 1 ? &b.min.y : &b.min.z;
    double* hi = axis == 0 ? &b.max.x : axis == 1 ? &b.max.y : &b.max.z;
    AABB moved = b;
    double* mlo = axis == 0 ? &moved.min.x : axis == 1 ? &moved.min.y : &moved.min.z;
    double* mhi = axis == 0 ? &moved.max.x : axis == 1 ? &moved.max.y : &moved.max.z;
    *mlo += delta;
    *mhi += delta;
    if (!overlapsSolid(moved, solid)) {
        *lo += delta;
        *hi += delta;
        return delta;
    }
    // Snap flush against the first blocking cell face.
    double allowed;
    if (delta > 0) {
        const double face = std::floor(*hi + delta - kSkin);
        allowed = std::max(0.0, face - *hi - kSkin);
    } else {
        const double face = std::floor(*lo + delta + kSkin) + 1.0;
        allowed = std::min(0.0, face - *lo + kSkin);
    }
    AABB test = b;
    double* tlo = axis == 0 ? &test.min.x : axis == 1 ? &test.min.y : &test.min.z;
    double* thi = axis == 0 ? &test.max.x : axis == 1 ? &test.max.y : &test.max.z;
    *tlo += allowed;
    *thi += allowed;
    if (overlapsSolid(test, solid)) allowed = 0.0;
    *lo += allowed;
    *hi += allowed;
    return allowed;
}

struct ControllerConfig {
    double halfWidth = 0.3;
    double height = 1.8;
    double walkSpeed = 4.5;
    double sprintSpeed = 7.0;
    double jumpSpeed = 8.5;
    double stepHeight = 0.55;   // climbs half-block ledges without jumping
    double armourWeight = 0.0;  // section 12: weight slows locomotion and dodge recovery
};

struct ControllerInput {
    double moveX = 0.0, moveZ = 0.0;  // normalised semantic "move" axis
    bool sprint = false;
    bool jump = false;
};

class CharacterController {
public:
    explicit CharacterController(ControllerConfig cfg = {}, Vec3 feet = {}) : cfg_(cfg), feet_(feet) {}

    void step(const ControllerInput& in, const SolidQuery& solid) {
        double mx = in.moveX, mz = in.moveZ;
        const double len = std::sqrt(mx * mx + mz * mz);
        if (len > 1.0) { mx /= len; mz /= len; }
        const double weightScale = 1.0 / (1.0 + cfg_.armourWeight * 0.02);
        const double speed = (in.sprint ? cfg_.sprintSpeed : cfg_.walkSpeed) * weightScale;

        if (in.jump && grounded_) { vel_.y = cfg_.jumpSpeed; grounded_ = false; }
        vel_.y = std::max(kTerminalFall, vel_.y + kGravity * kTickDt);

        AABB box = AABB::fromFeet(feet_, cfg_.halfWidth, cfg_.height);
        const double dx = mx * speed * kTickDt, dz = mz * speed * kTickDt;
        moveHorizontal(box, 0, dx, solid);
        moveHorizontal(box, 2, dz, solid);

        const double dy = vel_.y * kTickDt;
        const double gotY = sweepAxis(box, 1, dy, solid);
        if (gotY != dy) {
            if (dy < 0) grounded_ = true;
            vel_.y = 0.0;
        } else {
            grounded_ = false;
        }
        if (!grounded_ && vel_.y <= 0.0) {
            AABB probe = box;
            probe.min.y -= 0.02; probe.max.y = box.min.y;
            grounded_ = overlapsSolid(probe, solid);
        }
        feet_ = {(box.min.x + box.max.x) * 0.5, box.min.y, (box.min.z + box.max.z) * 0.5};
        ++ticks_;
    }

    Vec3 feet() const { return feet_; }
    Vec3 velocity() const { return vel_; }
    bool grounded() const { return grounded_; }
    std::uint64_t ticks() const { return ticks_; }
    void teleport(Vec3 feet) { feet_ = feet; vel_ = {}; grounded_ = false; }

    // Hash of the full controller state for deterministic replay gates (section 19).
    std::uint64_t stateHash() const {
        auto bits = [](double d) { std::uint64_t u; std::memcpy(&u, &d, sizeof u); return u; };
        std::uint64_t h = core::hashCombine(bits(feet_.x), bits(feet_.y));
        h = core::hashCombine(h, bits(feet_.z));
        h = core::hashCombine(h, bits(vel_.y));
        return core::hashCombine(h, grounded_ ? 1u : 0u);
    }

private:
    void moveHorizontal(AABB& box, int axis, double d, const SolidQuery& solid) {
        if (d == 0.0) return;
        const double got = sweepAxis(box, axis, d, solid);
        if (got == d || !grounded_) return;
        // Step-up: lift, retry the remaining motion, then settle back down.
        AABB lifted = box;
        if (sweepAxis(lifted, 1, cfg_.stepHeight, solid) < cfg_.stepHeight - 1e-9) return;
        const double rest = d - got;
        if (std::abs(sweepAxis(lifted, axis, rest, solid)) < std::abs(rest) - 1e-9) return;
        sweepAxis(lifted, 1, -cfg_.stepHeight, solid);
        box = lifted;
    }

    ControllerConfig cfg_;
    Vec3 feet_;
    Vec3 vel_{};
    bool grounded_ = false;
    std::uint64_t ticks_ = 0;
};

// --- Destruction (section 7) ------------------------------------------------------------------

struct Material {
    float health = 100.0f;
    float physicalResist = 1.0f;  // multiplier on incoming physical damage
    float ashResist = 1.0f;       // fire/ash damage multiplier (wood burns, stone does not)
    bool destructible = true;
};

struct BlockPosHash {
    std::size_t operator()(BlockPos p) const noexcept {
        return static_cast<std::size_t>(core::hashCombine(
            core::hashCombine(static_cast<std::uint64_t>(p.x), static_cast<std::uint64_t>(p.y)),
            static_cast<std::uint64_t>(p.z)));
    }
};

struct DestructionEvent {
    BlockPos pos;
    BlockId previous = world::kAir;
};

// Tracks per-cell damage. Cells inside a structural anchor never break, so the player cannot
// flatten critical authored spaces. Broken cells are reported so callers can journal them,
// spawn debris/dust and invalidate nav data.
class DestructionSystem {
public:
    void setMaterial(BlockId id, Material m) { materials_[id] = m; }
    void addAnchor(BlockPos min, BlockPos max) { anchors_.push_back({min, max}); }

    bool anchored(BlockPos p) const {
        for (const auto& a : anchors_)
            if (p.x >= a.min.x && p.x <= a.max.x && p.y >= a.min.y && p.y <= a.max.y &&
                p.z >= a.min.z && p.z <= a.max.z)
                return true;
        return false;
    }

    // Applies damage to a cell; returns true when the cell broke this call.
    bool damage(BlockPos p, BlockId id, float physical, float ash = 0.0f) {
        if (id == world::kAir || anchored(p)) return false;
        auto mit = materials_.find(id);
        const Material m = mit == materials_.end() ? Material{} : mit->second;
        if (!m.destructible) return false;
        float& dealt = damage_[p];
        dealt += physical * m.physicalResist + ash * m.ashResist;
        if (dealt < m.health) return false;
        damage_.erase(p);
        broken_.push_back({p, id});
        return true;
    }

    float damageAt(BlockPos p) const {
        auto it = damage_.find(p);
        return it == damage_.end() ? 0.0f : it->second;
    }

    // Returns and clears the cells broken since the last drain.
    std::vector<DestructionEvent> drainBroken() {
        std::vector<DestructionEvent> out;
        out.swap(broken_);
        return out;
    }

private:
    struct Anchor { BlockPos min, max; };
    std::unordered_map<BlockId, Material> materials_;
    std::unordered_map<BlockPos, float, BlockPosHash> damage_;
    std::vector<Anchor> anchors_;
    std::vector<DestructionEvent> broken_;
};

}  // namespace lt::physics
