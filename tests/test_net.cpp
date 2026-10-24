// TNet (Sections 13, 14, 15, 19): host-authoritative 2-4 player session, snapshot
// interpolation, region interest management, rollback-safe combat events, co-op ledger.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "TestHarness.h"
#include "tnet/Net.h"

using namespace lt::net;

int main() {
    TEST_SECTION("session");
    Session s(1, "host", 0xC0FFEEull);
    CHECK(!s.isMultiplayer());
    CHECK(s.join(2, "p2"));
    CHECK(!s.join(2, "dup"));
    CHECK(s.join(3, "p3"));
    CHECK(s.join(4, "p4"));
    CHECK(!s.join(5, "p5"));  // 4-player cap
    CHECK_EQ(s.size(), std::size_t(4));
    CHECK(s.isMultiplayer());
    CHECK(!s.leave(1));  // the host cannot leave its own session
    CHECK(s.leave(4));
    CHECK(!s.leave(4));
    CHECK_EQ(s.hostId(), PeerId(1));
    CHECK(s.find(4) == nullptr);

    TEST_SECTION("snapshot interpolation");
    SnapshotBuffer buf(4);
    CHECK(buf.push(Snapshot{10, {EntityState{7, 0.0, 0.0, 0.0, 3.0f}}}));
    CHECK(buf.push(Snapshot{20, {EntityState{7, 10.0, 2.0, -4.0, -3.0f}}}));
    CHECK(!buf.push(Snapshot{20, {}}));  // duplicate tick
    CHECK(!buf.push(Snapshot{15, {}}));  // out of order
    EntityState st;
    CHECK(buf.sample(7, 15.0, st));
    CHECK(std::fabs(st.x - 5.0) < 1e-9);
    CHECK(std::fabs(st.y - 1.0) < 1e-9);
    CHECK(std::fabs(st.z + 2.0) < 1e-9);
    // 3.0 -> -3.0 rad wraps through pi rather than sweeping back through zero.
    CHECK(std::fabs(st.yaw - (3.0f + (2.0f * kPi - 6.0f) * 0.5f)) < 1e-4f);
    CHECK(buf.sample(7, 0.0, st) && st.x == 0.0);     // clamps to the oldest snapshot
    CHECK(buf.sample(7, 500.0, st) && st.x == 10.0);  // never extrapolates
    CHECK(!buf.sample(99, 15.0, st));
    for (std::uint32_t t = 30; t <= 50; t += 10) {
        CHECK(buf.push(Snapshot{t, {}}));
    }
    CHECK_EQ(buf.size(), std::size_t(4));
    CHECK_EQ(buf.newestTick(), std::uint32_t(50));

    TEST_SECTION("region interest");
    CHECK(regionOf(0.0, 0.0) == (RegionKey{0, 0}));
    CHECK(regionOf(-1.0, 511.9) == (RegionKey{-1, 0}));
    CHECK(regionOf(1024.0, -513.0) == (RegionKey{2, -2}));
    // Still exact at the 35,000 km edge of the canonical world.
    CHECK(regionOf(35.0e6, 0.0) == (RegionKey{68359, 0}));
    CHECK(interested(RegionKey{0, 0}, RegionKey{1, -1}));
    CHECK(!interested(RegionKey{0, 0}, RegionKey{2, 0}));
    CHECK(interested(RegionKey{0, 0}, RegionKey{2, 0}, 2));
    const Snapshot world{60, {EntityState{1, 10.0, 0.0, 10.0, 0.0f}, EntityState{2, 2000.0, 0.0, 0.0, 0.0f}}};
    const Snapshot view = filterForPeer(world, regionOf(0.0, 0.0));
    CHECK_EQ(view.tick, std::uint32_t(60));
    CHECK_EQ(view.entities.size(), std::size_t(1));
    CHECK(view.find(1) != nullptr && view.find(2) == nullptr);

    TEST_SECTION("rollback-safe combat events");
    CombatLog combat(24);
    CHECK_EQ(combat.window(), std::uint32_t(24));
    std::uint32_t resim = 0;
    const CombatEvent hit{100, 2, 1, CombatEventKind::Hit, 10, 20, 15.0f};
    const CombatEvent guard{90, 3, 1, CombatEventKind::Guard, 20, 10, 5.0f};
    CHECK(combat.submit(s, hit, 100, resim) == SubmitResult::Accepted);
    CHECK(combat.submit(s, hit, 100, resim) == SubmitResult::Duplicate);  // retransmission
    CHECK(combat.submit(s, guard, 100, resim) == SubmitResult::AcceptedRollback);
    CHECK_EQ(resim, std::uint32_t(90));
    CHECK(combat.submit(s, CombatEvent{76, 3, 2, CombatEventKind::Dodge, 20, 0, 0.0f}, 100, resim) ==
          SubmitResult::AcceptedRollback);  // exactly at the rollback window edge
    CHECK_EQ(resim, std::uint32_t(76));
    CHECK(combat.submit(s, CombatEvent{75, 3, 3, CombatEventKind::Hit, 20, 10, 5.0f}, 100, resim) ==
          SubmitResult::TooLate);
    CHECK(combat.submit(s, CombatEvent{101, 2, 2, CombatEventKind::Hit, 10, 20, 5.0f}, 100, resim) ==
          SubmitResult::FromFuture);
    CHECK(combat.submit(s, CombatEvent{100, 4, 1, CombatEventKind::Hit, 40, 10, 5.0f}, 100, resim) ==
          SubmitResult::UnknownPeer);  // peer 4 left the session
    CHECK(combat.submit(s, CombatEvent{100, 2, 3, CombatEventKind::Hit, 10, 20, -1.0f}, 100, resim) ==
          SubmitResult::Invalid);
    CHECK(combat.submit(s, CombatEvent{100, 2, 4, CombatEventKind::Hit, 10, 20, std::nanf("")}, 100, resim) ==
          SubmitResult::Invalid);
    CHECK(combat.submit(s, CombatEvent{100, 2, 5, static_cast<CombatEventKind>(9), 10, 20, 1.0f}, 100, resim) ==
          SubmitResult::Invalid);
    CHECK_EQ(combat.events().size(), std::size_t(3));
    CHECK_EQ(combat.events()[0].tick, std::uint32_t(76));  // canonical (tick, peer, seq) order
    CHECK_EQ(combat.events()[1].tick, std::uint32_t(90));
    CHECK_EQ(combat.events()[2].tick, std::uint32_t(100));
    CHECK_EQ(combat.from(90).size(), std::size_t(2));
    CHECK(combat.from(101).empty());

    TEST_SECTION("combat log checksum is arrival-order independent");
    CombatLog orderA;
    CombatLog orderB;
    std::uint32_t ra = 0, rb = 0;
    CHECK(orderA.submit(s, hit, 100, ra) == SubmitResult::Accepted);
    CHECK(orderA.submit(s, guard, 100, ra) == SubmitResult::AcceptedRollback);
    CHECK(orderB.submit(s, guard, 95, rb) == SubmitResult::AcceptedRollback);
    CHECK(orderB.submit(s, hit, 100, rb) == SubmitResult::Accepted);
    CHECK(orderA.events() == orderB.events());
    CHECK_EQ(orderA.checksum(), orderB.checksum());
    CHECK(orderA.checksum() != combat.checksum());

    TEST_SECTION("co-op ledger");
    CoopLedger ledger;
    const LedgerEntry rune{LedgerKind::Rune, 2, 0xC0FFEEull, 100, -200, "rune.warning.ambush", {}};
    CHECK(ledger.record(s, rune));
    CHECK(!ledger.record(s, rune));  // same kind, author, key and position
    LedgerEntry moved = rune;
    moved.x = 140;
    CHECK(ledger.record(s, moved));  // the same rune text elsewhere is a new entry
    LedgerEntry foreign = rune;
    foreign.worldSeed = 1;
    CHECK(!ledger.record(s, foreign));
    LedgerEntry stranger = rune;
    stranger.author = 9;
    CHECK(!ledger.record(s, stranger));
    LedgerEntry blank = rune;
    blank.key.clear();
    CHECK(!ledger.record(s, blank));
    LedgerEntry longKey = rune;
    longKey.key.assign(kMaxLedgerKey + 1, 'k');
    CHECK(!ledger.record(s, longKey));
    const LedgerEntry phantom{LedgerKind::Phantom, 3, 0xC0FFEEull, -5, 7, "phantom.death.0001", {1, 2, 3, 4, 5}};
    CHECK(ledger.record(s, phantom));
    LedgerEntry heavy = phantom;
    heavy.key = "phantom.death.0002";
    heavy.data.assign(kMaxLedgerData + 1, 0);
    CHECK(!ledger.record(s, heavy));
    CHECK_EQ(ledger.count(LedgerKind::Rune), std::size_t(2));
    CHECK_EQ(ledger.count(LedgerKind::Phantom), std::size_t(1));
    CHECK_EQ(ledger.count(LedgerKind::Beacon), std::size_t(0));

    TEST_SECTION("co-op ledger save layer");
    const std::vector<std::uint8_t> bytes = ledger.encode();
    CoopLedger copy;
    CHECK(CoopLedger::decode(bytes, copy));
    CHECK(copy.entries() == ledger.entries());
    CHECK(copy.encode() == bytes);
    CoopLedger kept;
    CHECK(kept.record(s, phantom));
    std::vector<std::uint8_t> cut = bytes;
    cut.pop_back();
    CHECK(!CoopLedger::decode(cut, kept));  // truncated
    std::vector<std::uint8_t> extra = bytes;
    extra.push_back(0);
    CHECK(!CoopLedger::decode(extra, kept));  // trailing bytes
    std::vector<std::uint8_t> badKind = bytes;
    badKind[4] = 0xFF;  // the first entry's kind byte follows the u32 count
    CHECK(!CoopLedger::decode(badKind, kept));
    CHECK_EQ(kept.entries().size(), std::size_t(1));  // all-or-nothing
    CHECK(kept.entries().front() == phantom);
    CoopLedger emptyLedger;
    CHECK(CoopLedger::decode(CoopLedger{}.encode(), emptyLedger));
    CHECK(emptyLedger.entries().empty());

    return ::lt::test::finish("net");
}
