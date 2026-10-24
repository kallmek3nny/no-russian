#pragma once
// Legend Engine T - Milestone C: attack animation markers (design doc sections 7, 16 and 19).
// Every attack clip carries wind-up / contact / recovery markers plus guard-counter and deflect windows.
// Marker queries are tick-based and frame-rate independent so hit frames resolve identically at 30 and 60 FPS.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lt::anim {

enum class Phase : std::uint8_t { WindUp, Contact, Recovery, Done };

enum class AttackClass : std::uint8_t { Guardable, Unblockable, Grab };  // drives section 24 audio cues

struct AttackClip {
    std::string id;            // e.g. "greatblade_02.light_1"
    float windUp = 0.0f;       // seconds before the hit window opens
    float contact = 0.0f;      // seconds the hit window stays open
    float recovery = 0.0f;     // seconds of commitment after contact
    float cancelFrom = -1.0f;  // seconds into recovery when dodge-cancel is allowed (<0 = never)
    AttackClass cls = AttackClass::Guardable;
    float total() const { return windUp + contact + recovery; }
};

// Simulation ticks at a fixed 120 Hz; render frame rate only changes how many ticks run per frame.
inline constexpr std::uint32_t kTickRate = 120;

inline std::uint32_t toTicks(float seconds) {
    return static_cast<std::uint32_t>(seconds * static_cast<float>(kTickRate) + 0.5f);
}

struct ClipTicks {
    std::uint32_t contactStart = 0, contactEnd = 0, end = 0, cancel = 0;
    bool cancellable = false;
};

inline ClipTicks bake(const AttackClip& c) {
    ClipTicks t;
    t.contactStart = toTicks(c.windUp);
    t.contactEnd = t.contactStart + toTicks(c.contact);
    t.end = t.contactEnd + toTicks(c.recovery);
    t.cancellable = c.cancelFrom >= 0.0f;
    t.cancel = t.contactEnd + (t.cancellable ? toTicks(c.cancelFrom) : 0);
    return t;
}

inline Phase phaseAt(const ClipTicks& t, std::uint32_t tick) {
    if (tick < t.contactStart) return Phase::WindUp;
    if (tick < t.contactEnd) return Phase::Contact;
    if (tick < t.end) return Phase::Recovery;
    return Phase::Done;
}

// Plays one clip. advance() returns how many contact ticks were crossed this step, so a 30 FPS frame
// that jumps over a short hit window still registers the hit exactly once.
class AttackPlayer {
public:
    void start(const AttackClip& clip) { clip_ = clip; ticks_ = bake(clip); tick_ = 0; hitConsumed_ = false; active_ = true; }

    // Returns true exactly once per clip, on the step where the contact window is first entered or crossed.
    bool advance(std::uint32_t steps) {
        if (!active_) return false;
        const std::uint32_t before = tick_;
        tick_ += steps;
        bool hit = false;
        if (!hitConsumed_ && tick_ > ticks_.contactStart && before < ticks_.contactEnd) {
            hit = true;
            hitConsumed_ = true;
        }
        if (tick_ >= ticks_.end) active_ = false;
        return hit;
    }

    // Accumulates fractional render time into fixed ticks; returns the number of ticks to simulate.
    static std::uint32_t ticksForFrame(float dtSeconds, float& carry) {
        carry += dtSeconds * static_cast<float>(kTickRate);
        const auto n = static_cast<std::uint32_t>(carry);
        carry -= static_cast<float>(n);
        return n;
    }

    Phase phase() const { return active_ ? phaseAt(ticks_, tick_) : Phase::Done; }
    bool active() const { return active_; }
    bool canDodgeCancel() const { return active_ && ticks_.cancellable && tick_ >= ticks_.cancel; }
    std::uint32_t tick() const { return tick_; }
    const AttackClip& clip() const { return clip_; }

private:
    AttackClip clip_{};
    ClipTicks ticks_{};
    std::uint32_t tick_ = 0;
    bool hitConsumed_ = false;
    bool active_ = false;
};

// Defender-side timing: perfect deflection is a short window after guard is raised (section 7).
struct GuardTiming {
    float deflectWindow = 0.15f;  // seconds
    std::optional<std::uint32_t> raisedAt;

    void raise(std::uint32_t tick) { if (!raisedAt) raisedAt = tick; }
    void lower() { raisedAt.reset(); }
    bool guarding() const { return raisedAt.has_value(); }
    bool isPerfect(std::uint32_t hitTick) const {
        return raisedAt && hitTick >= *raisedAt && hitTick - *raisedAt <= toTicks(deflectWindow);
    }
};

// Default light/heavy timings per weapon family; movesets in data/ may override these.
inline std::vector<AttackClip> defaultFamilyClips(const std::string& family) {
    float w = 0.35f, c = 0.10f, r = 0.45f;  // longsword baseline
    if (family == "greatblade" || family == "hammer") { w = 0.60f; c = 0.12f; r = 0.70f; }
    else if (family == "spear") { w = 0.30f; c = 0.08f; r = 0.40f; }
    else if (family == "gauntlet" || family == "sickle") { w = 0.20f; c = 0.06f; r = 0.30f; }
    else if (family == "axe") { w = 0.45f; c = 0.10f; r = 0.55f; }
    return {
        AttackClip{family + ".light", w, c, r, r * 0.5f, AttackClass::Guardable},
        AttackClip{family + ".heavy", w * 1.8f, c * 1.2f, r * 1.4f, -1.0f, AttackClass::Guardable},
        AttackClip{family + ".charged", w * 2.6f, c * 1.5f, r * 1.6f, -1.0f, AttackClass::Unblockable},
    };
}

}  // namespace lt::anim
