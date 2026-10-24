#pragma once
// Legend Engine T - TNet (design doc sections 13, 14, 15): host-authoritative co-op
// session for 2-4 players, snapshot interpolation, region interest management,
// rollback-safe combat events, matchmaking adapter and the asynchronous co-op ledger.
// The game stays fully playable offline: no single-player save depends on TNet state.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "tcore/Checksum.h"
#include "tcore/Serialize.h"

namespace lt::net {

inline constexpr std::size_t kMinCoopPlayers = 2;
inline constexpr std::size_t kMaxCoopPlayers = 4;

using PeerId = std::uint32_t;

struct Peer {
    PeerId id = 0;
    std::string name;
    bool host = false;
};

// Host-authoritative session: the host owns the world seed and validates every peer event.
class Session {
public:
    Session(PeerId hostId, std::string hostName, std::uint64_t worldSeed) : seed_(worldSeed) {
        peers_.push_back(Peer{hostId, std::move(hostName), true});
    }

    std::uint64_t seed() const noexcept { return seed_; }
    PeerId hostId() const noexcept { return peers_.front().id; }
    std::size_t size() const noexcept { return peers_.size(); }
    const std::vector<Peer>& peers() const noexcept { return peers_; }
    bool isMultiplayer() const noexcept { return peers_.size() >= kMinCoopPlayers; }

    // Rejects duplicate ids and joins beyond the 4-player cap.
    bool join(PeerId id, std::string name) {
        if (peers_.size() >= kMaxCoopPlayers || find(id) != nullptr) return false;
        peers_.push_back(Peer{id, std::move(name), false});
        return true;
    }

    // The host cannot leave its own session; ending the session is a separate operation.
    bool leave(PeerId id) {
        if (id == hostId()) return false;
        const auto it = std::find_if(peers_.begin(), peers_.end(),
                                     [id](const Peer& p) { return p.id == id; });
        if (it == peers_.end()) return false;
        peers_.erase(it);
        return true;
    }

    const Peer* find(PeerId id) const noexcept {
        for (const auto& p : peers_)
            if (p.id == id) return &p;
        return nullptr;
    }

private:
    std::uint64_t seed_;
    std::vector<Peer> peers_;
};

// Snapshot interpolation (Section 13): the host broadcasts authoritative entity snapshots on
// the fixed simulation tick; clients render between the two snapshots that bracket the
// delayed render tick. Positions are world metres in double precision so 35,000 km stays exact.
struct EntityState {
    std::uint64_t entity = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    float yaw = 0.0f;  // radians
};

struct Snapshot {
    std::uint32_t tick = 0;
    std::vector<EntityState> entities;

    const EntityState* find(std::uint64_t entity) const noexcept {
        for (const auto& e : entities)
            if (e.entity == entity) return &e;
        return nullptr;
    }
};

inline constexpr float kPi = 3.14159265358979f;

class SnapshotBuffer {
public:
    explicit SnapshotBuffer(std::size_t capacity = 32) : capacity_(capacity < 2 ? 2 : capacity) {}

    // Stale, duplicate or out-of-order snapshots are dropped; only the newest `capacity` are kept.
    bool push(Snapshot s) {
        if (!snaps_.empty() && s.tick <= snaps_.back().tick) return false;
        snaps_.push_back(std::move(s));
        if (snaps_.size() > capacity_) snaps_.erase(snaps_.begin());
        return true;
    }

    std::size_t size() const noexcept { return snaps_.size(); }
    std::uint32_t newestTick() const noexcept { return snaps_.empty() ? 0u : snaps_.back().tick; }

    // Interpolated state at a fractional render tick. Clamps to the oldest/newest snapshot rather
    // than extrapolating; an entity missing from one side holds its known state.
    bool sample(std::uint64_t entity, double renderTick, EntityState& out) const {
        if (snaps_.empty()) return false;
        const Snapshot* a = &snaps_.front();
        const Snapshot* b = a;
        if (renderTick >= static_cast<double>(snaps_.back().tick)) {
            a = b = &snaps_.back();
        } else if (renderTick > static_cast<double>(snaps_.front().tick)) {
            for (std::size_t i = 1; i < snaps_.size(); ++i) {
                if (static_cast<double>(snaps_[i].tick) >= renderTick) {
                    a = &snaps_[i - 1];
                    b = &snaps_[i];
                    break;
                }
            }
        }
        const EntityState* ea = a->find(entity);
        const EntityState* eb = b->find(entity);
        if (!ea && !eb) return false;
        if (!ea) ea = eb;
        if (!eb) eb = ea;
        const double span = static_cast<double>(b->tick - a->tick);
        const double t = span > 0.0 ? (renderTick - static_cast<double>(a->tick)) / span : 0.0;
        out.entity = entity;
        out.x = ea->x + (eb->x - ea->x) * t;
        out.y = ea->y + (eb->y - ea->y) * t;
        out.z = ea->z + (eb->z - ea->z) * t;
        float d = eb->yaw - ea->yaw;  // shortest arc
        for (int k = 0; k < 8 && d > kPi; ++k) d -= 2.0f * kPi;
        for (int k = 0; k < 8 && d < -kPi; ++k) d += 2.0f * kPi;
        out.yaw = ea->yaw + d * static_cast<float>(t);
        return true;
    }

private:
    std::size_t capacity_;
    std::vector<Snapshot> snaps_;
};

// Region interest management (Sections 4.1, 13): a region is 16 x 16 chunks of 32 m = 512 m.
// Peers only receive entities whose region lies within `radius` regions of their own.
inline constexpr std::int64_t kRegionMetres = 512;

struct RegionKey {
    std::int64_t rx = 0, rz = 0;
    friend bool operator==(const RegionKey&, const RegionKey&) = default;
};

inline std::int64_t floorToInt(double v) noexcept {
    auto i = static_cast<std::int64_t>(v);
    if (static_cast<double>(i) > v) --i;
    return i;
}

inline std::int64_t floorDiv(std::int64_t a, std::int64_t b) noexcept {
    std::int64_t q = a / b;
    if (a % b != 0 && ((a < 0) != (b < 0))) --q;
    return q;
}

inline RegionKey regionOf(double worldX, double worldZ) noexcept {
    return RegionKey{floorDiv(floorToInt(worldX), kRegionMetres), floorDiv(floorToInt(worldZ), kRegionMetres)};
}

inline bool interested(const RegionKey& viewer, const RegionKey& subject, std::int64_t radius = 1) noexcept {
    const std::int64_t dx = viewer.rx > subject.rx ? viewer.rx - subject.rx : subject.rx - viewer.rx;
    const std::int64_t dz = viewer.rz > subject.rz ? viewer.rz - subject.rz : subject.rz - viewer.rz;
    return dx <= radius && dz <= radius;
}

// Host-side filter: the per-peer view of an authoritative snapshot.
inline Snapshot filterForPeer(const Snapshot& s, const RegionKey& viewer, std::int64_t radius = 1) {
    Snapshot out;
    out.tick = s.tick;
    for (const auto& e : s.entities)
        if (interested(viewer, regionOf(e.x, e.z), radius)) out.entities.push_back(e);
    return out;
}

// Rollback-safe combat events (Sections 13, 19): peers propose combat outcomes stamped with
// the simulation tick they observed. The host validates them, keeps a canonical order of
// (tick, peer, seq) and accepts late events inside a bounded rollback window; a late accept
// reports the tick to resimulate from so host replays remain deterministic.
enum class CombatEventKind : std::uint8_t { Hit = 0, Guard, Deflect, Dodge, PostureBreak, Death };

struct CombatEvent {
    std::uint32_t tick = 0;
    PeerId peer = 0;
    std::uint32_t seq = 0;  // per-peer sequence, used to drop retransmissions
    CombatEventKind kind = CombatEventKind::Hit;
    std::uint64_t source = 0, target = 0;
    float amount = 0.0f;
    friend bool operator==(const CombatEvent&, const CombatEvent&) = default;
};

inline bool combatBefore(const CombatEvent& a, const CombatEvent& b) noexcept {
    if (a.tick != b.tick) return a.tick < b.tick;
    if (a.peer != b.peer) return a.peer < b.peer;
    return a.seq < b.seq;
}

enum class SubmitResult : std::uint8_t {
    Accepted = 0, AcceptedRollback, Duplicate, TooLate, FromFuture, UnknownPeer, Invalid
};

inline constexpr float kMaxCombatAmount = 1.0e6f;

class CombatLog {
public:
    explicit CombatLog(std::uint32_t rollbackWindowTicks = 24) : window_(rollbackWindowTicks) {}

    std::uint32_t window() const noexcept { return window_; }
    const std::vector<CombatEvent>& events() const noexcept { return events_; }

    // hostTick is the host's current simulation tick. resimFrom is set only on AcceptedRollback.
    SubmitResult submit(const Session& session, const CombatEvent& e, std::uint32_t hostTick,
                        std::uint32_t& resimFrom) {
        if (session.find(e.peer) == nullptr) return SubmitResult::UnknownPeer;
        // NaN, infinity and negative amounts all fail this range check.
        if (!(e.amount >= 0.0f && e.amount <= kMaxCombatAmount) ||
            static_cast<std::uint8_t>(e.kind) > static_cast<std::uint8_t>(CombatEventKind::Death))
            return SubmitResult::Invalid;
        if (e.tick > hostTick) return SubmitResult::FromFuture;
        if (hostTick - e.tick > window_) return SubmitResult::TooLate;
        for (const auto& x : events_)
            if (x.peer == e.peer && x.seq == e.seq) return SubmitResult::Duplicate;
        const auto pos = std::upper_bound(events_.begin(), events_.end(), e, combatBefore);
        const bool late = e.tick < hostTick;
        events_.insert(pos, e);
        if (late) {
            resimFrom = e.tick;
            return SubmitResult::AcceptedRollback;
        }
        return SubmitResult::Accepted;
    }

    // Events at or after a tick, in canonical order: the input to a resimulation.
    std::vector<CombatEvent> from(std::uint32_t tick) const {
        std::vector<CombatEvent> out;
        for (const auto& e : events_)
            if (e.tick >= tick) out.push_back(e);
        return out;
    }

    // Digest of the canonical log: hosts that accepted the same events agree regardless of arrival order.
    std::uint32_t checksum() const {
        lt::core::ByteWriter w;
        for (const auto& e : events_) {
            w.u32(e.tick);
            w.u32(e.peer);
            w.u32(e.seq);
            w.u8(static_cast<std::uint8_t>(e.kind));
            w.u64(e.source);
            w.u64(e.target);
            w.f32(e.amount);
        }
        return lt::core::crc32(w.data().data(), w.size());
    }

private:
    std::uint32_t window_;
    std::vector<CombatEvent> events_;
};

// Asynchronous co-op ledger (Sections 13, 14): validated shared discoveries, aspect fragments,
// world runes, beacon echoes and phantom records (death/route replays). The host validates every
// entry before it is recorded; the ledger is its own save layer and never touches the single-player save.
enum class LedgerKind : std::uint8_t { Discovery = 0, AspectFragment, Rune, Beacon, Phantom };

struct LedgerEntry {
    LedgerKind kind = LedgerKind::Discovery;
    PeerId author = 0;
    std::uint64_t worldSeed = 0;
    std::int64_t x = 0, z = 0;       // world metres
    std::string key;                 // discovery id, aspect id, rune loc key, beacon id
    std::vector<std::uint8_t> data;  // phantom route / behaviour samples
    friend bool operator==(const LedgerEntry&, const LedgerEntry&) = default;
};

inline constexpr std::uint32_t kLedgerSchemaVersion = 1;
inline constexpr std::size_t kMaxLedgerKey = 128;
inline constexpr std::size_t kMaxLedgerData = 64 * 1024;
inline constexpr std::size_t kMaxLedgerEntries = 100000;

class CoopLedger {
public:
    // Rejects unknown authors, foreign seeds, empty/oversized keys, oversized data and duplicates
    // of the same (kind, author, key, position).
    bool record(const Session& session, LedgerEntry e) {
        if (session.find(e.author) == nullptr || e.worldSeed != session.seed()) return false;
        if (!validShape(e)) return false;
        for (const auto& x : entries_)
            if (x.kind == e.kind && x.author == e.author && x.key == e.key && x.x == e.x && x.z == e.z)
                return false;
        entries_.push_back(std::move(e));
        return true;
    }

    const std::vector<LedgerEntry>& entries() const noexcept { return entries_; }

    std::size_t count(LedgerKind k) const noexcept {
        std::size_t n = 0;
        for (const auto& e : entries_)
            if (e.kind == k) ++n;
        return n;
    }

    std::vector<std::uint8_t> encode() const {
        lt::core::ByteWriter w;
        w.u32(static_cast<std::uint32_t>(entries_.size()));
        for (const auto& e : entries_) {
            w.u8(static_cast<std::uint8_t>(e.kind));
            w.u32(e.author);
            w.u64(e.worldSeed);
            w.i64(e.x);
            w.i64(e.z);
            w.str(e.key);
            w.blob(e.data);
        }
        return w.take();
    }

    // All-or-nothing: on any malformed byte the output ledger is left untouched.
    static bool decode(const std::vector<std::uint8_t>& payload, CoopLedger& out) {
        lt::core::ByteReader r(payload);
        const std::uint32_t n = r.count(kMinEntryBytes, kMaxLedgerEntries);
        std::vector<LedgerEntry> v;
        v.reserve(n);
        for (std::uint32_t i = 0; i < n && r.ok(); ++i) {
            LedgerEntry e;
            e.kind = static_cast<LedgerKind>(r.u8());
            e.author = r.u32();
            e.worldSeed = r.u64();
            e.x = r.i64();
            e.z = r.i64();
            e.key = r.str(kMaxLedgerKey);
            e.data = r.blob(kMaxLedgerData);
            if (!r.ok() || !validShape(e)) return false;
            v.push_back(std::move(e));
        }
        if (!r.finished()) return false;
        out.entries_ = std::move(v);
        return true;
    }

private:
    // kind u8 + author u32 + seed u64 + x, z i64 + key length u32 + data length u32
    static constexpr std::size_t kMinEntryBytes = 1 + 4 + 8 + 8 + 8 + 4 + 4;

    static bool validShape(const LedgerEntry& e) noexcept {
        return static_cast<std::uint8_t>(e.kind) <= static_cast<std::uint8_t>(LedgerKind::Phantom) &&
               !e.key.empty() && e.key.size() <= kMaxLedgerKey && e.data.size() <= kMaxLedgerData;
    }

    std::vector<LedgerEntry> entries_;
};

}  // namespace lt::net
