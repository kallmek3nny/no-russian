#pragma once
// TCore jobs (Section 15: TCore owns memory, jobs, logging, random, serialization; Section 23:
// dedicated streaming and mesh jobs, no blocking disk access on the render thread). A fixed worker
// pool over one FIFO queue with counter-based waits. Waiting threads help run queued jobs, so a
// zero-worker pool runs everything inline on the caller (headless nodes, deterministic replay
// tests in Section 19). A throwing job is counted in failed() and still releases its counter.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace lt::core {

// Tracks outstanding jobs submitted against it; JobSystem::wait() returns once all have run.
class JobCounter {
public:
    std::uint32_t pending() const noexcept { return pending_.load(std::memory_order_acquire); }
    bool done() const noexcept { return pending() == 0; }
private:
    friend class JobSystem;
    std::atomic<std::uint32_t> pending_{0};
};

// Non-copyable and non-movable: workers hold `this` for the pool's whole lifetime.
class JobSystem {
public:
    using Job = std::function<void()>;
    explicit JobSystem(std::size_t workerCount) {
        workers_.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i) workers_.emplace_back([this] { workerLoop(); });
    }
    ~JobSystem() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        wake_.notify_all();
        for (std::thread& t : workers_) t.join();  // workers drain the queue before exiting
        drain();                                   // zero-worker pools finish inline
    }

    void submit(Job job, JobCounter* counter = nullptr) {
        if (counter) counter->pending_.fetch_add(1, std::memory_order_acq_rel);
        { std::lock_guard<std::mutex> lock(mutex_); queue_.push_back(Entry{std::move(job), counter}); }
        wake_.notify_one();
    }

    // Blocks until the counter reaches zero, running queued jobs on this thread meanwhile.
    void wait(const JobCounter& counter) {
        while (!counter.done())
            if (!runOne()) std::this_thread::yield();
    }

    // Runs every queued job on the calling thread; returns how many ran.
    std::size_t drain() { std::size_t n = 0; while (runOne()) ++n; return n; }

    std::size_t workerCount() const noexcept { return workers_.size(); }
    std::size_t queued() const { std::lock_guard<std::mutex> lock(mutex_); return queue_.size(); }
    std::uint64_t completed() const noexcept { return completed_.load(std::memory_order_acquire); }
    std::uint64_t failed() const noexcept { return failed_.load(std::memory_order_acquire); }

private:
    struct Entry { Job job; JobCounter* counter = nullptr; };

    // With `block`, sleeps until work arrives or the pool is stopping; false when nothing popped.
    bool pop(Entry& out, bool block) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (block) wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }
    bool runOne() {
        Entry e;
        if (!pop(e, false)) return false;
        execute(e);
        return true;
    }
    // The job and its captures are released before the counter drops, so waiters never race them.
    void execute(Entry& e) noexcept {
        try { if (e.job) e.job(); } catch (...) { failed_.fetch_add(1, std::memory_order_acq_rel); }
        e.job = nullptr;
        completed_.fetch_add(1, std::memory_order_acq_rel);
        if (e.counter) e.counter->pending_.fetch_sub(1, std::memory_order_acq_rel);
    }
    void workerLoop() {
        Entry e;
        while (pop(e, true)) execute(e);  // false only once stopping with an empty queue
    }

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Entry> queue_;
    std::vector<std::thread> workers_;
    std::atomic<std::uint64_t> completed_{0};
    std::atomic<std::uint64_t> failed_{0};
    bool stopping_ = false;
};

}  // namespace lt::core
