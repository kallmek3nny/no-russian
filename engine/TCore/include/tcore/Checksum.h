#pragma once
// TCore checksums (Section 14): CRC-32 for save and journal integrity.
#include <array>
#include <cstddef>
#include <cstdint>

namespace lt::core {

namespace detail {
constexpr std::array<std::uint32_t, 256> makeCrcTable() noexcept {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        t[i] = c;
    }
    return t;
}
inline constexpr auto kCrcTable = makeCrcTable();
} // namespace detail

constexpr std::uint32_t crc32(const std::uint8_t* data, std::size_t len,
                              std::uint32_t crc = 0) noexcept {
    crc = ~crc;
    for (std::size_t i = 0; i < len; ++i) crc = detail::kCrcTable[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

} // namespace lt::core
