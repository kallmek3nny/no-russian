#pragma once
// TTools save inspector (Sections 14, 15, 19). Opens a save written by core::SaveFile
// without trusting it and explains exactly why a file is unusable: missing, truncated,
// bad magic, payload-size mismatch, checksum mismatch, wrong layer, or a schema newer
// than this build. It inspects the .bak1/.bak2 rolling backups, flags a leftover .tmp
// from an interrupted write, predicts which file SaveFile::loadWithRecovery will pick,
// and implements the settings-menu save recovery action: restore the newest good backup
// over a damaged main file and quarantine the damaged copy as <save>.corrupt.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "tcore/SaveFile.h"

namespace lt::tools {

// Envelope layout written by core::SaveFile (little-endian):
// magic(4) layer(4) schemaVersion(4) payloadSize(8) payload crc32(4).
inline constexpr std::size_t kSaveHeaderBytes = 20;
inline constexpr std::size_t kSaveTrailerBytes = 4;
inline constexpr std::uint32_t kAnySchemaVersion = std::numeric_limits<std::uint32_t>::max();

enum class SaveStatus : std::uint8_t {
    Ok = 0,
    Missing,          // file absent or unreadable
    Truncated,        // shorter than header + checksum
    BadMagic,         // not a save envelope at all
    SizeMismatch,     // declared payload size disagrees with the file length
    ChecksumMismatch, // header is consistent but the CRC does not match the bytes
    WrongLayer,       // valid envelope for a different save layer
    NewerSchema,      // valid envelope written by a newer build
};

inline const char* saveStatusName(SaveStatus s) noexcept {
    switch (s) {
        case SaveStatus::Ok: return "ok";
        case SaveStatus::Missing: return "missing";
        case SaveStatus::Truncated: return "truncated";
        case SaveStatus::BadMagic: return "bad_magic";
        case SaveStatus::SizeMismatch: return "size_mismatch";
        case SaveStatus::ChecksumMismatch: return "checksum_mismatch";
        case SaveStatus::WrongLayer: return "wrong_layer";
        case SaveStatus::NewerSchema: return "newer_schema";
    }
    return "unknown";
}

inline const char* saveLayerName(std::uint32_t layer) noexcept {
    switch (layer) {
        case static_cast<std::uint32_t>(core::SaveLayer::Profile): return "profile";
        case static_cast<std::uint32_t>(core::SaveLayer::Character): return "character";
        case static_cast<std::uint32_t>(core::SaveLayer::WorldSeed): return "world_seed";
        case static_cast<std::uint32_t>(core::SaveLayer::WorldJournal): return "world_journal";
        case static_cast<std::uint32_t>(core::SaveLayer::CoopLedger): return "coop_ledger";
        default: break;
    }
    return "unknown";
}

struct SaveFileReport {
    std::filesystem::path path;
    SaveStatus status = SaveStatus::Missing;
    std::uint64_t fileBytes = 0;
    bool headerReadable = false; // magic/layer/schema/size fields are present
    std::uint32_t magic = 0;
    std::uint32_t layer = 0;     // raw layer id from the header
    std::uint32_t schemaVersion = 0;
    std::uint64_t declaredPayloadBytes = 0;
    std::uint64_t actualPayloadBytes = 0;
    bool crcComputed = false;    // computedCrc is only meaningful once the header is consistent
    std::uint32_t storedCrc = 0;
    std::uint32_t computedCrc = 0;

    bool usable() const noexcept { return status == SaveStatus::Ok; }
};

namespace detail {

inline std::uint64_t readLe(const std::vector<std::uint8_t>& in, std::size_t off, int n) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i)
        v |= static_cast<std::uint64_t>(in[off + static_cast<std::size_t>(i)]) << (8 * i);
    return v;
}

inline std::optional<std::vector<std::uint8_t>> readFileBytes(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) return std::nullopt;
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace detail

// Classifies raw envelope bytes. expectedLayer and maxSupportedVersion mirror the arguments of
// SaveFile::loadWithRecovery; leave them defaulted to inspect a file of unknown kind.
inline SaveFileReport inspectSaveBytes(const std::vector<std::uint8_t>& bytes,
                                       std::optional<core::SaveLayer> expectedLayer = std::nullopt,
                                       std::uint32_t maxSupportedVersion = kAnySchemaVersion) {
    SaveFileReport r;
    r.fileBytes = bytes.size();
    if (bytes.size() < kSaveHeaderBytes + kSaveTrailerBytes) {
        r.status = SaveStatus::Truncated;
        return r;
    }
    const std::size_t body = bytes.size() - kSaveTrailerBytes;
    r.headerReadable = true;
    r.magic = static_cast<std::uint32_t>(detail::readLe(bytes, 0, 4));
    r.layer = static_cast<std::uint32_t>(detail::readLe(bytes, 4, 4));
    r.schemaVersion = static_cast<std::uint32_t>(detail::readLe(bytes, 8, 4));
    r.declaredPayloadBytes = detail::readLe(bytes, 12, 8);
    r.actualPayloadBytes = body - kSaveHeaderBytes;
    r.storedCrc = static_cast<std::uint32_t>(detail::readLe(bytes, body, 4));
    if (r.magic != core::kSaveMagic) {
        r.status = SaveStatus::BadMagic;
        return r;
    }
    if (r.declaredPayloadBytes != r.actualPayloadBytes) {
        r.status = SaveStatus::SizeMismatch;
        return r;
    }
    // The header is self-consistent, so re-encoding the same fields reproduces the exact body
    // bytes and the re-encoded trailer is the CRC this file should carry.
    core::SaveEnvelope probe;
    probe.layer = static_cast<core::SaveLayer>(r.layer);
    probe.schemaVersion = r.schemaVersion;
    probe.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kSaveHeaderBytes),
                         bytes.begin() + static_cast<std::ptrdiff_t>(body));
    const std::vector<std::uint8_t> reencoded = core::SaveFile::encode(probe);
    r.computedCrc =
        static_cast<std::uint32_t>(detail::readLe(reencoded, reencoded.size() - kSaveTrailerBytes, 4));
    r.crcComputed = true;
    if (r.computedCrc != r.storedCrc || !core::SaveFile::decode(bytes)) {
        r.status = SaveStatus::ChecksumMismatch;
        return r;
    }
    if (expectedLayer && r.layer != static_cast<std::uint32_t>(*expectedLayer)) {
        r.status = SaveStatus::WrongLayer;
        return r;
    }
    if (r.schemaVersion > maxSupportedVersion) {
        r.status = SaveStatus::NewerSchema;
        return r;
    }
    r.status = SaveStatus::Ok;
    return r;
}

inline SaveFileReport inspectSaveFile(const std::filesystem::path& path,
                                      std::optional<core::SaveLayer> expectedLayer = std::nullopt,
                                      std::uint32_t maxSupportedVersion = kAnySchemaVersion) {
    SaveFileReport r;
    if (const auto bytes = detail::readFileBytes(path))
        r = inspectSaveBytes(*bytes, expectedLayer, maxSupportedVersion);
    r.path = path;
    return r;
}

// Rotation order used by SaveFile::writeBytesAtomic: 0 main, 1 .bak1, 2 .bak2.
inline std::filesystem::path saveChainPath(const std::filesystem::path& path, int index) {
    if (index == 1) return core::SaveFile::withSuffix(path, ".bak1");
    if (index == 2) return core::SaveFile::withSuffix(path, ".bak2");
    return path;
}

struct SaveChainReport {
    SaveFileReport files[3];       // main, .bak1, .bak2
    int recoverySource = -1;       // file loadWithRecovery returns: 0 main, 1/2 backup, -1 none
    bool interruptedWrite = false; // a leftover .tmp means a write never reached its rename

    const SaveFileReport& primary() const noexcept { return files[0]; }
    bool healthy() const noexcept { return recoverySource == 0; }
    bool needsRecovery() const noexcept { return recoverySource > 0; }
    bool unrecoverable() const noexcept { return recoverySource < 0; }
    int usableCount() const noexcept {
        int n = 0;
        for (const auto& f : files) n += f.usable() ? 1 : 0;
        return n;
    }
};

inline SaveChainReport inspectSaveChain(const std::filesystem::path& path, core::SaveLayer layer,
                                        std::uint32_t maxSupportedVersion) {
    SaveChainReport c;
    for (int i = 0; i < 3; ++i) {
        c.files[i] = inspectSaveFile(saveChainPath(path, i), layer, maxSupportedVersion);
        if (c.recoverySource < 0 && c.files[i].usable()) c.recoverySource = i;
    }
    std::error_code ec;
    c.interruptedWrite = std::filesystem::exists(core::SaveFile::withSuffix(path, ".tmp"), ec);
    return c;
}

struct SaveRecoveryResult {
    bool restored = false;
    int source = -1;                   // chain index used; 0 = main already good, -1 = nothing usable
    std::filesystem::path quarantined; // where the damaged main file went; empty if there was none
};

// Settings-menu save recovery (Section 14). When the main file is unusable and a backup is
// good, the backup bytes are re-verified, written to <save>.tmp and flushed; the damaged main
// file is moved aside to <save>.corrupt for bug reports, then the temp file is renamed into
// place. Backups stay untouched so the player can still fall back further.
inline SaveRecoveryResult recoverSave(const std::filesystem::path& path, core::SaveLayer layer,
                                      std::uint32_t maxSupportedVersion) {
    namespace fs = std::filesystem;
    SaveRecoveryResult res;
    const SaveChainReport chain = inspectSaveChain(path, layer, maxSupportedVersion);
    res.source = chain.recoverySource;
    if (chain.recoverySource <= 0) return res;
    const auto bytes = detail::readFileBytes(saveChainPath(path, chain.recoverySource));
    if (!bytes || !inspectSaveBytes(*bytes, layer, maxSupportedVersion).usable()) return res;

    std::error_code ec;
    const fs::path tmp = core::SaveFile::withSuffix(path, ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return res;
        f.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
        f.flush();
        if (!f) {
            f.close();
            fs::remove(tmp, ec);
            return res;
        }
    }
    if (fs::exists(path, ec)) {
        const fs::path quarantine = core::SaveFile::withSuffix(path, ".corrupt");
        fs::remove(quarantine, ec);
        ec.clear();
        fs::rename(path, quarantine, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return res;
        }
        res.quarantined = quarantine;
    }
    ec.clear();
    fs::rename(tmp, path, ec);
    if (ec) return res;
    res.restored = true;
    return res;
}

// One-line human-readable summary, used by the save inspector CLI and crash reports.
inline std::string describe(const SaveFileReport& r) {
    std::ostringstream os;
    os << r.path.string() << ": " << saveStatusName(r.status) << " (" << r.fileBytes << " bytes)";
    if (r.headerReadable && r.magic == core::kSaveMagic)
        os << " layer=" << saveLayerName(r.layer) << " schema=v" << r.schemaVersion
           << " payload=" << r.actualPayloadBytes << '/' << r.declaredPayloadBytes;
    if (r.headerReadable) {
        os << " crc=0x" << std::hex << r.storedCrc;
        if (r.crcComputed) os << " expected=0x" << r.computedCrc;
        os << std::dec;
    }
    return os.str();
}

inline std::string describe(const SaveChainReport& c) {
    std::ostringstream os;
    for (const auto& f : c.files) os << describe(f) << '\n';
    if (c.interruptedWrite) os << "warning: leftover .tmp from an interrupted write\n";
    os << "recovery: ";
    switch (c.recoverySource) {
        case 0: os << "main file is good"; break;
        case 1: os << "load .bak1"; break;
        case 2: os << "load .bak2"; break;
        default: os << "no usable copy"; break;
    }
    os << '\n';
    return os.str();
}

} // namespace lt::tools
