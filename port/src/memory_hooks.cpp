#include "memory_hooks.h"
#include "memory_policy.h"
#include <rex/ppc/context.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <bit>
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

namespace {
uint32_t ReserveMiB() {
  static const uint32_t value = [] {
    char* raw = nullptr;
    size_t length = 0;
    if (_dupenv_s(&raw, &length, "FIFA07_PHYSICAL_RESERVE_MIB") != 0 || !raw)
      return 14u;
    std::unique_ptr<char, decltype(&std::free)> owned(raw, &std::free);
    uint32_t parsed = 0;
    const auto result = std::from_chars(raw, raw + std::strlen(raw), parsed);
    return result.ec == std::errc{} && *result.ptr == '\0' && parsed >= 14 && parsed <= 256
        ? parsed : 14u;
  }();
  return value;
}

struct Request {
  uint32_t size = 0, caller = 0, free_before = 0;
  std::array<uint32_t, 6> callers{};
};
thread_local Request pending;

bool TracePhysicalAllocations() {
  static const bool value = [] {
    char* raw = nullptr;
    size_t length = 0;
    if (_dupenv_s(&raw, &length, "FIFA07_TRACE_PHYSICAL") != 0 || !raw)
      return false;
    std::unique_ptr<char, decltype(&std::free)> owned(raw, &std::free);
    return std::string_view(raw) == "1";
  }();
  return value;
}

uint32_t PhysicalFreePages() {
  return REX_KERNEL_MEMORY()->GetPhysicalHeap()->GetUnreservedPageCount();
}
}

void Fifa07AssetReserve(rex::ppc::Register& available) {
  const uint32_t original = available.u32;
  available.u64 = fifa07::AvailableForAssetPreallocation(original, ReserveMiB());
  const uint32_t request_mib = available.u32 > 14 ? available.u32 - 14 : 0;
  REXLOG_INFO("[memory-policy] available_mib={} reserve_mib={} preallocate_mib={} physical_free_pages={}",
              original, ReserveMiB(), request_mib, PhysicalFreePages());
}

void Fifa07PhysicalBefore(rex::ppc::Register& size, rex::ppc::Register& stack) {
  uint32_t saved_lr;
  std::memcpy(&saved_lr, REX_KERNEL_MEMORY()->TranslateVirtual(stack.u32 + 88), sizeof(saved_lr));
  pending = {size.u32, std::byteswap(saved_lr),
             TracePhysicalAllocations() || size.u32 >= 1048576 || size.u32 == 196608
                 ? PhysicalFreePages() : 0};
  if (size.u32 == 196608) {
    uint32_t frame = stack.u32;
    for (auto& caller : pending.callers) {
      uint32_t parent;
      std::memcpy(&parent, REX_KERNEL_MEMORY()->TranslateVirtual(frame), 4);
      parent = std::byteswap(parent);
      if (parent <= frame || parent - frame > 1048576 || (parent & 15)) break;
      std::memcpy(&caller, REX_KERNEL_MEMORY()->TranslateVirtual(parent - 8), 4);
      caller = std::byteswap(caller);
      if (caller < 0x82000000 || caller >= 0x83000000) { caller = 0; break; }
      frame = parent;
    }
  }
}

void Fifa07PhysicalAfter(rex::ppc::Register& result) {
  if (pending.size == 196608) {
    static thread_local std::array<uint32_t, 6> last_callers{};
    if (pending.callers != last_callers || result.u32 == 0) {
      last_callers = pending.callers;
      REXLOG_INFO("[physical-owner] size={} stack={:08X},{:08X},{:08X},{:08X},{:08X},{:08X} free_before_pages={} result={:08X}",
                  pending.size, pending.callers[0], pending.callers[1], pending.callers[2],
                  pending.callers[3], pending.callers[4], pending.callers[5], pending.free_before, result.u32);
    }
  }
  if (!TracePhysicalAllocations() && result.u32 != 0 && pending.size < 1048576)
    return;
  REXLOG_INFO("[physical-allocation] size={} caller={:08X} result={:08X} free_before_pages={} free_after_pages={}",
              pending.size, pending.caller, result.u32, pending.free_before, PhysicalFreePages());
}

void Fifa07PhysicalDirectBefore(rex::ppc::Register& size) {
  pending = {size.u32, 0x821DCC54,
             TracePhysicalAllocations() || size.u32 >= 1048576 ? PhysicalFreePages() : 0};
}
