#include "../src/memory_policy.h"
#include <cstdint>
#include <cstdio>

int main() {
  uint32_t cases = 0;
  // All possible 12-bit MiB inputs from the original rlwinm instruction.
  for (uint32_t available = 0; available < 4096; ++available) {
    for (uint32_t reserve = 14; reserve <= 256; ++reserve) {
      const auto adjusted = fifa07::AvailableForAssetPreallocation(available, reserve);
      // Replay the following original PPC compare/subtract/shift operations.
      const uint32_t actual = adjusted > 14 ? (adjusted - 14) << 20 : 0;
      const uint32_t expected = available > reserve ? (available - reserve) << 20 : 0;
      if (actual != expected || adjusted > available ||
          (reserve == 14 && adjusted != available)) {
        std::printf("FAIL available=%u reserve=%u actual=%u expected=%u\n",
                    available, reserve, actual, expected);
        return 1;
      }
      ++cases;
    }
  }
  std::printf("PASS %u cases; zero/threshold/full 12-bit range and baseline identity\n", cases);
}
