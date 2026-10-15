#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/system/xmemory.h>

#include "guest_memory.h"

namespace {

// A forward copy equals memmove unless the destination starts inside the source, where it repeats
// the copied bytes.
bool IsPlainForwardCopy(uint32_t dest, uint32_t source, uint32_t size) {
  return dest <= source || dest - source >= size;
}

// One host pointer covers the range (the physical view above 0xE0000000 is offset on some hosts),
// and none of it is the GPU register window, which plain stores would bypass.
bool IsNativeRange(uint32_t address, uint32_t size) {
  const uint64_t end = uint64_t(address) + size;
  return end <= 0x100000000ull && (address >= 0xE0000000u || end <= 0xE0000000ull) &&
         (end <= 0x7F000000ull || address >= 0x80000000u);
}

}  // namespace

// The C runtime's memcpy: r3 destination, r4 source, r5 size; returns the destination, which it
// also leaves at r1 - 8.
REX_EXTERN(__imp__sub_8212EDC0);
REX_HOOK_RAW(sub_8212EDC0) {
  const uint32_t dest = ctx.r3.u32;
  const uint32_t source = ctx.r4.u32;
  const uint32_t size = ctx.r5.u32;
  if (!IsPlainForwardCopy(dest, source, size) || !IsNativeRange(dest, size) ||
      !IsNativeRange(source, size)) {
    __imp__sub_8212EDC0(ctx, base);
    return;
  }
  gw2::StoreBE64(base, ctx.r1.u32 - 8 + rex::memory::detail::PhysicalHostOffset(ctx.r1.u32 - 8),
                 ctx.r3.u64);
  std::memmove(rex::memory::GuestPtr(base, dest), rex::memory::GuestPtr(base, source), size);
}

// The C runtime's memset: r3 destination, r4 fill byte, r5 size; returns the destination.
REX_EXTERN(__imp__sub_82130E10);
REX_HOOK_RAW(sub_82130E10) {
  const uint32_t dest = ctx.r3.u32;
  const uint32_t size = ctx.r5.u32;
  if (!IsNativeRange(dest, size)) {
    __imp__sub_82130E10(ctx, base);
    return;
  }
  std::memset(rex::memory::GuestPtr(base, dest), int(ctx.r4.u32 & 0xFF), size);
}
