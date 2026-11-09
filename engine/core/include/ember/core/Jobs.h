#pragma once

// Job system: small units of work spread over one worker thread per CPU core.
//
// - Each thread has its own queue. It pushes and pops at the back (LIFO: the work it just created is
//   still in cache); idle threads steal from the front of other queues (FIFO: the oldest, usually
//   biggest, pieces of work). That's "work stealing".
// - A JobCounter counts unfinished jobs. wait(counter) doesn't block: the waiting thread runs other
//   jobs until the counter reaches zero, so no core sits idle and nested waits can't deadlock.
// - Jobs are stored inline (no heap allocation per job): captures must fit in kJobStorage bytes and be
//   trivially copyable (values, pointers, references).
//
//   JobCounter counter;
//   jobs.parallelFor(items.size(), 64, [&](u32 begin, u32 end) { ... }, &counter);
//   jobs.wait(counter);

#include "ember/core/Memory.h"
#include "ember/core/Types.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace ember {

struct JobCounter {
    std::atomic<u32> pending{0};
    bool done() const { return pending.load(std::memory_order_acquire) == 0; }
};

inline constexpr usize kJobStorage = 48;

struct Job {
    void (*invoke)(Job&) = nullptr; // runs the stored callable
    JobCounter* counter = nullptr;
    alignas(std::max_align_t) std::byte storage[kJobStorage];
};

class JobSystem {
public:
    // workerCount 0 = one per hardware thread, minus the calling (main) thread.
    explicit JobSystem(u32 workerCount = 0);
    ~JobSystem(); // finishes queued jobs, then joins the workers

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    template <typename F>
    void run(F&& fn, JobCounter* counter = nullptr)
    {
        using Fn = std::decay_t<F>;
        static_assert(sizeof(Fn) <= kJobStorage, "job captures too big: capture a pointer to the data instead");
        static_assert(alignof(Fn) <= alignof(std::max_align_t));
        // Queues move jobs around as raw bytes, so captures must be plain values/pointers/references.
        static_assert(std::is_trivially_copyable_v<Fn>, "capture only plain values, pointers and references");
        Job job;
        new (job.storage) Fn(std::forward<F>(fn));
        job.invoke = [](Job& j) {
            (*std::launder(reinterpret_cast<Fn*>(j.storage)))();
        };
        job.counter = counter;
        submit(job);
    }

    // Calls fn(begin, end) over [0, count) in batches of `batchSize`, spread across workers.
    // `fn` is referenced, not copied: keep it alive until the counter (or wait) finishes.
    template <typename F>
    void parallelFor(u32 count, u32 batchSize, const F& fn, JobCounter* counter)
    {
        batchSize = batchSize == 0 ? 1 : batchSize;
        for (u32 begin = 0; begin < count; begin += batchSize) {
            const u32 end = begin + batchSize < count ? begin + batchSize : count;
            run([&fn, begin, end] { fn(begin, end); }, counter);
        }
    }

    // Convenience: parallelFor and wait for it.
    template <typename F>
    void parallelFor(u32 count, u32 batchSize, const F& fn)
    {
        JobCounter counter;
        parallelFor(count, batchSize, fn, &counter);
        wait(counter);
    }

    // Runs jobs until the counter is done.
    void wait(JobCounter& counter);

    u32 threadCount() const { return static_cast<u32>(m_queues.size()); } // workers + main thread
    // 0 = main thread, 1.. = workers, ~0u = a thread the job system doesn't know.
    static u32 currentThreadIndex();

private:
    struct Queue {
        std::mutex mutex; // short critical sections; a lock-free Chase-Lev deque is a later upgrade
        std::vector<Job, memory::TrackedAllocator<Job, memory::Tag::Jobs>> jobs;
    };

    void submit(Job& job);
    bool tryRunOne(u32 self);
    bool popOwn(u32 self, Job& out);
    bool steal(u32 self, Job& out);
    void workerLoop(u32 index);
    static void execute(Job& job);

    std::vector<std::unique_ptr<Queue>> m_queues; // [0] = main thread
    std::vector<std::thread> m_threads;
    std::atomic<u32> m_queued{0};
    std::atomic<bool> m_stopping{false};
    std::mutex m_sleepMutex;
    std::condition_variable m_wake;
};

} // namespace ember
