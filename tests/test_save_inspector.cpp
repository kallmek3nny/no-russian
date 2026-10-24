// TTools save inspector tests (design doc sections 14, 15, 19): classification of every
// envelope failure mode, rolling-backup chain inspection that agrees with
// SaveFile::loadWithRecovery, interrupted-write detection, and the settings-menu recovery
// action (restore the newest good backup, quarantine the damaged main file).
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "TestHarness.h"
#include "tcore/SaveFile.h"
#include "ttools/SaveInspector.h"

using namespace lt::tools;
using lt::core::SaveEnvelope;
using lt::core::SaveFile;
using lt::core::SaveLayer;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kMaxSchema = 3;
constexpr SaveLayer kChainLayer = SaveLayer::WorldSeed;

SaveEnvelope makeEnvelope(SaveLayer layer, std::uint32_t version, std::string_view text) {
    SaveEnvelope e;
    e.layer = layer;
    e.schemaVersion = version;
    e.payload.assign(text.begin(), text.end());
    return e;
}

std::vector<std::uint8_t> encodeText(SaveLayer layer, std::uint32_t version, std::string_view text) {
    return SaveFile::encode(makeEnvelope(layer, version, text));
}

std::vector<std::uint8_t> garbage() {
    const std::string s = "this is not a save envelope, only plain text";
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

void flipPayloadBit(std::vector<std::uint8_t>& bytes) {
    bytes[kSaveHeaderBytes] = static_cast<std::uint8_t>(bytes[kSaveHeaderBytes] ^ 0x20u);
}

void writeRaw(const fs::path& p, const std::vector<std::uint8_t>& bytes) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> readRaw(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string payloadText(const fs::path& p) {
    const auto e = SaveFile::decode(readRaw(p));
    return e ? std::string(e->payload.begin(), e->payload.end()) : std::string("<unreadable>");
}

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

// The inspector must predict exactly which file the real loader picks.
void checkAgreesWithLoader(const fs::path& save) {
    const SaveChainReport chain = inspectSaveChain(save, kChainLayer, kMaxSchema);
    const lt::core::SaveLoadResult loaded = SaveFile::loadWithRecovery(save, kChainLayer, kMaxSchema);
    CHECK_EQ(chain.recoverySource, loaded.source);
    CHECK_EQ(chain.recoverySource >= 0, loaded.envelope.has_value());
}

} // namespace

int main() {
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec || base.empty()) base = fs::current_path();
    const fs::path dir = base / "lt_test_save_inspector";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path save = dir / "world_42.tmw";
    const fs::path bak1 = SaveFile::withSuffix(save, ".bak1");
    const fs::path bak2 = SaveFile::withSuffix(save, ".bak2");
    const fs::path tmp = SaveFile::withSuffix(save, ".tmp");
    const fs::path corrupt = SaveFile::withSuffix(save, ".corrupt");

    TEST_SECTION("good envelope");
    {
        const auto bytes = encodeText(SaveLayer::Character, 2, "hp=120;stamina=90");
        const SaveFileReport r = inspectSaveBytes(bytes, SaveLayer::Character, kMaxSchema);
        CHECK(r.usable());
        CHECK_EQ(r.status, SaveStatus::Ok);
        CHECK(r.headerReadable);
        CHECK_EQ(r.magic, lt::core::kSaveMagic);
        CHECK_EQ(r.layer, static_cast<std::uint32_t>(SaveLayer::Character));
        CHECK_EQ(r.schemaVersion, 2u);
        CHECK_EQ(r.fileBytes, static_cast<std::uint64_t>(bytes.size()));
        CHECK_EQ(r.declaredPayloadBytes, 17u);
        CHECK_EQ(r.actualPayloadBytes, 17u);
        CHECK(r.crcComputed);
        CHECK_EQ(r.storedCrc, r.computedCrc);
        // A file of unknown kind (no layer or version expectation) still classifies as ok.
        CHECK(inspectSaveBytes(bytes).usable());
    }

    TEST_SECTION("truncated");
    {
        CHECK_EQ(inspectSaveBytes(std::vector<std::uint8_t>{}).status, SaveStatus::Truncated);
        auto bytes = encodeText(SaveLayer::Profile, 1, "settings");
        bytes.resize(kSaveHeaderBytes + kSaveTrailerBytes - 1);
        const SaveFileReport r = inspectSaveBytes(bytes, SaveLayer::Profile, kMaxSchema);
        CHECK_EQ(r.status, SaveStatus::Truncated);
        CHECK(!r.headerReadable);
        CHECK(!r.usable());
    }

    TEST_SECTION("bad magic");
    {
        auto bytes = encodeText(SaveLayer::Profile, 1, "settings");
        bytes[0] = static_cast<std::uint8_t>(bytes[0] ^ 0xFFu);
        const SaveFileReport r = inspectSaveBytes(bytes, SaveLayer::Profile, kMaxSchema);
        CHECK_EQ(r.status, SaveStatus::BadMagic);
        CHECK(r.headerReadable);
        CHECK(!r.crcComputed);
        CHECK_EQ(inspectSaveBytes(garbage()).status, SaveStatus::BadMagic);
    }

    TEST_SECTION("size mismatch");
    {
        // Declared payload size no longer matches the bytes on disk.
        auto bytes = encodeText(SaveLayer::WorldJournal, 1, "b 10 64 -3 7");
        bytes[12] = static_cast<std::uint8_t>(bytes[12] + 1);
        SaveFileReport r = inspectSaveBytes(bytes, SaveLayer::WorldJournal, kMaxSchema);
        CHECK_EQ(r.status, SaveStatus::SizeMismatch);
        CHECK_EQ(r.declaredPayloadBytes, r.actualPayloadBytes + 1);
        // A write that lost its tail but kept a full header is caught the same way.
        auto cut = encodeText(SaveLayer::WorldJournal, 1, "b 10 64 -3 7 and more journal entries");
        cut.resize(cut.size() - 6);
        r = inspectSaveBytes(cut, SaveLayer::WorldJournal, kMaxSchema);
        CHECK_EQ(r.status, SaveStatus::SizeMismatch);
        CHECK(r.actualPayloadBytes < r.declaredPayloadBytes);
    }

    TEST_SECTION("checksum mismatch");
    {
        auto bytes = encodeText(SaveLayer::Character, 2, "gold=500");
        flipPayloadBit(bytes);
        const SaveFileReport r = inspectSaveBytes(bytes, SaveLayer::Character, kMaxSchema);
        CHECK_EQ(r.status, SaveStatus::ChecksumMismatch);
        CHECK(r.crcComputed);
        CHECK(r.storedCrc != r.computedCrc);
        CHECK(!SaveFile::decode(bytes).has_value());
        // Corrupting only the stored CRC is detected too.
        auto tail = encodeText(SaveLayer::Character, 2, "gold=500");
        tail.back() = static_cast<std::uint8_t>(tail.back() ^ 0x01u);
        CHECK_EQ(inspectSaveBytes(tail, SaveLayer::Character, kMaxSchema).status, SaveStatus::ChecksumMismatch);
        // Inspection is deterministic: the same bytes always produce the same report.
        const SaveFileReport again = inspectSaveBytes(bytes, SaveLayer::Character, kMaxSchema);
        CHECK_EQ(again.status, r.status);
        CHECK_EQ(again.computedCrc, r.computedCrc);
        CHECK_EQ(describe(again), describe(r));
    }

    TEST_SECTION("wrong layer and newer schema");
    {
        const auto profile = encodeText(SaveLayer::Profile, 1, "font_scale=1.25");
        CHECK_EQ(inspectSaveBytes(profile, SaveLayer::Character, kMaxSchema).status, SaveStatus::WrongLayer);
        CHECK_EQ(inspectSaveBytes(profile, SaveLayer::Profile, kMaxSchema).status, SaveStatus::Ok);
        const auto future = encodeText(SaveLayer::Profile, kMaxSchema + 1, "from a newer build");
        const SaveFileReport r = inspectSaveBytes(future, SaveLayer::Profile, kMaxSchema);
        CHECK_EQ(r.status, SaveStatus::NewerSchema);
        CHECK_EQ(r.schemaVersion, kMaxSchema + 1);
        CHECK_EQ(inspectSaveBytes(future, SaveLayer::Profile, kMaxSchema + 1).status, SaveStatus::Ok);
        CHECK_EQ(inspectSaveBytes(future).status, SaveStatus::Ok);
    }

    TEST_SECTION("names and descriptions");
    {
        CHECK(std::string_view(saveStatusName(SaveStatus::ChecksumMismatch)) == "checksum_mismatch");
        CHECK(std::string_view(saveStatusName(SaveStatus::NewerSchema)) == "newer_schema");
        CHECK(std::string_view(saveLayerName(static_cast<std::uint32_t>(SaveLayer::WorldJournal))) == "world_journal");
        CHECK(std::string_view(saveLayerName(99)) == "unknown");
        auto bytes = encodeText(SaveLayer::CoopLedger, 1, "ledger");
        flipPayloadBit(bytes);
        SaveFileReport r = inspectSaveBytes(bytes, SaveLayer::CoopLedger, kMaxSchema);
        r.path = "coop.tmc";
        const std::string text = describe(r);
        CHECK(contains(text, "coop.tmc"));
        CHECK(contains(text, "checksum_mismatch"));
        CHECK(contains(text, "layer=coop_ledger"));
        CHECK(contains(text, "expected=0x"));
    }

    TEST_SECTION("missing files");
    {
        const SaveFileReport r = inspectSaveFile(dir / "nope.tmw", kChainLayer, kMaxSchema);
        CHECK_EQ(r.status, SaveStatus::Missing);
        CHECK(r.path == dir / "nope.tmw");
        const SaveChainReport c = inspectSaveChain(save, kChainLayer, kMaxSchema);
        CHECK(c.unrecoverable());
        CHECK_EQ(c.usableCount(), 0);
        CHECK(!c.interruptedWrite);
        checkAgreesWithLoader(save);
    }

    TEST_SECTION("healthy rolling chain");
    {
        CHECK(SaveFile::writeAtomic(save, makeEnvelope(kChainLayer, 2, "gen1")));
        SaveChainReport c = inspectSaveChain(save, kChainLayer, kMaxSchema);
        CHECK(c.healthy());
        CHECK_EQ(c.usableCount(), 1);
        CHECK_EQ(c.files[1].status, SaveStatus::Missing);
        CHECK_EQ(c.files[2].status, SaveStatus::Missing);
        CHECK(SaveFile::writeAtomic(save, makeEnvelope(kChainLayer, 2, "gen2")));
        CHECK(SaveFile::writeAtomic(save, makeEnvelope(kChainLayer, 3, "gen3")));
        c = inspectSaveChain(save, kChainLayer, kMaxSchema);
        CHECK(c.healthy());
        CHECK(!c.needsRecovery());
        CHECK_EQ(c.usableCount(), 3);
        CHECK(c.primary().path == save);
        CHECK(c.files[1].path == bak1);
        CHECK(c.files[2].path == bak2);
        CHECK_EQ(c.primary().schemaVersion, 3u);
        CHECK_EQ(c.files[1].schemaVersion, 2u);
        CHECK(!c.interruptedWrite);
        CHECK(contains(describe(c), "main file is good"));
        checkAgreesWithLoader(save);
        // Recovery on a healthy chain is a no-op.
        const SaveRecoveryResult res = recoverSave(save, kChainLayer, kMaxSchema);
        CHECK(!res.restored);
        CHECK_EQ(res.source, 0);
        CHECK(res.quarantined.empty());
        CHECK_EQ(payloadText(save), std::string("gen3"));
    }

    TEST_SECTION("damaged main falls back to .bak1");
    {
        auto bytes = readRaw(save);
        flipPayloadBit(bytes);
        writeRaw(save, bytes);
        const SaveChainReport c = inspectSaveChain(save, kChainLayer, kMaxSchema);
        CHECK_EQ(c.primary().status, SaveStatus::ChecksumMismatch);
        CHECK_EQ(c.recoverySource, 1);
        CHECK(c.needsRecovery());
        CHECK_EQ(c.usableCount(), 2);
        CHECK(contains(describe(c), "load .bak1"));
        checkAgreesWithLoader(save);
    }

    TEST_SECTION("two damaged copies fall back to .bak2");
    {
        const auto good1 = readRaw(bak1);
        writeRaw(bak1, garbage());
        const SaveChainReport c = inspectSaveChain(save, kChainLayer, kMaxSchema);
        CHECK_EQ(c.files[1].status, SaveStatus::BadMagic);
        CHECK_EQ(c.recoverySource, 2);
        CHECK(contains(describe(c), "load .bak2"));
        checkAgreesWithLoader(save);
        writeRaw(bak1, good1); // restore for the next sections
    }

    TEST_SECTION("interrupted write is flagged");
    {
        writeRaw(tmp, std::vector<std::uint8_t>(8, 0xAB));
        const SaveChainReport c = inspectSaveChain(save, kChainLayer, kMaxSchema);
        CHECK(c.interruptedWrite);
        CHECK(contains(describe(c), "interrupted write"));
    }

    TEST_SECTION("settings-menu recovery restores the newest good backup");
    {
        const auto damaged = readRaw(save);
        const SaveRecoveryResult res = recoverSave(save, kChainLayer, kMaxSchema);
        CHECK(res.restored);
        CHECK_EQ(res.source, 1);
        CHECK(res.quarantined == corrupt);
        CHECK(fs::exists(corrupt));
        CHECK(readRaw(corrupt) == damaged);
        CHECK_EQ(payloadText(save), std::string("gen2"));
        CHECK_EQ(payloadText(bak1), std::string("gen2"));
        CHECK_EQ(payloadText(bak2), std::string("gen1"));
        CHECK(!fs::exists(tmp));
        const SaveChainReport after = inspectSaveChain(save, kChainLayer, kMaxSchema);
        CHECK(after.healthy());
        CHECK(!after.interruptedWrite);
        CHECK_EQ(after.usableCount(), 3);
        checkAgreesWithLoader(save);
        // The next save rotates normally on top of the restored file.
        CHECK(SaveFile::writeAtomic(save, makeEnvelope(kChainLayer, 3, "gen4")));
        CHECK_EQ(payloadText(save), std::string("gen4"));
        CHECK_EQ(payloadText(bak1), std::string("gen2"));
        CHECK_EQ(payloadText(bak2), std::string("gen2"));
    }

    TEST_SECTION("recovery with the main file missing");
    {
        fs::remove(save, ec);
        fs::remove(corrupt, ec);
        const SaveRecoveryResult res = recoverSave(save, kChainLayer, kMaxSchema);
        CHECK(res.restored);
        CHECK_EQ(res.source, 1);
        CHECK(res.quarantined.empty());
        CHECK(!fs::exists(corrupt));
        CHECK_EQ(payloadText(save), std::string("gen2"));
        checkAgreesWithLoader(save);
    }

    TEST_SECTION("newer-schema main file is skipped and preserved");
    {
        CHECK(SaveFile::writeAtomic(save, makeEnvelope(kChainLayer, kMaxSchema + 2, "future")));
        const SaveChainReport c = inspectSaveChain(save, kChainLayer, kMaxSchema);
        CHECK_EQ(c.primary().status, SaveStatus::NewerSchema);
        CHECK_EQ(c.recoverySource, 1);
        checkAgreesWithLoader(save);
        const SaveRecoveryResult res = recoverSave(save, kChainLayer, kMaxSchema);
        CHECK(res.restored);
        CHECK(res.quarantined == corrupt);
        CHECK_EQ(payloadText(save), std::string("gen2"));
        // The quarantined copy stays intact, so a newer build can still read it.
        CHECK_EQ(payloadText(corrupt), std::string("future"));
        CHECK(inspectSaveFile(corrupt, kChainLayer, kMaxSchema + 2).usable());
    }

    TEST_SECTION("unrecoverable chain is left untouched");
    {
        writeRaw(save, garbage());
        auto truncated = encodeText(kChainLayer, 1, "x");
        truncated.resize(5);
        writeRaw(bak1, truncated);
        writeRaw(bak2, encodeText(SaveLayer::CoopLedger, 1, "ledger"));
        const SaveChainReport c = inspectSaveChain(save, kChainLayer, kMaxSchema);
        CHECK(c.unrecoverable());
        CHECK_EQ(c.usableCount(), 0);
        CHECK_EQ(c.files[0].status, SaveStatus::BadMagic);
        CHECK_EQ(c.files[1].status, SaveStatus::Truncated);
        CHECK_EQ(c.files[2].status, SaveStatus::WrongLayer);
        CHECK(contains(describe(c), "no usable copy"));
        checkAgreesWithLoader(save);
        const SaveRecoveryResult res = recoverSave(save, kChainLayer, kMaxSchema);
        CHECK(!res.restored);
        CHECK_EQ(res.source, -1);
        CHECK(res.quarantined.empty());
        CHECK(readRaw(save) == garbage());
        CHECK(!fs::exists(tmp));
    }

    fs::remove_all(dir, ec);
    return lt::test::finish("save_inspector");
}
