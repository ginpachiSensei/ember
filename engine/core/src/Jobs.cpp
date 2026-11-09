#include "ember/core/Jobs.h"

#include "ember/core/Profile.h"

#include <string>

#if defined(EMBER_PROFILING)
#include <tracy/Tracy.hpp>
#endif

namespace ember {

namespace {
thread_local u32 t_threadIndex = ~0u;
}

JobSystem::JobSystem(u32 workerCount)
{
    if (workerCount == 0) {
        const u32 hardware = std::thread::hardware_concurrency();
        workerCount = hardware > 1 ? hardware - 1 : 1;
    }
    t_threadIndex = 0; // the creating thread is the "main" thread
    for (u32 i = 0; i <= workerCount; ++i) {
        m_queues.push_back(std::make_unique<Queue>());
    }
    for (u32 i = 1; i <= workerCount; ++i) {
        m_threads.emplace_back([this, i] { workerLoop(i); });
    }
}

JobSystem::~JobSystem()
{
    // Drain what's left, so no job (or counter) is silently dropped.
    while (m_queued.load() > 0) {
        tryRunOne(0);
    }
    m_stopping.store(true);
    {
        std::lock_guard lock(m_sleepMutex);
    }
    m_wake.notify_all();
    for (std::thread& thread : m_threads) {
        thread.join();
    }
}

u32 JobSystem::currentThreadIndex()
{
    return t_threadIndex;
}

void JobSystem::submit(Job& job)
{
    if (job.counter) {
        job.counter->pending.fetch_add(1, std::memory_order_relaxed);
    }
    const u32 self = t_threadIndex < m_queues.size() ? t_threadIndex : 0;
    {
        Queue& queue = *m_queues[self];
        std::lock_guard lock(queue.mutex);
        queue.jobs.push_back(job);
    }
    m_queued.fetch_add(1, std::memory_order_release);
    {
        std::lock_guard lock(m_sleepMutex); // pairs with the sleeping worker's predicate check
    }
    m_wake.notify_one();
}

bool JobSystem::popOwn(u32 self, Job& out)
{
    Queue& queue = *m_queues[self];
    std::lock_guard lock(queue.mutex);
    if (queue.jobs.empty()) {
        return false;
    }
    out = queue.jobs.back();
    queue.jobs.pop_back();
    return true;
}

bool JobSystem::steal(u32 self, Job& out)
{
    const u32 count = static_cast<u32>(m_queues.size());
    for (u32 offset = 1; offset < count; ++offset) {
        Queue& victim = *m_queues[(self + offset) % count];
        std::lock_guard lock(victim.mutex);
        if (!victim.jobs.empty()) {
            out = victim.jobs.front();
            victim.jobs.erase(victim.jobs.begin());
            return true;
        }
    }
    return false;
}

void JobSystem::execute(Job& job)
{
    job.invoke(job);
    if (job.counter) {
        job.counter->pending.fetch_sub(1, std::memory_order_acq_rel);
    }
}

bool JobSystem::tryRunOne(u32 self)
{
    Job job;
    if (popOwn(self, job) || steal(self, job)) {
        m_queued.fetch_sub(1, std::memory_order_acq_rel);
        execute(job);
        return true;
    }
    return false;
}

void JobSystem::wait(JobCounter& counter)
{
    EMBER_PROFILE_SCOPE("JobSystem::wait");
    const u32 self = t_threadIndex < m_queues.size() ? t_threadIndex : 0;
    while (!counter.done()) {
        if (!tryRunOne(self)) {
            std::this_thread::yield(); // the remaining jobs are running on other threads
        }
    }
}

void JobSystem::workerLoop(u32 index)
{
    t_threadIndex = index;
#if defined(EMBER_PROFILING)
    const std::string name = "Worker " + std::to_string(index);
    tracy::SetThreadName(name.c_str());
#endif
    while (true) {
        if (tryRunOne(index)) {
            continue;
        }
        std::unique_lock lock(m_sleepMutex);
        m_wake.wait(lock, [&] { return m_stopping.load() || m_queued.load(std::memory_order_acquire) > 0; });
        if (m_stopping.load() && m_queued.load() == 0) {
            return;
        }
    }
}

} // namespace ember
