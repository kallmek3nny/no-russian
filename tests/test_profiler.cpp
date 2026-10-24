// TTools profiler tests (design doc sections 15, 23): per-stage frame recording,
// frame budgets, ring-window wrap, lifetime averages, a 30-minute Android traversal
// capture, and deterministic replay of identical stage timings.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "TestHarness.h"
#include "ttools/Profiler.h"

using namespace lt::tools;

namespace {

// Small deterministic LCG so the replay section does not depend on wall-clock time.
std::uint32_t nextLcg(std::uint32_t& state) noexcept {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

void feedReplay(Profiler& p, std::uint32_t seed, int frames) {
    std::uint32_t s = seed;
    for (int f = 0; f < frames; ++f) {
        p.beginFrame();
        for (std::size_t i = 0; i < kStageCount; ++i)
            p.record(static_cast<Stage>(i), nextLcg(s) % 4000u);
        p.endFrame();
    }
}

}  // namespace

int main() {
    TEST_SECTION("stage names");
    CHECK_EQ(kStageCount, static_cast<std::size_t>(8));
    CHECK_EQ(toString(Stage::Simulation), std::string_view("simulation"));
    CHECK_EQ(toString(Stage::Culling), std::string_view("culling"));
    CHECK_EQ(toString(Stage::ChunkGeneration), std::string_view("chunk_generation"));
    CHECK_EQ(toString(Stage::MeshUpload), std::string_view("mesh_upload"));
    CHECK_EQ(toString(Stage::Shadows), std::string_view("shadows"));
    CHECK_EQ(toString(Stage::Lighting), std::string_view("lighting"));
    CHECK_EQ(toString(Stage::UI), std::string_view("ui"));
    CHECK_EQ(toString(Stage::Present), std::string_view("present"));
    CHECK_EQ(toString(static_cast<Stage>(200)), std::string_view("unknown"));
    {
        bool distinct = true;
        for (std::size_t i = 0; i < kStageCount; ++i)
            for (std::size_t j = i + 1; j < kStageCount; ++j)
                if (toString(static_cast<Stage>(i)) == toString(static_cast<Stage>(j)))
                    distinct = false;
        CHECK(distinct);
    }

    TEST_SECTION("frame budgets");
    CHECK_EQ(frameBudgetUs(30), 33333u);
    CHECK_EQ(frameBudgetUs(60), 16666u);
    CHECK_EQ(frameBudgetUs(120), 8333u);
    CHECK_EQ(frameBudgetUs(0), 0u);
    CHECK_EQ(frameBudgetUs(-5), 0u);

    TEST_SECTION("frame sample total");
    {
        FrameSample s;
        CHECK_EQ(s.total(), 0u);
        s.us[static_cast<std::size_t>(Stage::Simulation)] = 1200;
        s.us[static_cast<std::size_t>(Stage::Present)] = 300;
        CHECK_EQ(s.total(), 1500u);
    }

    TEST_SECTION("empty profiler");
    {
        Profiler p;
        CHECK_EQ(p.capacity(), static_cast<std::size_t>(600));
        CHECK_EQ(p.windowSize(), static_cast<std::size_t>(0));
        CHECK_EQ(p.frames(), 0u);
        CHECK_EQ(p.worstFrameUs(), 0u);
        CHECK_EQ(p.averageFrameUs(), 0.0);
        CHECK_EQ(p.averageStageUs(Stage::UI), 0.0);

        Profiler z(0);
        CHECK_EQ(z.capacity(), static_cast<std::size_t>(1));
    }

    TEST_SECTION("time outside a frame is ignored");
    {
        Profiler p(4);
        p.record(Stage::UI, 500);
        p.endFrame();  // no open frame: must not commit anything
        CHECK_EQ(p.frames(), 0u);
        CHECK_EQ(p.windowSize(), static_cast<std::size_t>(0));
    }

    TEST_SECTION("single frame accumulation");
    {
        Profiler p(4);
        p.beginFrame();
        p.record(Stage::Simulation, 4000);
        p.record(Stage::Culling, 1000);
        p.record(Stage::Simulation, 500);  // same stage accumulates
        p.endFrame();
        CHECK_EQ(p.frames(), 1u);
        CHECK_EQ(p.windowSize(), static_cast<std::size_t>(1));
        CHECK_EQ(p.recent(0).us[static_cast<std::size_t>(Stage::Simulation)], 4500u);
        CHECK_EQ(p.recent(0).us[static_cast<std::size_t>(Stage::Culling)], 1000u);
        CHECK_EQ(p.recent(0).total(), 5500u);
        CHECK_EQ(p.worstFrameUs(), 5500u);
        CHECK_EQ(p.averageFrameUs(), 5500.0);
        CHECK_EQ(p.averageStageUs(Stage::Simulation), 4500.0);
        CHECK_EQ(p.averageStageUs(Stage::Lighting), 0.0);

        p.endFrame();  // double end is a no-op
        CHECK_EQ(p.frames(), 1u);

        p.record(Stage::Present, 999);  // between frames: dropped, not leaked
        p.beginFrame();
        p.endFrame();
        CHECK_EQ(p.frames(), 2u);
        CHECK_EQ(p.recent(1).total(), 0u);
        CHECK_EQ(p.worstFrameUs(), 5500u);
        CHECK_EQ(p.averageFrameUs(), 2750.0);
    }

    TEST_SECTION("ring window wrap");
    {
        Profiler p(3);
        for (std::uint32_t i = 1; i <= 5; ++i) {
            p.beginFrame();
            p.record(Stage::Simulation, i * 1000u);
            p.endFrame();
        }
        CHECK_EQ(p.capacity(), static_cast<std::size_t>(3));
        CHECK_EQ(p.windowSize(), static_cast<std::size_t>(3));
        CHECK_EQ(p.frames(), 5u);
        CHECK_EQ(p.recent(0).total(), 3000u);  // oldest frame still in the window
        CHECK_EQ(p.recent(1).total(), 4000u);
        CHECK_EQ(p.recent(2).total(), 5000u);
        CHECK_EQ(p.worstFrameUs(), 5000u);
        CHECK_EQ(p.averageFrameUs(), 3000.0);  // lifetime, not window: 15000 / 5
        CHECK_EQ(p.averageStageUs(Stage::Simulation), 3000.0);
    }

    TEST_SECTION("30-minute Android traversal capture");
    {
        // 30 FPS for 30 minutes; a chunk-generation spike lands every 30th frame.
        constexpr int kFps = 30;
        constexpr int kFrames = kFps * 60 * 30;
        Profiler p(600);
        for (int f = 0; f < kFrames; ++f) {
            p.beginFrame();
            p.record(Stage::Simulation, 8000);
            p.record(Stage::Culling, 1500);
            p.record(Stage::ChunkGeneration, (f % 30 == 0) ? 6000u : 0u);
            p.record(Stage::MeshUpload, 2000);
            p.record(Stage::Shadows, 3000);
            p.record(Stage::Lighting, 4000);
            p.record(Stage::UI, 1000);
            p.record(Stage::Present, 2000);
            p.endFrame();
        }
        CHECK_EQ(p.frames(), static_cast<std::uint64_t>(kFrames));
        CHECK_EQ(p.windowSize(), static_cast<std::size_t>(600));
        CHECK_EQ(p.worstFrameUs(), 27500u);
        CHECK(p.worstFrameUs() <= frameBudgetUs(kFps));  // holds the Android C31 budget
        CHECK(p.worstFrameUs() > frameBudgetUs(60));     // but would miss the desktop 60 FPS budget
        CHECK_EQ(p.averageStageUs(Stage::ChunkGeneration), 200.0);
        CHECK_EQ(p.averageStageUs(Stage::Simulation), 8000.0);
        CHECK_EQ(p.averageFrameUs(), 21700.0);
        // The newest frame in the window is the last one fed (f = kFrames - 1, no spike).
        CHECK_EQ(p.recent(p.windowSize() - 1).total(), 21500u);
    }

    TEST_SECTION("deterministic replay");
    {
        Profiler a(64), b(64);
        feedReplay(a, 0xC31u, 1000);
        feedReplay(b, 0xC31u, 1000);
        CHECK_EQ(a.frames(), b.frames());
        CHECK_EQ(a.worstFrameUs(), b.worstFrameUs());
        CHECK_EQ(a.averageFrameUs(), b.averageFrameUs());
        bool same = true;
        for (std::size_t i = 0; i < a.windowSize(); ++i)
            for (std::size_t s = 0; s < kStageCount; ++s)
                if (a.recent(i).us[s] != b.recent(i).us[s]) same = false;
        CHECK(same);
        for (std::size_t s = 0; s < kStageCount; ++s)
            CHECK_EQ(a.averageStageUs(static_cast<Stage>(s)), b.averageStageUs(static_cast<Stage>(s)));

        Profiler c(64);
        feedReplay(c, 0xC32u, 1000);
        CHECK(c.averageFrameUs() != a.averageFrameUs());
    }

    return lt::test::finish("profiler");
}
