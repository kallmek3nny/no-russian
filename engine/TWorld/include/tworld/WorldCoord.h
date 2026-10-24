#pragma once
// TWorld coordinates (Section 4.1): 64-bit world space, 32x32x128 chunks,
// 16x16-chunk regions, floating origin rebased every 512 m.
#include <cmath>
#include <cstdint>

#include "tcore/Random.h"

namespace lt::world {

inline constexpr std::int64_t kChunkSizeX = 32;
inline constexpr std::int64_t kChunkSizeZ = 32;
inline constexpr int kChunkHeightDesktop = 128;
inline constexpr int kChunkHeightMobile = 96;
inline constexpr std::int64_t kRegionChunks = 16;
inline constexpr double kRebaseDistance = 512.0;

constexpr std::int64_t floorDiv(std::int64_t a, std::int64_t b) noexcept {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

constexpr std::int64_t floorMod(std::int64_t a, std::int64_t b) noexcept {
    return a - floorDiv(a, b) * b;
}

struct BlockPos {
    std::int64_t x = 0, y = 0, z = 0;
    friend constexpr bool operator==(const BlockPos&, const BlockPos&) = default;
};

struct ChunkPos {
    std::int64_t x = 0, z = 0;
    friend constexpr bool operator==(const ChunkPos&, const ChunkPos&) = default;
};

struct RegionPos {
    std::int64_t x = 0, z = 0;
    friend constexpr bool operator==(const RegionPos&, const RegionPos&) = default;
};

struct LocalPos {
    int x = 0, y = 0, z = 0;
    friend constexpr bool operator==(const LocalPos&, const LocalPos&) = default;
};

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

constexpr ChunkPos chunkOf(BlockPos p) noexcept {
    return {floorDiv(p.x, kChunkSizeX), floorDiv(p.z, kChunkSizeZ)};
}

constexpr RegionPos regionOf(ChunkPos c) noexcept {
    return {floorDiv(c.x, kRegionChunks), floorDiv(c.z, kRegionChunks)};
}

constexpr LocalPos localInChunk(BlockPos p) noexcept {
    return {static_cast<int>(floorMod(p.x, kChunkSizeX)), static_cast<int>(p.y),
            static_cast<int>(floorMod(p.z, kChunkSizeZ))};
}

constexpr std::uint64_t chunkSeed(std::uint64_t worldSeed, ChunkPos c) noexcept {
    return core::hashCombine(core::hashCombine(worldSeed, static_cast<std::uint64_t>(c.x)),
                             static_cast<std::uint64_t>(c.z));
}

constexpr std::uint64_t regionSeed(std::uint64_t worldSeed, RegionPos r) noexcept {
    constexpr std::uint64_t kRegionSalt = 0x5245474E494F4E00ull;
    return core::hashCombine(
        core::hashCombine(core::hashCombine(worldSeed, kRegionSalt), static_cast<std::uint64_t>(r.x)),
        static_cast<std::uint64_t>(r.z));
}

// Keeps render/physics coordinates small: positions are doubles relative to a 64-bit origin.
class FloatingOrigin {
public:
    BlockPos origin() const noexcept { return origin_; }

    Vec3 toLocal(BlockPos p) const noexcept {
        return {static_cast<double>(p.x - origin_.x), static_cast<double>(p.y - origin_.y),
                static_cast<double>(p.z - origin_.z)};
    }

    BlockPos toWorldBlock(Vec3 local) const noexcept {
        return {origin_.x + static_cast<std::int64_t>(std::floor(local.x)),
                origin_.y + static_cast<std::int64_t>(std::floor(local.y)),
                origin_.z + static_cast<std::int64_t>(std::floor(local.z))};
    }

    // Rebases when the player drifts past kRebaseDistance; shifts playerLocal to match.
    bool maybeRebase(Vec3& playerLocal) noexcept {
        if (std::abs(playerLocal.x) < kRebaseDistance && std::abs(playerLocal.y) < kRebaseDistance &&
            std::abs(playerLocal.z) < kRebaseDistance) {
            return false;
        }
        const auto dx = static_cast<std::int64_t>(std::floor(playerLocal.x));
        const auto dy = static_cast<std::int64_t>(std::floor(playerLocal.y));
        const auto dz = static_cast<std::int64_t>(std::floor(playerLocal.z));
        origin_.x += dx;
        origin_.y += dy;
        origin_.z += dz;
        playerLocal.x -= static_cast<double>(dx);
        playerLocal.y -= static_cast<double>(dy);
        playerLocal.z -= static_cast<double>(dz);
        ++rebaseCount_;
        return true;
    }

    std::uint64_t rebaseCount() const noexcept { return rebaseCount_; }

private:
    BlockPos origin_{};
    std::uint64_t rebaseCount_ = 0;
};

} // namespace lt::world
