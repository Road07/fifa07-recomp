// Diagnostic-only hook used during the investigation; excluded from normal builds.
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <bit>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <string>

REX_EXTERN(__imp__sub_825B67B0);
REX_EXTERN(__imp__sub_822A7A50);
bool Fifa07CreateSharedCube(PPCContext& ctx, uint8_t* base);

namespace {
thread_local uint32_t texture_asset = 0;
std::mutex count_mutex;
std::unordered_map<uint32_t, uint32_t> loads;
uint32_t Read32(uint32_t address) {
  uint32_t value;
  std::memcpy(&value, REX_KERNEL_MEMORY()->TranslateVirtual(address), 4);
  return std::byteswap(value);
}
std::string Name(uint32_t resource) {
  if (!resource || Read32(Read32(resource) + 48) != 0x8277C5D8) return {};
  std::string result;
  for (uint32_t i = 0; i < 260; ++i) {
    const char c = *REX_KERNEL_MEMORY()->TranslateVirtual<const char*>(resource + 8 + i);
    if (!c) break;
    result += c;
  }
  return result;
}
}

REX_EXTERN(sub_825B67B0) {
  const auto previous = texture_asset;
  texture_asset = ctx.r3.u32;
  const uint32_t resource = Read32(texture_asset + 12);
  const uint32_t cached = resource ? Read32(resource + 4) : 0;
  uint32_t count = 0;
  if (resource) {
    std::lock_guard lock(count_mutex);
    count = ++loads[resource];
  }
  REXLOG_INFO("[texture-load] asset={:08X} resource={:08X} cached={:08X} visit={} name={}",
              texture_asset, resource, cached, count, Name(resource));
  __imp__sub_825B67B0(ctx, base);
  REXLOG_INFO("[texture-loaded] asset={:08X} resource={:08X} cached={:08X}",
              texture_asset, resource, resource ? Read32(resource + 4) : 0);
  texture_asset = previous;
}

REX_EXTERN(sub_822A7A50) {
  if (Fifa07CreateSharedCube(ctx, base)) return;
  REXLOG_INFO("[texture-cube] asset={:08X} data={:08X} bytes={} magic={:08X}",
              texture_asset, ctx.r4.u32, ctx.r5.u32, ctx.r5.u32 >= 4 ? Read32(ctx.r4.u32) : 0);
  if (ctx.r5.u32 >= 128 && Read32(ctx.r4.u32) == 0x44445320) {
    REXLOG_INFO("[texture-dds] width={} height={} mipmaps={} fourcc={:08X} caps2={:08X}",
                std::byteswap(Read32(ctx.r4.u32 + 16)), std::byteswap(Read32(ctx.r4.u32 + 12)),
                std::byteswap(Read32(ctx.r4.u32 + 28)), Read32(ctx.r4.u32 + 84),
                std::byteswap(Read32(ctx.r4.u32 + 112)));
  }
  __imp__sub_822A7A50(ctx, base);
}
