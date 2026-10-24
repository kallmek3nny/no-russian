#pragma once
// TWorld persistent edit journal (Sections 4.1, 14): append-only block edits,
// compaction, and checksummed atomic writes with two rolling backups.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "tcore/Checksum.h"
#include "tworld/Chunk.h"

namespace lt::world {

inline constexpr std::uint32_t kJournalMagic = 0x314A4D54u; // "TMJ1" little-endian
inline constexpr std::uint32_t kJournalVersion = 1;

struct BlockEdit {
    std::uint64_t seq = 0;
    BlockPos pos;
    BlockId id = kAir;
    friend bool operator==(const BlockEdit&, const BlockEdit&) = default;
};

class EditJournal {
public:
    std::uint64_t record(BlockPos pos, BlockId id) {
        edits_.push_back({nextSeq_, pos, id});
        return nextSeq_++;
    }

    const std::vector<BlockEdit>& edits() const noexcept { return edits_; }
    std::size_t size() const noexcept { return edits_.size(); }
    std::uint64_t nextSeq() const noexcept { return nextSeq_; }

    // Replays this chunk's edits in sequence order; wire to ChunkStreamer::setOnLoad.
    int applyTo(Chunk& chunk) const {
        int applied = 0;
        for (const auto& e : edits_) {
            if (chunkOf(e.pos) == chunk.pos() && chunk.set(localInChunk(e.pos), e.id)) ++applied;
        }
        return applied;
    }

    // Drops superseded edits, keeping the latest edit per block in sequence order.
    std::size_t compact() {
        std::map<Key, std::size_t> latest;
        for (std::size_t i = 0; i < edits_.size(); ++i) latest[key(edits_[i].pos)] = i;
        std::vector<BlockEdit> kept;
        kept.reserve(latest.size());
        for (std::size_t i = 0; i < edits_.size(); ++i) {
            if (latest[key(edits_[i].pos)] == i) kept.push_back(edits_[i]);
        }
        const std::size_t removed = edits_.size() - kept.size();
        edits_ = std::move(kept);
        return removed;
    }

    // Layout (little-endian): magic, version, nextSeq, count, records..., crc32.
    std::vector<std::uint8_t> serialize() const {
        std::vector<std::uint8_t> out;
        out.reserve(kHeaderBytes + edits_.size() * kRecordBytes + 4);
        put(out, kJournalMagic, 4);
        put(out, kJournalVersion, 4);
        put(out, nextSeq_, 8);
        put(out, edits_.size(), 8);
        for (const auto& e : edits_) {
            put(out, e.seq, 8);
            put(out, static_cast<std::uint64_t>(e.pos.x), 8);
            put(out, static_cast<std::uint64_t>(e.pos.y), 8);
            put(out, static_cast<std::uint64_t>(e.pos.z), 8);
            put(out, e.id, 2);
        }
        put(out, core::crc32(out.data(), out.size()), 4);
        return out;
    }

    static std::optional<EditJournal> deserialize(const std::vector<std::uint8_t>& in) {
        if (in.size() < kHeaderBytes + 4) return std::nullopt;
        const std::size_t body = in.size() - 4;
        if (core::crc32(in.data(), body) != get(in, body, 4)) return std::nullopt;
        std::size_t off = 0;
        if (get(in, off, 4) != kJournalMagic) return std::nullopt;
        off += 4;
        if (get(in, off, 4) != kJournalVersion) return std::nullopt;
        off += 4;
        EditJournal j;
        j.nextSeq_ = get(in, off, 8);
        off += 8;
        const std::uint64_t count = get(in, off, 8);
        off += 8;
        if (count > (body - off) / kRecordBytes || off + count * kRecordBytes != body) {
            return std::nullopt;
        }
        j.edits_.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            BlockEdit e;
            e.seq = get(in, off, 8);
            e.pos.x = static_cast<std::int64_t>(get(in, off + 8, 8));
            e.pos.y = static_cast<std::int64_t>(get(in, off + 16, 8));
            e.pos.z = static_cast<std::int64_t>(get(in, off + 24, 8));
            e.id = static_cast<BlockId>(get(in, off + 32, 2));
            off += kRecordBytes;
            j.edits_.push_back(e);
        }
        return j;
    }

    // Atomic write: temp file, flush, rename; previous saves rotate to .bak1 / .bak2.
    bool saveAtomic(const std::filesystem::path& path) const {
        namespace fs = std::filesystem;
        const auto bytes = serialize();
        const fs::path tmp = withSuffix(path, ".tmp");
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            f.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            f.flush();
            if (!f) return false;
        }
        std::error_code ec;
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

    // Loads path, falling back to .bak1 then .bak2. source: 0 main, 1/2 backup, -1 none.
    static std::optional<EditJournal> loadWithRecovery(const std::filesystem::path& path,
                                                       int* source = nullptr) {
        const std::filesystem::path candidates[] = {path, withSuffix(path, ".bak1"),
                                                    withSuffix(path, ".bak2")};
        for (int i = 0; i < 3; ++i) {
            std::ifstream f(candidates[i], std::ios::binary);
            if (!f) continue;
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                            std::istreambuf_iterator<char>());
            if (auto j = deserialize(bytes)) {
                if (source) *source = i;
                return j;
            }
        }
        if (source) *source = -1;
        return std::nullopt;
    }

    static std::filesystem::path withSuffix(const std::filesystem::path& p, const char* suffix) {
        std::filesystem::path r = p;
        r += suffix;
        return r;
    }

private:
    using Key = std::tuple<std::int64_t, std::int64_t, std::int64_t>;
    static constexpr std::size_t kHeaderBytes = 24;
    static constexpr std::size_t kRecordBytes = 8 * 4 + 2;

    static Key key(BlockPos p) { return {p.x, p.y, p.z}; }

    static void put(std::vector<std::uint8_t>& out, std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }

    static std::uint64_t get(const std::vector<std::uint8_t>& in, std::size_t off, int n) {
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(in[off + i]) << (8 * i);
        return v;
    }

    std::vector<BlockEdit> edits_;
    std::uint64_t nextSeq_ = 0;
};

} // namespace lt::world
