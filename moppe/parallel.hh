#ifndef MOPPE_PARALLEL_HH
#define MOPPE_PARALLEL_HH

#include <moppe/environment.hh>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <thread>
#include <vector>

namespace moppe {
  // The threads a parallel loop uses: every hardware thread, or
  // MOPPE_THREADS of them (results do not depend on it; tests check that).
  inline std::size_t parallel_width () {
    static const std::size_t width = [] {
      if (const char* threads = moppe::environment ("MOPPE_THREADS"))
        if (const int count = std::atoi (threads); count > 0)
          return static_cast<std::size_t> (count);
      return static_cast<std::size_t> (
        std::max (1u, std::thread::hardware_concurrency ()));
    }();
    return width;
  }

  // Calls body (begin, end) over [0, count) in contiguous ranges, one per
  // thread, and returns when all are done. A body that writes only the
  // elements of its own range computes the same bits however many threads
  // there are, which world generation needs (docs/determinism.md). Below
  // `grain` elements per thread the loop runs on the caller's thread alone.
  template <typename Body>
  void parallel_for (std::size_t count, std::size_t grain, const Body& body) {
    const std::size_t threads =
      std::min (parallel_width (), std::max<std::size_t> (1, count / grain));
    if (threads <= 1) {
      body (std::size_t { 0 }, count);
      return;
    }
    const auto range = [&] (std::size_t part) {
      return std::pair { count * part / threads, count * (part + 1) / threads };
    };
    std::vector<std::jthread> workers;
    workers.reserve (threads - 1);
    for (std::size_t part = 1; part < threads; ++part)
      workers.emplace_back ([&body, range, part] {
        const auto [begin, end] = range (part);
        body (begin, end);
      });
    const auto [begin, end] = range (0);
    body (begin, end);
  }
}

#endif
