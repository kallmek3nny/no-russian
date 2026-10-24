#pragma once
// Legend Engine T - TTools profiler (design doc sections 15, 23).
// Records frame time by simulation, culling, chunk generation, mesh upload, shadows,
// lighting, UI and present. Callers feed measured microseconds per stage, so the
// profiler never reads a clock itself and stays deterministic under test and replay.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lt::tools {

enum class Stage : std::uint8_t {
    Simulation = 0, Culling, ChunkGeneration, MeshUpload, Shadows, Lighting, UI, Present
};
inline constexpr std::size_t kStageCount = 8;

constexpr std::string_view toString(Stage s) noexcept {
    switch (s) {
        case Stage::Simulation: return "simulation";
        case Stage::Culling: return "culling";
        case Stage::ChunkGeneration: return "chunk_generation";
        case Stage::MeshUpload: return "mesh_upload";
        case Stage::Shadows: return "shadows";
        case Stage::Lighting: return "lighting";
        case Stage::UI: return "ui";
        case Stage::Present: return "present";
    }
    return "unknown";
}

struct FrameSample {
    std::array<std::uint32_t, kStageCount> us{};  // microseconds spent per stage
    std::uint32_t total() const noexcept {
        std::uint32_t t = 0;
        for (const auto v : us) t += v;
        return t;
    }
};

// Frame budget for a target rate: 30 FPS -> 33333 us, 60 FPS -> 16666 us.
constexpr std::uint32_t frameBudgetUs(int fps) noexcept {
    return fps > 0 ? static_cast<std::uint32_t>(1000000 / fps) : 0u;
}

// Keeps a fixed-capacity ring of recent frames for percentile queries, plus lifetime
// sums so a 30-minute traversal capture is summarised without storing every frame.
class Profiler {
public:
    explicit Profiler(std::size_t capacity = 600) : ring_(capacity == 0 ? 1 : capacity) {}

    void beginFrame() noexcept { current_ = FrameSample{}; open_ = true; }

    // Time recorded outside an open frame is ignored rather than leaking into the next frame.
    void record(Stage s, std::uint32_t us) noexcept {
        if (open_) current_.us[static_cast<std::size_t>(s)] += us;
    }

    void endFrame() {
        if (!open_) return;
        open_ = false;
        ring_[head_] = current_;
        head_ = (head_ + 1) % ring_.size();
        if (size_ < ring_.size()) ++size_;
        ++frames_;
        for (std::size_t i = 0; i < kStageCount; ++i) stageSum_[i] += current_.us[i];
        const std::uint32_t t = current_.total();
        totalSum_ += t;
        if (t > worst_) worst_ = t;
    }

    std::size_t windowSize() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return ring_.size(); }
    std::uint64_t frames() const noexcept { return frames_; }
    std::uint32_t worstFrameUs() const noexcept { return worst_; }

    // Lifetime averages over the whole capture, in microseconds.
    double averageFrameUs() const noexcept {
        return frames_ ? static_cast<double>(totalSum_) / static_cast<double>(frames_) : 0.0;
    }
    double averageStageUs(Stage s) const noexcept {
        return frames_ ? static_cast<double>(stageSum_[static_cast<std::size_t>(s)]) /
                             static_cast<double>(frames_)
                       : 0.0;
    }

    // i = 0 is the oldest frame still inside the window.
    const FrameSample& recent(std::size_t i) const noexcept {
        const std::size_t start = (head_ + ring_.size() - size_) % ring_.size();
        return ring_[(start + i) % ring_.size()];
    }

private:
    std::vector<FrameSample> ring_;
    std::size_t head_ = 0, size_ = 0;
    FrameSample current_{};
    bool open_ = false;
    std::uint64_t frames_ = 0, totalSum_ = 0;
    std::array<std::uint64_t, kStageCount> stageSum_{};
    std::uint32_t worst_ = 0;
};

}  // namespace lt::tools
