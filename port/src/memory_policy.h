#pragma once
#include <cstdint>

namespace fifa07 {
// The following original PPC instructions compare against 14 and subtract 14.
// Adjust their input instead of changing kernel statistics or allocator results.
constexpr uint32_t AvailableForAssetPreallocation(uint32_t available_mib,
                                                uint32_t reserve_mib) {
  const uint32_t extra = reserve_mib > 14 ? reserve_mib - 14 : 0;
  return available_mib > extra ? available_mib - extra : 0;
}
}
