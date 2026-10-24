#pragma once
// TCore versioned save container (Sections 14, 19): every save layer (profile,
// character, world seed, journal, co-op ledger) is wrapped in a checksummed,
// versioned envelope, written atomically with two rolling backups, and upgraded
// through registered schema migrations on load.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "tcore/Checksum.h"

namespace lt::core {

inline constexpr std::uint32_t kSaveMagic = 0x31534D54u; // "TMS1" little-endian

// Save layers from Section 14.
enum class SaveLayer : std::uint32_t {
    Profile = 1,
    Character = 2,
    WorldSeed = 3,
    WorldJournal = 4,
    CoopLedger = 5,
};

struct SaveEnvelope {
    SaveLayer layer = SaveLayer::Profile;
    std::uint32_t schemaVersion = 1;
    std::vector<std::uint8_t> payload;
    friend bool operator==(const SaveEnvelope&, const SaveEnvelope&) = default;
};

// Where a load came from: 0 main file, 1/2 backups, -1 nothing usable.
struct SaveLoadResult {
    std::optional<SaveEnvelope> envelope;
    int source = -1;
    bool migrated = false;
};

class SaveFile {
public:
    // Layout (little-endian): magic, layer, schemaVersion, payloadSize(u64), payload, crc32.
    static std::vector<std::uint8_t> encode(const SaveEnvelope& e) {
        std::vector<std::uint8_t> out;
        out.reserve(kHeaderBytes + e.payload.size() + 4);
        put(out, kSaveMagic, 4);
        put(out, static_cast<std::uint32_t>(e.layer), 4);
        put(out, e.schemaVersion, 4);
        put(out, e.payload.size(), 8);
        out.insert(out.end(), e.payload.begin(), e.payload.end());
        put(out, crc32(out.data(), out.size()), 4);
        return out;
    }

    static std::optional<SaveEnvelope> decode(const std::vector<std::uint8_t>& in) {
        if (in.size() < kHeaderBytes + 4) return std::nullopt;
        const std::size_t body = in.size() - 4;
        if (crc32(in.data(), body) != get(in, body, 4)) return std::nullopt;
        if (get(in, 0, 4) != kSaveMagic) return std::nullopt;
        SaveEnvelope e;
        e.layer = static_cast<SaveLayer>(get(in, 4, 4));
        e.schemaVersion = static_cast<std::uint32_t>(get(in, 8, 4));
        const std::uint64_t size = get(in, 12, 8);
        if (size != body - kHeaderBytes) return std::nullopt;
        e.payload.assign(in.begin() + kHeaderBytes, in.begin() + static_cast<std::ptrdiff_t>(body));
        return e;
    }

    // Atomic write: temp file, flush, checksum embedded, rename; old saves rotate to .bak1/.bak2.
    static bool writeAtomic(const std::filesystem::path& path, const SaveEnvelope& e) {
        return writeBytesAtomic(path, encode(e));
    }

    static bool writeBytesAtomic(const std::filesystem::path& path,
                                 const std::vector<std::uint8_t>& bytes) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
        const fs::path tmp = withSuffix(path, ".tmp");
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            f.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            f.flush();
            if (!f) return false;
        }
        const fs::path bak1 = withSuffix(path, ".bak1");
        const fs::path bak2 = withSuffix(path, ".bak2");
        if (fs::exists(bak1, ec)) {
            fs::remove(bak2, ec);
            fs::rename(bak1, bak2, ec);
        }
        if (fs::exists(path, ec)) {
            fs::rename(path, bak1, ec);
            if (ec) return false;
        }
        fs::rename(tmp, path, ec);
        return !ec;
    }

    // Loads path, falling back to .bak1 then .bak2 when a file is missing, corrupt,
    // the wrong layer, or newer than this build understands.
    static SaveLoadResult loadWithRecovery(const std::filesystem::path& path, SaveLayer layer,
                                           std::uint32_t maxSupportedVersion) {
        const std::filesystem::path candidates[] = {path, withSuffix(path, ".bak1"),
                                                    withSuffix(path, ".bak2")};
        for (int i = 0; i < 3; ++i) {
            std::ifstream f(candidates[i], std::ios::binary);
            if (!f) continue;
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                            std::istreambuf_iterator<char>());
            auto e = decode(bytes);
            if (!e || e->layer != layer || e->schemaVersion > maxSupportedVersion) continue;
            return {std::move(e), i, false};
        }
        return {};
    }

    static std::filesystem::path withSuffix(const std::filesystem::path& p, const char* suffix) {
        std::filesystem::path r = p;
        r += suffix;
        return r;
    }

private:
    static constexpr std::size_t kHeaderBytes = 20;

    static void put(std::vector<std::uint8_t>& out, std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }

    static std::uint64_t get(const std::vector<std::uint8_t>& in, std::size_t off, int n) {
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(in[off + i]) << (8 * i);
        return v;
    }
};

// Schema migration chain per layer: a step upgrades payload from version v to v + 1.
class SaveMigrator {
public:
    using Step = std::function<bool(std::vector<std::uint8_t>& payload)>;

    SaveMigrator(SaveLayer layer, std::uint32_t currentVersion)
        : layer_(layer), current_(currentVersion) {}

    std::uint32_t currentVersion() const noexcept { return current_; }

    void addStep(std::uint32_t fromVersion, Step step) { steps_[fromVersion] = std::move(step); }

    // Upgrades in place to currentVersion. Fails on a missing step or a failed step,
    // leaving the envelope untouched so callers can fall back to a backup.
    bool migrate(SaveEnvelope& e) const {
        if (e.layer != layer_ || e.schemaVersion > current_) return false;
        SaveEnvelope work = e;
        while (work.schemaVersion < current_) {
            auto it = steps_.find(work.schemaVersion);
            if (it == steps_.end() || !it->second(work.payload)) return false;
            ++work.schemaVersion;
        }
        e = std::move(work);
        return true;
    }

    // Load with backup recovery, then migrate; tries older files if migration fails.
    SaveLoadResult load(const std::filesystem::path& path) const {
        const std::filesystem::path candidates[] = {path, SaveFile::withSuffix(path, ".bak1"),
                                                    SaveFile::withSuffix(path, ".bak2")};
        for (int i = 0; i < 3; ++i) {
            std::ifstream f(candidates[i], std::ios::binary);
            if (!f) continue;
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                            std::istreambuf_iterator<char>());
            auto e = SaveFile::decode(bytes);
            if (!e) continue;
            const std::uint32_t before = e->schemaVersion;
            if (!migrate(*e)) continue;
            return {std::move(e), i, before != current_};
        }
        return {};
    }

private:
    SaveLayer layer_;
    std::uint32_t current_;
    std::map<std::uint32_t, Step> steps_;
};

} // namespace lt::core
