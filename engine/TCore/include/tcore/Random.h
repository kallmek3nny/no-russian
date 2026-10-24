#pragma once
// TCore deterministic random: SplitMix64 hashing + xoshiro256** streams.
// Identical results on every platform for a given seed (Section 19: Simulation gate).
#include <cstdint>

namespace lt::core {

constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

constexpr std::uint64_t hashCombine(std::uint64_t seed, std::uint64_t value) noexcept {
    return splitmix64(seed ^ splitmix64(value));
}

class Rng {
public:
    explicit constexpr Rng(std::uint64_t seed) noexcept {
        std::uint64_t s = seed;
        for (auto& word : state_) {
            s = splitmix64(s);
            word = s;
        }
    }

    constexpr std::uint64_t next() noexcept {
        const std::uint64_t result = rotl(state_[1] * 5, 7) * 9;
        const std::uint64_t t = state_[1] << 17;
        state_[2] ^= state_[0];
        state_[3] ^= state_[1];
        state_[1] ^= state_[2];
        state_[0] ^= state_[3];
        state_[2] ^= t;
        state_[3] = rotl(state_[3], 45);
        return result;
    }

    // Unbiased value in [0, bound).
    constexpr std::uint64_t below(std::uint64_t bound) noexcept {
        if (bound == 0) return 0;
        const std::uint64_t max = ~std::uint64_t{0};
        const std::uint64_t limit = max - (max % bound);
        std::uint64_t v = next();
        while (v >= limit) v = next();
        return v % bound;
    }

    // Value in [0, 1).
    double unit() noexcept { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

private:
    static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
        return (x << k) | (x >> (64 - k));
    }

    std::uint64_t state_[4]{};
};

} // namespace lt::core
