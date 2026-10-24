// Serialization tests (Sections 14, 15): little-endian layout, exact float
// round trips, bounded strings/lists/blobs, and failure latching on truncated
// or malformed input so corrupt saves can never over-read or over-allocate.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "TestHarness.h"
#include "tcore/Serialize.h"

using namespace lt::core;

namespace {

std::uint32_t bitsOf(float v) {
    std::uint32_t b = 0;
    std::memcpy(&b, &v, sizeof b);
    return b;
}

std::uint64_t bitsOf(double v) {
    std::uint64_t b = 0;
    std::memcpy(&b, &v, sizeof b);
    return b;
}

// A representative save record: fixed fields plus every length-prefixed shape.
struct Sample {
    std::uint32_t id = 0;
    std::string name;
    std::vector<std::string> tags;
    std::vector<std::uint8_t> blob;
    double weight = 0.0;
};

std::vector<std::uint8_t> encodeSample(const Sample& s) {
    ByteWriter w;
    w.u32(s.id);
    w.str(s.name);
    w.strList(s.tags);
    w.blob(s.blob);
    w.f64(s.weight);
    return w.take();
}

bool decodeSample(const std::uint8_t* p, std::size_t n, Sample& out) {
    ByteReader r(p, n);
    Sample s;
    s.id = r.u32();
    s.name = r.str();
    s.tags = r.strList();
    s.blob = r.blob();
    s.weight = r.f64();
    if (!r.finished()) return false;
    out = std::move(s);
    return true;
}

} // namespace

int main() {
    TEST_SECTION("little-endian layout");
    {
        ByteWriter w;
        w.u16(0x1234);
        w.u32(0xA1B2C3D4u);
        w.i32(-2);
        const auto& d = w.data();
        CHECK_EQ(w.size(), 10u);
        CHECK(d[0] == 0x34 && d[1] == 0x12);
        CHECK(d[2] == 0xD4 && d[3] == 0xC3 && d[4] == 0xB2 && d[5] == 0xA1);
        CHECK(d[6] == 0xFE && d[7] == 0xFF && d[8] == 0xFF && d[9] == 0xFF);
    }

    TEST_SECTION("scalar round trip");
    {
        const float nanF = std::numeric_limits<float>::quiet_NaN();
        ByteWriter w;
        w.u8(0xEE);
        w.u16(0xBEEF);
        w.u32(0xDEADBEEFu);
        w.u64(0x0123456789ABCDEFull);
        w.i32(std::numeric_limits<std::int32_t>::min());
        w.i64(-1234567890123456789ll);
        w.boolean(true);
        w.boolean(false);
        w.f32(3.25f);
        w.f64(-0.0);
        w.f32(nanF);
        w.f64(std::numeric_limits<double>::infinity());
        const auto bytes = w.take();
        CHECK_EQ(bytes.size(), 1u + 2 + 4 + 8 + 4 + 8 + 1 + 1 + 4 + 8 + 4 + 8);

        ByteReader r(bytes);
        CHECK_EQ(r.u8(), 0xEE);
        CHECK_EQ(r.u16(), 0xBEEF);
        CHECK_EQ(r.u32(), 0xDEADBEEFu);
        CHECK_EQ(r.u64(), 0x0123456789ABCDEFull);
        CHECK_EQ(r.i32(), std::numeric_limits<std::int32_t>::min());
        CHECK_EQ(r.i64(), -1234567890123456789ll);
        CHECK(r.boolean());
        CHECK(!r.boolean());
        CHECK_EQ(r.f32(), 3.25f);
        const double negZero = r.f64();
        CHECK_EQ(bitsOf(negZero), bitsOf(-0.0));
        CHECK(std::signbit(negZero));
        CHECK_EQ(bitsOf(r.f32()), bitsOf(nanF));
        CHECK(std::isinf(r.f64()));
        CHECK(r.finished());
    }

    TEST_SECTION("strings, lists and blobs");
    {
        const std::string cyrillic = "\xD0\xA9\xD0\xB8\xD1\x82"; // UTF-8 Cyrillic (Section 26)
        ByteWriter w;
        w.str("Aetherfang");
        w.str("");
        w.str(cyrillic);
        w.strList({"motion", "memory"});
        w.strList({});
        w.blob({1, 2, 3, 250});
        w.blob({});
        const auto bytes = w.take();

        ByteReader r(bytes);
        CHECK_EQ(r.str(), "Aetherfang");
        CHECK(r.str().empty());
        CHECK_EQ(r.str(), cyrillic);
        const auto tags = r.strList();
        CHECK(tags == std::vector<std::string>({"motion", "memory"}));
        CHECK(r.strList().empty());
        const auto blob = r.blob();
        CHECK(blob == std::vector<std::uint8_t>({1, 2, 3, 250}));
        CHECK(r.blob().empty());
        CHECK(r.finished());
    }

    TEST_SECTION("record round trip and every truncation fails");
    {
        Sample s;
        s.id = 77;
        s.name = "weapon.aetherfang";
        s.tags = {"motion", "memory", "rune"};
        s.blob = {9, 8, 7};
        s.weight = 7.2;
        const auto bytes = encodeSample(s);

        Sample back;
        CHECK(decodeSample(bytes.data(), bytes.size(), back));
        CHECK_EQ(back.id, 77u);
        CHECK_EQ(back.name, s.name);
        CHECK(back.tags == s.tags);
        CHECK(back.blob == s.blob);
        CHECK_EQ(bitsOf(back.weight), bitsOf(7.2));

        int truncatedAccepted = 0;
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            Sample t;
            if (decodeSample(bytes.data(), n, t)) ++truncatedAccepted;
        }
        CHECK_EQ(truncatedAccepted, 0);

        // Trailing garbage is rejected by finished().
        auto longer = bytes;
        longer.push_back(0);
        Sample t;
        CHECK(!decodeSample(longer.data(), longer.size(), t));
    }

    TEST_SECTION("malformed input latches failure");
    {
        // Non-canonical boolean byte.
        const std::vector<std::uint8_t> badBool = {2, 1, 0, 0, 0};
        ByteReader r(badBool);
        r.boolean();
        CHECK(!r.ok());
        // Once failed, later reads return zero even though bytes remain.
        CHECK_EQ(r.u32(), 0u);
        CHECK(!r.finished());
    }
    {
        // String length larger than the remaining bytes.
        ByteWriter w;
        w.u32(64);
        w.u8('x');
        const auto bytes = w.take();
        ByteReader r(bytes);
        CHECK(r.str().empty());
        CHECK(!r.ok());
    }
    {
        // String longer than the caller's limit.
        ByteWriter w;
        w.str("frostbound_knight");
        const auto bytes = w.take();
        ByteReader r(bytes);
        CHECK(r.str(8).empty());
        CHECK(!r.ok());
        ByteReader fine(bytes);
        CHECK_EQ(fine.str(64), "frostbound_knight");
        CHECK(fine.finished());
    }
    {
        // A huge element count is rejected before anything is allocated.
        ByteWriter w;
        w.u32(0xFFFFFFFFu);
        w.u32(0);
        const auto bytes = w.take();
        ByteReader r(bytes);
        CHECK(r.strList().empty());
        CHECK(!r.ok());
        ByteReader c(bytes);
        CHECK_EQ(c.count(4), 0u);
        CHECK(!c.ok());
    }
    {
        // Count that fits the buffer but exceeds the caller's cap, and a count
        // larger than remaining / minElemBytes.
        ByteWriter w;
        w.u32(5);
        for (int i = 0; i < 5; ++i) w.u32(0);
        const auto bytes = w.take();
        ByteReader capped(bytes);
        CHECK_EQ(capped.count(4, 4), 0u);
        CHECK(!capped.ok());
        ByteReader uncapped(bytes);
        CHECK_EQ(uncapped.count(4, 8), 5u);
        CHECK(uncapped.ok());
        ByteReader dense(bytes);
        CHECK_EQ(dense.count(8), 0u);
        CHECK(!dense.ok());
    }
    {
        // Blob length past the end, and an explicit fail() call.
        ByteWriter w;
        w.blob({1, 2, 3});
        auto bytes = w.take();
        bytes.pop_back();
        ByteReader r(bytes);
        CHECK(r.blob().empty());
        CHECK(!r.ok());

        const std::vector<std::uint8_t> one = {1};
        ByteReader f(one);
        f.fail();
        CHECK(!f.ok());
        CHECK(!f.boolean());
    }
    {
        // Empty buffer: finished immediately, any read fails safely.
        const std::vector<std::uint8_t> empty;
        ByteReader r(empty);
        CHECK(r.atEnd());
        CHECK(r.finished());
        CHECK_EQ(r.u64(), 0u);
        CHECK(!r.ok());
    }

    return lt::test::finish("serialize");
}
