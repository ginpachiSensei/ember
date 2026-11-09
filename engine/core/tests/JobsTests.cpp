#include "ember/core/Jobs.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <numeric>
#include <vector>

using namespace ember;

TEST_CASE("jobs run and counters reach zero")
{
    JobSystem jobs(4);
    std::atomic<int> sum{0};
    JobCounter counter;
    for (int i = 1; i <= 1000; ++i) {
        jobs.run([&sum, i] { sum.fetch_add(i); }, &counter);
    }
    jobs.wait(counter);
    CHECK(counter.done());
    CHECK(sum.load() == 1000 * 1001 / 2);
}

TEST_CASE("parallelFor covers every index exactly once")
{
    JobSystem jobs(3);
    std::vector<int> hits(100000, 0);
    jobs.parallelFor(static_cast<u32>(hits.size()), 1000, [&](u32 begin, u32 end) {
        for (u32 i = begin; i < end; ++i) {
            ++hits[i];
        }
    });
    CHECK(std::accumulate(hits.begin(), hits.end(), 0) == 100000);
    CHECK(std::all_of(hits.begin(), hits.end(), [](int h) { return h == 1; }));
}

TEST_CASE("jobs can spawn and wait on nested jobs without deadlocking")
{
    JobSystem jobs(2);
    std::atomic<int> leaves{0};
    JobCounter outer;
    for (int i = 0; i < 16; ++i) {
        jobs.run([&jobs, &leaves] {
            JobCounter inner;
            for (int j = 0; j < 16; ++j) {
                jobs.run([&leaves] { leaves.fetch_add(1); }, &inner);
            }
            jobs.wait(inner); // a worker waiting here runs other jobs meanwhile
        }, &outer);
    }
    jobs.wait(outer);
    CHECK(leaves.load() == 256);
}
