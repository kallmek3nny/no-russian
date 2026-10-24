#pragma once
// TWorld chunk storage and streaming ring (Section 4.1).
// Chunks are 32x32xH block columns; the streamer keeps a square ring of chunks
// around a centre chunk loaded and generates them deterministically from the world seed.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tcore/Random.h"
#include "tworld/WorldCoord.h"

namespace lt::world {

using BlockId = std::uint16_t;
inline constexpr BlockId kAir = 0;

// Streaming ring limits from Section 4.1.
inline constexpr int kStreamRadiusMobileMin = 5;
inline constexpr int kStreamRadiusMobileMax = 12;
inline constexpr int kStreamRadiusDesktopMin = 12;
inline constexpr int kStreamRadiusDesktopMax = 32;

class Chunk {
public:
    explicit Chunk(ChunkPos pos, int height = kChunkHeightDesktop)
        : pos_(pos), height_(height),
          blocks_(static_cast<std::size_t>(kChunkSizeX * kChunkSizeZ * height), kAir) {}

    ChunkPos pos() const noexcept { return pos_; }
    int height() const noexcept { return height_; }

    bool inBounds(LocalPos l) const noexcept {
        return l.x >= 0 && l.x < kChunkSizeX && l.z >= 0 && l.z < kChunkSizeZ && l.y >= 0 &&
               l.y < height_;
    }

    BlockId get(LocalPos l) const noexcept { return inBounds(l) ? blocks_[index(l)] : kAir; }

    bool set(LocalPos l, BlockId id) noexcept {
        if (!inBounds(l)) return false;
        blocks_[index(l)] = id;
        dirty_ = true;
        return true;
    }

    bool dirty() const noexcept { return dirty_; }
    void clearDirty() noexcept { dirty_ = false; }

    // Stable content hash used by deterministic replay / reload tests.
    std::uint64_t contentHash() const noexcept {
        std::uint64_t h = core::hashCombine(static_cast<std::uint64_t>(pos_.x),
                                            static_cast<std::uint64_t>(pos_.z));
        for (BlockId b : blocks_) h = core::hashCombine(h, b);
        return h;
    }

private:
    std::size_t index(LocalPos l) const noexcept {
        return (static_cast<std::size_t>(l.y) * kChunkSizeZ + static_cast<std::size_t>(l.z)) *
                   kChunkSizeX +
               static_cast<std::size_t>(l.x);
    }

    ChunkPos pos_;
    int height_;
    std::vector<BlockId> blocks_;
    bool dirty_ = false;
};

struct ChunkPosHash {
    std::size_t operator()(ChunkPos c) const noexcept {
        return static_cast<std::size_t>(core::hashCombine(static_cast<std::uint64_t>(c.x),
                                                          static_cast<std::uint64_t>(c.z)));
    }
};

class ChunkStreamer {
public:
    using Generator = std::function<void(Chunk&, std::uint64_t chunkSeed)>;
    using ChunkCallback = std::function<void(Chunk&)>;

    struct UpdateResult {
        int loaded = 0;
        int unloaded = 0;
    };

    ChunkStreamer(std::uint64_t worldSeed, int radius, Generator gen,
                  int chunkHeight = kChunkHeightDesktop)
        : worldSeed_(worldSeed), radius_(radius < 0 ? 0 : radius), gen_(std::move(gen)),
          chunkHeight_(chunkHeight) {}

    // Called after generation so persistent edits can be replayed onto the chunk.
    void setOnLoad(ChunkCallback cb) { onLoad_ = std::move(cb); }
    // Called before a chunk is dropped so dirty state can be journaled.
    void setOnUnload(ChunkCallback cb) { onUnload_ = std::move(cb); }

    int radius() const noexcept { return radius_; }
    void setRadius(int r) noexcept { radius_ = r < 0 ? 0 : r; }

    bool inRing(ChunkPos c, ChunkPos center) const noexcept {
        const auto dx = c.x - center.x;
        const auto dz = c.z - center.z;
        return dx >= -radius_ && dx <= radius_ && dz >= -radius_ && dz <= radius_;
    }

    UpdateResult update(ChunkPos center) {
        UpdateResult r;
        for (auto it = chunks_.begin(); it != chunks_.end();) {
            if (!inRing(it->first, center)) {
                if (onUnload_) onUnload_(it->second);
                it = chunks_.erase(it);
                ++r.unloaded;
            } else {
                ++it;
            }
        }
        for (std::int64_t dz = -radius_; dz <= radius_; ++dz) {
            for (std::int64_t dx = -radius_; dx <= radius_; ++dx) {
                const ChunkPos c{center.x + dx, center.z + dz};
                if (chunks_.find(c) != chunks_.end()) continue;
                auto [it, inserted] = chunks_.emplace(c, Chunk(c, chunkHeight_));
                if (gen_) gen_(it->second, chunkSeed(worldSeed_, c));
                if (onLoad_) onLoad_(it->second);
                it->second.clearDirty();
                ++r.loaded;
            }
        }
        return r;
    }

    Chunk* find(ChunkPos c) noexcept {
        auto it = chunks_.find(c);
        return it == chunks_.end() ? nullptr : &it->second;
    }
    const Chunk* find(ChunkPos c) const noexcept {
        auto it = chunks_.find(c);
        return it == chunks_.end() ? nullptr : &it->second;
    }

    std::size_t loadedCount() const noexcept { return chunks_.size(); }

private:
    std::uint64_t worldSeed_;
    int radius_;
    Generator gen_;
    int chunkHeight_;
    ChunkCallback onLoad_;
    ChunkCallback onUnload_;
    std::unordered_map<ChunkPos, Chunk, ChunkPosHash> chunks_;
};

} // namespace lt::world
