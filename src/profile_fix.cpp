#include <atomic>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>

#include "profile_fix.h"

namespace {

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorIoPending = 0x3E5;

}  // namespace

// sub_820E89B8 wraps XamUserReadProfileSettings(title, user, count, ids, size ptr, buffer, overlapped).
// ReXGlue completes the read before returning, so the buffer is final once the original returns.
REX_EXTERN(__imp__sub_820E89B8);
REX_HOOK_RAW(sub_820E89B8) {
  const uint32_t buffer = ctx.r8.u32;
  const uint32_t overlapped = ctx.r9.u32;
  __imp__sub_820E89B8(ctx, base);
  uint32_t status = ctx.r3.u32;
  if (status == kErrorIoPending && overlapped) status = gw2::LoadBE32(base, overlapped);
  if (!buffer || status != kErrorSuccess) return;

  const auto summary = gw2::ReportUnsavedTitleSettingsAsDefault(base, buffer);
  static std::atomic<int> logged{0};
  if ((summary.saved || summary.defaulted) && logged.fetch_add(1) < 8) {
    REXLOG_INFO("[DEBUG_SAULO][Save] profile read: {} saved title setting(s), {} unsaved reported as default",
                summary.saved, summary.defaulted);
  }
}
