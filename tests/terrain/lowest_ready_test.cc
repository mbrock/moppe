#include <moppe/terrain/lowest_ready.hh>

#include <tests/test.hh>

#include <cstdint>
#include <random>
#include <set>

using namespace moppe;

// The queue hands back what a heap of indices would: the lowest of those
// waiting, whatever was pushed in between.
MOPPE_TEST (lowest_ready_takes_the_lowest_index_first) {
  for (const std::size_t count : { 1u, 63u, 64u, 65u, 4096u, 4097u, 300000u }) {
    terrain::LowestReady ready (count);
    std::set<std::uint32_t> waiting;
    std::mt19937 random (static_cast<std::uint32_t> (count));
    MOPPE_CHECK (ready.empty ());
    bool same = true;
    for (int round = 0; round < 2000; ++round) {
      const std::uint32_t cell = static_cast<std::uint32_t> (random () % count);
      if (waiting.insert (cell).second)
        ready.push (cell);
      if (random () % 3 == 0 && !waiting.empty ()) {
        same = same && ready.pop () == *waiting.begin ();
        waiting.erase (waiting.begin ());
      }
    }
    while (!waiting.empty ()) {
      same = same && !ready.empty () && ready.pop () == *waiting.begin ();
      waiting.erase (waiting.begin ());
    }
    MOPPE_CHECK (same);
    MOPPE_CHECK (ready.empty ());
  }
}
