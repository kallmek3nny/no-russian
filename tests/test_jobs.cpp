// TCore job system tests (Sections 15, 19, 23): inline execution on zero-worker pools, FIFO drain
// order, counter waits across worker threads, independent counters, exception accounting, nested
// submission from inside jobs, destructor drain, and results that do not depend on worker count.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

#include "TestHarness.h"
#include "tcore/Jobs.h"

using namespace lt::core;

namespace {
std::uint64_t mix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Stand-in for chunk generation: every job writes only its own slot from (seed, index).
std::vector<std::uint64_t> generate(std::size_t workers, std::uint64_t seed, std::size_t count) {
    std::vector<std::uint64_t> out(count, 0);
    JobSystem jobs(workers);
    JobCounter counter;
    for (std::size_t i = 0; i < count; ++i) {
        jobs.submit(
            [&out, seed, i] {
                std::uint64_t h = mix(seed ^ static_cast<std::uint64_t>(i));
                for (int r = 0; r < 64; ++r) h = mix(h);
                out[i] = h;
            },
            &counter);
    }
    jobs.wait(counter);
    return out;
}
}  // namespace

int main() {
    TEST_SECTION("zero-worker pool runs inline");
    {
        JobSystem jobs(0);
        CHECK_EQ(jobs.workerCount(), 0u);
        JobCounter counter;
        CHECK(counter.done());
        std::vector<int> order;
        std::thread::id ranOn;
        for (int i = 0; i < 5; ++i) {
            jobs.submit(
                [&order, &ranOn, i] {
                    order.push_back(i);
                    ranOn = std::this_thread::get_id();
                },
                &counter);
        }
        CHECK_EQ(jobs.queued(), 5u);
        CHECK_EQ(counter.pending(), 5u);
        CHECK(!counter.done());
        CHECK(order.empty());  // nothing runs until someone waits or drains
        jobs.wait(counter);
        CHECK(counter.done());
        CHECK_EQ(jobs.queued(), 0u);
        CHECK_EQ(jobs.completed(), 5u);
        CHECK(ranOn == std::this_thread::get_id());
        CHECK(order == std::vector<int>({0, 1, 2, 3, 4}));
    }

    TEST_SECTION("drain runs queued jobs in FIFO order");
    {
        JobSystem jobs(0);
        std::vector<int> order;
        for (int i = 0; i < 4; ++i) jobs.submit([&order, i] { order.push_back(i * 10); });
        jobs.submit(JobSystem::Job{});  // an empty job completes without running anything
        CHECK_EQ(jobs.drain(), 5u);
        CHECK(order == std::vector<int>({0, 10, 20, 30}));
        CHECK_EQ(jobs.completed(), 5u);
        CHECK_EQ(jobs.failed(), 0u);
        CHECK_EQ(jobs.drain(), 0u);
    }

    TEST_SECTION("worker pool completes every job");
    {
        JobSystem jobs(4);
        CHECK_EQ(jobs.workerCount(), 4u);
        std::atomic<std::uint64_t> sum{0};
        JobCounter counter;
        for (std::uint64_t i = 1; i <= 1000; ++i)
            jobs.submit([&sum, i] { sum.fetch_add(i, std::memory_order_relaxed); }, &counter);
        jobs.wait(counter);
        CHECK(counter.done());
        CHECK_EQ(sum.load(), 500500u);
        CHECK_EQ(jobs.completed(), 1000u);
        CHECK_EQ(jobs.failed(), 0u);
    }

    TEST_SECTION("independent counters");
    {
        JobSystem jobs(2);
        JobCounter streaming;
        JobCounter meshing;
        std::atomic<int> streamed{0};
        std::atomic<int> meshed{0};
        for (int i = 0; i < 50; ++i) {
            jobs.submit([&streamed] { streamed.fetch_add(1); }, &streaming);
            jobs.submit([&meshed] { meshed.fetch_add(1); }, &meshing);
        }
        jobs.wait(streaming);
        CHECK_EQ(streamed.load(), 50);
        jobs.wait(meshing);
        CHECK_EQ(meshed.load(), 50);
        CHECK(streaming.done() && meshing.done());
    }

    TEST_SECTION("throwing jobs are counted and still release counters");
    {
        JobSystem jobs(2);
        JobCounter counter;
        std::atomic<int> ran{0};
        for (int i = 0; i < 10; ++i) {
            jobs.submit(
                [&ran, i] {
                    ran.fetch_add(1);
                    if (i % 3 == 0) throw std::runtime_error("chunk generation failed");
                },
                &counter);
        }
        jobs.wait(counter);
        CHECK(counter.done());
        CHECK_EQ(ran.load(), 10);
        CHECK_EQ(jobs.failed(), 4u);  // i = 0, 3, 6, 9
        CHECK_EQ(jobs.completed(), 10u);
    }

    TEST_SECTION("jobs may submit and wait on follow-up jobs");
    for (const std::size_t workers : {std::size_t{0}, std::size_t{3}}) {
        JobSystem jobs(workers);
        JobCounter outer;
        std::atomic<int> leaves{0};
        for (int region = 0; region < 4; ++region) {
            jobs.submit(
                [&jobs, &leaves] {
                    JobCounter inner;
                    for (int chunk = 0; chunk < 16; ++chunk)
                        jobs.submit([&leaves] { leaves.fetch_add(1); }, &inner);
                    jobs.wait(inner);  // helps run queued jobs instead of parking a worker
                },
                &outer);
        }
        jobs.wait(outer);
        CHECK_EQ(leaves.load(), 64);
        CHECK_EQ(jobs.completed(), 68u);
    }

    TEST_SECTION("destructor finishes queued work");
    {
        std::atomic<int> ran{0};
        {
            JobSystem jobs(0);
            for (int i = 0; i < 8; ++i) jobs.submit([&ran] { ran.fetch_add(1); });
        }
        CHECK_EQ(ran.load(), 8);
        {
            JobSystem jobs(2);
            for (int i = 0; i < 200; ++i) jobs.submit([&ran] { ran.fetch_add(1); });
        }
        CHECK_EQ(ran.load(), 208);
    }

    TEST_SECTION("results do not depend on worker count");
    {
        const std::uint64_t seed = 0x5EED0F7A0BA5Eull;
        const std::vector<std::uint64_t> inline0 = generate(0, seed, 256);
        const std::vector<std::uint64_t> pooled = generate(4, seed, 256);
        const std::vector<std::uint64_t> again = generate(4, seed, 256);
        CHECK(inline0 == pooled);
        CHECK(pooled == again);
        CHECK(generate(2, seed + 1, 256) != pooled);
        bool filled = true;
        for (const std::uint64_t v : pooled) filled = filled && v != 0;
        CHECK(filled);
    }

    return lt::test::finish("jobs");
}
