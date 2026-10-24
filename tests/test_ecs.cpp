// Milestone A ECS tests: handles, component pools, stale-handle safety, save identity.
#include "TestHarness.h"
#include "tentity/Registry.h"

namespace {
struct Health {
    int current = 0;
    int max = 0;
};
struct Posture {
    float value = 0.0f;
};
} // namespace

using namespace lt::ecs;

int main() {
    Registry reg;
    const Entity a = reg.create();
    const Entity b = reg.create();
    const Entity c = reg.create();
    CHECK(reg.aliveCount() == 3);
    CHECK(reg.alive(a) && reg.alive(b) && reg.alive(c));
    CHECK(!reg.alive(kNullEntity));
    CHECK(reg.saveId(a) == 1 && reg.saveId(b) == 2 && reg.saveId(c) == 3);

    CHECK(reg.emplace<Health>(a, 100, 100) != nullptr);
    CHECK(reg.emplace<Health>(b, 50, 80) != nullptr);
    CHECK(reg.emplace<Health>(c, 10, 10) != nullptr);
    CHECK(reg.emplace<Posture>(b, 30.0f) != nullptr);
    CHECK(reg.has<Health>(a) && !reg.has<Posture>(a) && reg.has<Posture>(b));
    CHECK(reg.get<Health>(b) && reg.get<Health>(b)->current == 50);

    int total = 0, visited = 0;
    reg.each<Health>([&](Entity, Health& h) {
        total += h.current;
        ++visited;
    });
    CHECK(visited == 3 && total == 160);

    // Destroy the middle entity: swap-and-pop must keep the others intact.
    CHECK(reg.destroy(a));
    CHECK(!reg.destroy(a));
    CHECK(!reg.alive(a));
    CHECK(reg.get<Health>(a) == nullptr);
    CHECK(reg.get<Health>(c) && reg.get<Health>(c)->current == 10);
    CHECK(reg.get<Health>(b) && reg.get<Health>(b)->max == 80);

    // Slot reuse bumps the generation so stale handles stay dead.
    const Entity d = reg.create();
    CHECK(d.index == a.index && d.generation == a.generation + 1);
    CHECK(!reg.alive(a) && reg.alive(d));
    CHECK(!reg.has<Health>(d));
    CHECK(reg.emplace<Health>(a, 1, 1) == nullptr);
    CHECK(reg.saveId(d) == 4);

    // Component removal.
    reg.remove<Posture>(b);
    CHECK(!reg.has<Posture>(b) && reg.has<Health>(b));

    // Save identity: restored entities keep IDs and new IDs never collide.
    Registry loaded;
    const Entity rb = loaded.createWithSaveId(reg.saveId(b));
    const Entity rd = loaded.createWithSaveId(reg.saveId(d));
    CHECK(loaded.saveId(rb) == 2 && loaded.saveId(rd) == 4);
    CHECK(loaded.createWithSaveId(2) == kNullEntity);
    CHECK(loaded.createWithSaveId(0) == kNullEntity);
    CHECK(loaded.findBySaveId(4) == rd);
    CHECK(loaded.findBySaveId(99) == kNullEntity);
    const Entity fresh = loaded.create();
    CHECK(loaded.saveId(fresh) == 5);
    CHECK(loaded.destroy(rd) && loaded.findBySaveId(4) == kNullEntity);

    // Deterministic replay: identical operation sequences yield identical state.
    auto run = [] {
        Registry r;
        long long sum = 0;
        for (int i = 0; i < 200; ++i) {
            Entity e = r.create();
            r.emplace<Health>(e, i, i * 2);
            if (i % 3 == 0) r.destroy(e);
        }
        r.each<Health>([&](Entity e, Health& h) { sum = sum * 31 + h.current + e.index; });
        return sum;
    };
    CHECK(run() == run());

    return lt::test::finish("ecs");
}
