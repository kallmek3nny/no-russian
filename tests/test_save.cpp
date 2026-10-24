// Save container tests (Sections 14, 19): encode/decode round trip, corruption
// detection, atomic writes with rolling backups, interrupted-write recovery,
// layer/version guards, and schema migration.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "TestHarness.h"
#include "tcore/SaveFile.h"

using namespace lt::core;
namespace fs = std::filesystem;

namespace {

SaveEnvelope make(SaveLayer layer, std::uint32_t version, std::uint8_t fill, std::size_t n) {
    SaveEnvelope e;
    e.layer = layer;
    e.schemaVersion = version;
    e.payload.assign(n, fill);
    return e;
}

// Flips one payload byte on disk, simulating a torn or bit-rotted write.
void corrupt(const fs::path& p) {
    std::fstream f(p, std::ios::binary | std::ios::in | std::ios::out);
    f.seekp(22);
    f.put(static_cast<char>(0x5A));
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "tm_test_save";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    TEST_SECTION("encode/decode round trip");
    {
        const auto e = make(SaveLayer::Character, 3, 0xAB, 64);
        const auto bytes = SaveFile::encode(e);
        const auto d = SaveFile::decode(bytes);
        CHECK(d.has_value());
        if (d) CHECK(*d == e);
        auto bad = bytes;
        bad[25] ^= 0xFF;
        CHECK(!SaveFile::decode(bad).has_value());
        bad = bytes;
        bad.pop_back();
        CHECK(!SaveFile::decode(bad).has_value());
        CHECK(!SaveFile::decode({}).has_value());
    }

    const fs::path path = dir / "profile.tms";

    TEST_SECTION("atomic write with rolling backups");
    for (std::uint8_t v = 1; v <= 3; ++v) {
        CHECK(SaveFile::writeAtomic(path, make(SaveLayer::Profile, 1, v, 32)));
    }
    CHECK(fs::exists(path));
    CHECK(fs::exists(SaveFile::withSuffix(path, ".bak1")));
    CHECK(fs::exists(SaveFile::withSuffix(path, ".bak2")));
    CHECK(!fs::exists(SaveFile::withSuffix(path, ".tmp")));
    auto r = SaveFile::loadWithRecovery(path, SaveLayer::Profile, 1);
    CHECK_EQ(r.source, 0);
    CHECK(r.envelope && r.envelope->payload[0] == 3);

    TEST_SECTION("interrupted write recovers from backup");
    corrupt(path);
    {
        std::ofstream t(SaveFile::withSuffix(path, ".tmp"), std::ios::binary);
        t << "partial";
    }
    r = SaveFile::loadWithRecovery(path, SaveLayer::Profile, 1);
    CHECK_EQ(r.source, 1);
    CHECK(r.envelope && r.envelope->payload[0] == 2);
    corrupt(SaveFile::withSuffix(path, ".bak1"));
    r = SaveFile::loadWithRecovery(path, SaveLayer::Profile, 1);
    CHECK_EQ(r.source, 2);
    CHECK(r.envelope && r.envelope->payload[0] == 1);
    corrupt(SaveFile::withSuffix(path, ".bak2"));
    r = SaveFile::loadWithRecovery(path, SaveLayer::Profile, 1);
    CHECK_EQ(r.source, -1);
    CHECK(!r.envelope.has_value());
    // A fresh write after an interrupted one replaces the stale temp file.
    CHECK(SaveFile::writeAtomic(path, make(SaveLayer::Profile, 1, 9, 32)));
    r = SaveFile::loadWithRecovery(path, SaveLayer::Profile, 1);
    CHECK_EQ(r.source, 0);
    CHECK(r.envelope && r.envelope->payload[0] == 9);

    TEST_SECTION("layer and version guards");
    const fs::path charPath = dir / "character.tms";
    CHECK(SaveFile::writeAtomic(charPath, make(SaveLayer::Character, 5, 4, 16)));
    CHECK(!SaveFile::loadWithRecovery(charPath, SaveLayer::Profile, 9).envelope.has_value());
    CHECK(!SaveFile::loadWithRecovery(charPath, SaveLayer::Character, 4).envelope.has_value());
    CHECK_EQ(SaveFile::loadWithRecovery(charPath, SaveLayer::Character, 5).source, 0);

    TEST_SECTION("schema migration");
    {
        using Bytes = std::vector<std::uint8_t>;
        SaveMigrator m(SaveLayer::Character, 3);
        m.addStep(1, [](Bytes& p) { p.push_back(10); return true; });
        m.addStep(2, [](Bytes& p) { p.push_back(20); return true; });

        const fs::path migPath = dir / "migrate.tms";
        CHECK(SaveFile::writeAtomic(migPath, make(SaveLayer::Character, 1, 7, 2)));
        auto mr = m.load(migPath);
        CHECK(mr.envelope.has_value());
        CHECK(mr.migrated);
        CHECK_EQ(mr.source, 0);
        if (mr.envelope) {
            CHECK_EQ(mr.envelope->schemaVersion, 3u);
            CHECK_EQ(mr.envelope->payload.size(), 4u);
            CHECK(mr.envelope->payload[2] == 10 && mr.envelope->payload[3] == 20);
        }

        // Missing step leaves the envelope untouched.
        SaveMigrator gap(SaveLayer::Character, 3);
        gap.addStep(1, [](Bytes& p) { p.push_back(10); return true; });
        auto e = make(SaveLayer::Character, 1, 7, 2);
        CHECK(!gap.migrate(e));
        CHECK_EQ(e.schemaVersion, 1u);
        CHECK_EQ(e.payload.size(), 2u);

        // Wrong layer and future versions are rejected.
        auto wrong = make(SaveLayer::Profile, 1, 0, 2);
        CHECK(!m.migrate(wrong));
        auto future = make(SaveLayer::Character, 4, 0, 2);
        CHECK(!m.migrate(future));

        // A failing migration on the main file falls back to the backup.
        SaveMigrator brittle(SaveLayer::Character, 3);
        brittle.addStep(1, [](Bytes&) { return false; });
        brittle.addStep(2, [](Bytes& p) { p.push_back(20); return true; });
        const fs::path fbPath = dir / "fallback.tms";
        CHECK(SaveFile::writeAtomic(fbPath, make(SaveLayer::Character, 2, 5, 2)));
        CHECK(SaveFile::writeAtomic(fbPath, make(SaveLayer::Character, 1, 6, 2)));
        auto fr = brittle.load(fbPath);
        CHECK_EQ(fr.source, 1);
        CHECK(fr.migrated);
        CHECK(fr.envelope && fr.envelope->payload[0] == 5 && fr.envelope->payload.back() == 20);

        // Already-current saves load without migration.
        const fs::path curPath = dir / "current.tms";
        CHECK(SaveFile::writeAtomic(curPath, make(SaveLayer::Character, 3, 1, 2)));
        auto cr = m.load(curPath);
        CHECK(cr.envelope.has_value());
        CHECK(!cr.migrated);
    }

    fs::remove_all(dir, ec);
    return lt::test::finish("save");
}
