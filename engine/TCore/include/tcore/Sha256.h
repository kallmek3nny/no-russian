#pragma once
// TCore SHA-256 and HMAC-SHA256 (Sections 19, 25): asset hashes for package inspection and
// keyed signatures on desktop mod permission manifests. Header-only and dependency-free.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lt::core {

using Sha256Digest = std::array<std::uint8_t, 32>;

class Sha256 {
public:
    Sha256() noexcept { reset(); }

    void reset() noexcept {
        h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
              0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        len_ = 0;
        used_ = 0;
    }

    void update(const std::uint8_t* data, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            buf_[used_++] = data[i];
            if (used_ == 64) {
                block(buf_.data());
                used_ = 0;
            }
        }
        len_ += static_cast<std::uint64_t>(n);
    }

    void update(std::string_view s) noexcept {
        update(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    }

    // Appends padding and the 64-bit big-endian bit length, returns the digest and resets.
    Sha256Digest finish() noexcept {
        const std::uint64_t bits = len_ * 8u;
        const std::uint8_t one = 0x80u, zero = 0u;
        update(&one, 1);
        while (used_ != 56) update(&zero, 1);
        std::uint8_t lenBytes[8];
        for (int i = 0; i < 8; ++i) lenBytes[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        update(lenBytes, 8);
        Sha256Digest out{};
        for (int i = 0; i < 8; ++i)
            for (int k = 0; k < 4; ++k)
                out[static_cast<std::size_t>(i * 4 + k)] = static_cast<std::uint8_t>(h_[i] >> (24 - 8 * k));
        reset();
        return out;
    }

private:
    static constexpr std::uint32_t rotr(std::uint32_t x, int n) noexcept { return (x >> n) | (x << (32 - n)); }

    void block(const std::uint8_t* p) noexcept {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t{p[4 * i]} << 24) | (std::uint32_t{p[4 * i + 1]} << 16) |
                   (std::uint32_t{p[4 * i + 2]} << 8) | std::uint32_t{p[4 * i + 3]};
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }

    std::array<std::uint32_t, 8> h_{};
    std::array<std::uint8_t, 64> buf_{};
    std::uint64_t len_ = 0;
    std::size_t used_ = 0;
};

inline Sha256Digest sha256(std::string_view s) noexcept {
    Sha256 h;
    h.update(s);
    return h.finish();
}

// HMAC-SHA256 (RFC 2104). A symmetric keyed signature: the verifier holds the publisher key.
inline Sha256Digest hmacSha256(std::string_view key, std::string_view msg) noexcept {
    std::array<std::uint8_t, 64> k{};
    if (key.size() > 64) {
        const Sha256Digest d = sha256(key);
        std::copy(d.begin(), d.end(), k.begin());
    } else {
        for (std::size_t i = 0; i < key.size(); ++i) k[i] = static_cast<std::uint8_t>(key[i]);
    }
    std::array<std::uint8_t, 64> ipad{}, opad{};
    for (std::size_t i = 0; i < 64; ++i) {
        ipad[i] = static_cast<std::uint8_t>(k[i] ^ 0x36u);
        opad[i] = static_cast<std::uint8_t>(k[i] ^ 0x5cu);
    }
    Sha256 inner;
    inner.update(ipad.data(), ipad.size());
    inner.update(msg);
    const Sha256Digest ih = inner.finish();
    Sha256 outer;
    outer.update(opad.data(), opad.size());
    outer.update(ih.data(), ih.size());
    return outer.finish();
}

inline std::string digestHex(const Sha256Digest& d) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (const std::uint8_t b : d) {
        s += kHex[b >> 4];
        s += kHex[b & 15];
    }
    return s;
}

// Constant-time comparison so signature checks do not leak how many leading bytes matched.
inline bool digestEqual(const Sha256Digest& a, const Sha256Digest& b) noexcept {
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff = static_cast<std::uint8_t>(diff | (a[i] ^ b[i]));
    return diff == 0;
}

} // namespace lt::core
