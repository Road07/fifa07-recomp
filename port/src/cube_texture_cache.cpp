#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

REX_EXTERN(__imp__sub_822A7A50);
REX_EXTERN(__imp__sub_821CFBA8); // Original D3D resource AddRef.
REX_EXTERN(__imp__sub_821CFC20); // Original D3D resource Release.

namespace {
constexpr uint32_t kMaximumEntries = 8;
constexpr uint32_t kMaximumPhysicalBytes = 4 * 1024 * 1024;
struct Entry {
  uint32_t device, texture, physical_bytes;
  uint64_t hash;
  std::vector<uint8_t> dds;
};
std::mutex cache_mutex;
std::vector<Entry> entries;
uint32_t physical_bytes = 0;
uint64_t hits = 0;

bool Enabled() {
  static const bool enabled = [] {
    char* raw = nullptr;
    size_t length = 0;
    if (_dupenv_s(&raw, &length, "FIFA07_SHARE_CUBE_TEXTURES") != 0 || !raw)
      return true;
    std::unique_ptr<char, decltype(&std::free)> owned(raw, &std::free);
    return std::string_view(raw) == "1";
  }();
  return enabled;
}
uint32_t Read32(uint32_t address) {
  uint32_t value;
  std::memcpy(&value, REX_KERNEL_MEMORY()->TranslateVirtual(address), 4);
  return std::byteswap(value);
}
void Write32(uint32_t address, uint32_t value) {
  value = std::byteswap(value);
  std::memcpy(REX_KERNEL_MEMORY()->TranslateVirtual(address), &value, 4);
}
void Reference(PPCContext& parent, uint8_t* base, uint32_t texture, bool add) {
  PPCContext call = parent;
  call.r3.u64 = texture;
  if (add) __imp__sub_821CFBA8(call, base);
  else __imp__sub_821CFC20(call, base);
}
uint32_t PhysicalBytes(uint32_t texture) {
  const uint32_t first = Read32(texture + 32) & 0xFFFFF000;
  const uint32_t second = Read32(texture + 48) & 0xFFFFF000;
  uint32_t total = 0;
  for (const auto address : {first, second == first ? 0u : second}) {
    if (!address) continue;
    auto* heap = REX_KERNEL_MEMORY()->LookupHeap(address);
    uint32_t size = 0;
    if (!heap || !heap->QuerySize(address, &size) || size > kMaximumPhysicalBytes)
      return kMaximumPhysicalBytes + 1;
    total += size;
  }
  return total;
}
}

// This entrypoint has fixed creation options. Restrict sharing to small DDS cube
// assets with exactly identical complete contents on the same guest device.
// The cache holds one real guest reference per entry for the process lifetime;
// callers get their own AddRef. No guest call runs under cache_mutex.
bool Fifa07CreateSharedCube(PPCContext& ctx, uint8_t* base) {
  if (!Enabled()) return false;
  const uint32_t device = ctx.r3.u32, data = ctx.r4.u32, size = ctx.r5.u32;
  const uint32_t output = ctx.r6.u32;
  if (!data || !output || size < 128 || size > 256 * 1024 || Read32(data) != 0x44445320)
    return false;
  const uint32_t width = std::byteswap(Read32(data + 16));
  const uint32_t height = std::byteswap(Read32(data + 12));
  if (!width || !height || width > 256 || height > 256) return false;
  const auto* bytes = REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(data);
  uint64_t hash = 14695981039346656037ull;
  for (uint32_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
  uint32_t texture = 0;
  uint64_t hit = 0;
  {
    std::lock_guard lock(cache_mutex);
    for (const auto& entry : entries) {
      if (entry.device == device && entry.hash == hash && entry.dds.size() == size &&
          std::memcmp(entry.dds.data(), bytes, size) == 0) {
        texture = entry.texture;
        hit = ++hits;
        break;
      }
    }
  }
  if (texture) {
    Reference(ctx, base, texture, true);
    Write32(output, texture);
    ctx.r3.u64 = 0; // Same S_OK result as successful original creation.
    if (hit <= 6 || hit % 64 == 0)
      REXLOG_INFO("[cube-share] hit={} texture={:08X} references={} dds_bytes={}",
                  hit, texture, Read32(texture + 4), size);
    return true;
  }

  std::vector<uint8_t> snapshot(bytes, bytes + size);
  __imp__sub_822A7A50(ctx, base);
  if (ctx.r3.u32 != 0) return true;
  texture = Read32(output);
  if (!texture || Read32(texture + 4) == 0) return true;
  const uint32_t cost = PhysicalBytes(texture);
  if (!cost || cost > kMaximumPhysicalBytes) return true;

  // Pin before publication. A concurrent miss may publish the same bytes first;
  // in that case drop just our extra reference, preserving the caller's result.
  Reference(ctx, base, texture, true);
  bool inserted = false;
  uint32_t count = 0, total = 0;
  {
    std::lock_guard lock(cache_mutex);
    bool duplicate = false;
    for (const auto& entry : entries)
      if (entry.device == device && entry.hash == hash && entry.dds == snapshot)
        duplicate = true;
    if (!duplicate && entries.size() < kMaximumEntries &&
        cost <= kMaximumPhysicalBytes - physical_bytes) {
      entries.push_back({device, texture, cost, hash, std::move(snapshot)});
      physical_bytes += cost;
      count = static_cast<uint32_t>(entries.size());
      total = physical_bytes;
      inserted = true;
    }
  }
  if (!inserted) Reference(ctx, base, texture, false);
  else REXLOG_INFO("[cube-share] inserted={} texture={:08X} physical_bytes={} total_physical_bytes={} references={}",
                   count, texture, cost, total, Read32(texture + 4));
  return true;
}

REX_EXTERN(sub_822A7A50) {
  if (!Fifa07CreateSharedCube(ctx, base)) __imp__sub_822A7A50(ctx, base);
}

void Fifa07ResetCubeSharing() {
  std::lock_guard lock(cache_mutex);
  entries.clear();
  physical_bytes = 0;
  hits = 0;
}

void Fifa07ReportCubeSharing(rex::memory::Memory* memory) {
  size_t count;
  uint32_t bytes;
  uint64_t reused;
  {
    std::lock_guard lock(cache_mutex);
    count = entries.size();
    bytes = physical_bytes;
    reused = hits;
  }
  REXLOG_INFO("[cube-share-summary] entries={} hits={} pinned_physical_bytes={} free_physical_pages={}",
              count, reused, bytes, memory->GetPhysicalHeap()->GetUnreservedPageCount());
}
