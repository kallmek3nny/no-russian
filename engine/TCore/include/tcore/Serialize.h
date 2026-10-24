#pragma once
// TCore serialization (Sections 14, 15): little-endian byte writer/reader shared by
// the character, world-seed and co-op ledger save layers. The reader never reads past
// the end of its buffer; malformed input latches a failure flag instead of throwing.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lt::core {

class ByteWriter {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }
    void u16(std::uint16_t v) { put(v, 2); }
    void u32(std::uint32_t v) { put(v, 4); }
    void u64(std::uint64_t v) { put(v, 8); }
    void i32(std::int32_t v) { put(static_cast<std::uint32_t>(v), 4); }
    void i64(std::int64_t v) { put(static_cast<std::uint64_t>(v), 8); }
    void boolean(bool v) { u8(v ? 1u : 0u); }

    // Raw IEEE-754 bit patterns so floats round-trip exactly on every platform.
    void f32(float v) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        u32(bits);
    }
    void f64(double v) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        u64(bits);
    }

    void str(std::string_view s) {
        u32(static_cast<std::uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    void strList(const std::vector<std::string>& v) {
        u32(static_cast<std::uint32_t>(v.size()));
        for (const auto& s : v) str(s);
    }

    void blob(const std::vector<std::uint8_t>& b) {
        u32(static_cast<std::uint32_t>(b.size()));
        buf_.insert(buf_.end(), b.begin(), b.end());
    }

    std::size_t size() const noexcept { return buf_.size(); }
    const std::vector<std::uint8_t>& data() const noexcept { return buf_; }
    std::vector<std::uint8_t> take() noexcept { return std::move(buf_); }

private:
    void put(std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }

    std::vector<std::uint8_t> buf_;
};

class ByteReader {
public:
    static constexpr std::size_t kMaxString = std::size_t{1} << 20;
    static constexpr std::size_t kMaxCount = std::size_t{1} << 20;

    explicit ByteReader(const std::vector<std::uint8_t>& b) noexcept
        : data_(b.data()), size_(b.size()) {}
    ByteReader(const std::uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

    std::uint8_t u8() noexcept { return static_cast<std::uint8_t>(get(1)); }
    std::uint16_t u16() noexcept { return static_cast<std::uint16_t>(get(2)); }
    std::uint32_t u32() noexcept { return static_cast<std::uint32_t>(get(4)); }
    std::uint64_t u64() noexcept { return get(8); }
    std::int32_t i32() noexcept { return static_cast<std::int32_t>(u32()); }
    std::int64_t i64() noexcept { return static_cast<std::int64_t>(u64()); }

    bool boolean() noexcept {
        const std::uint8_t v = u8();
        if (v > 1) fail_ = true;
        return v == 1;
    }

    float f32() noexcept {
        const std::uint32_t bits = u32();
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof v);
        return v;
    }
    double f64() noexcept {
        const std::uint64_t bits = u64();
        double v = 0.0;
        std::memcpy(&v, &bits, sizeof v);
        return v;
    }

    std::string str(std::size_t maxLen = kMaxString) {
        const std::uint32_t n = u32();
        if (fail_ || n > maxLen || n > remaining()) {
            fail_ = true;
            return {};
        }
        if (n == 0) return {};
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }

    // Element count for a container whose elements occupy at least minElemBytes each;
    // rejects counts that cannot possibly fit in the remaining bytes.
    std::uint32_t count(std::size_t minElemBytes = 1, std::size_t maxCount = kMaxCount) noexcept {
        const std::uint32_t n = u32();
        if (fail_ || n > maxCount || (minElemBytes != 0 && n > remaining() / minElemBytes)) {
            fail_ = true;
            return 0;
        }
        return n;
    }

    std::vector<std::string> strList(std::size_t maxCount = kMaxCount) {
        const std::uint32_t n = count(4, maxCount);
        std::vector<std::string> v;
        v.reserve(n);
        for (std::uint32_t i = 0; i < n && !fail_; ++i) v.push_back(str());
        return v;
    }

    std::vector<std::uint8_t> blob(std::size_t maxLen = kMaxString) {
        const std::uint32_t n = u32();
        if (fail_ || n > maxLen || n > remaining()) {
            fail_ = true;
            return {};
        }
        if (n == 0) return {};
        std::vector<std::uint8_t> b(data_ + pos_, data_ + pos_ + n);
        pos_ += n;
        return b;
    }

    void fail() noexcept { fail_ = true; }
    bool ok() const noexcept { return !fail_; }
    std::size_t remaining() const noexcept { return size_ - pos_; }
    bool atEnd() const noexcept { return pos_ == size_; }
    // Everything consumed and no error: the check every decoder should end with.
    bool finished() const noexcept { return !fail_ && pos_ == size_; }

private:
    std::uint64_t get(int n) noexcept {
        if (fail_ || remaining() < static_cast<std::size_t>(n)) {
            fail_ = true;
            return 0;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i)
            v |= static_cast<std::uint64_t>(data_[pos_ + static_cast<std::size_t>(i)]) << (8 * i);
        pos_ += static_cast<std::size_t>(n);
        return v;
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    bool fail_ = false;
};

} // namespace lt::core
