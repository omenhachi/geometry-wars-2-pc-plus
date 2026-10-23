#include "live_fix.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <cstring>
#include <mutex>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "achievements_overlay_rules.h"
#include "demo.h"
#include "guest_memory.h"
#include "launcher_settings.h"
#include "online_leaderboard.h"
#include "replays_menu.h"
#include "run_stats_hooks.h"

REXCVAR_DEFINE_BOOL(gw2_local_live, true, "Geometry Wars",
                    "Serve the game's Xbox Live scores and leaderboards from a local file; nothing goes online")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace gw2::live {
namespace {

constexpr uint32_t kSignedInLocally = 1;
constexpr uint32_t kSignedInToLive = 2;
constexpr uint32_t kInputStateGamepadOffset = 4;
constexpr uint32_t kInputStateGamepadSize = 12;
constexpr uint32_t kThumbLeftYOffset = 6;

struct Services {
  Services(std::filesystem::path file, LocalUser user) : stats(std::move(file)), board(stats, std::move(user)) {}

  LocalStats stats;
  LocalLeaderboard board;
  EnumeratorRegistry enumerators;
};

// Never freed: hooks may still run while the process exits.
std::atomic<Services*> g_services{nullptr};
AppBridge g_bridge;
std::mutex g_gate_mutex;
AchievementsOverlayGate g_gate;  // guarded by g_gate_mutex
std::atomic<float> g_scroll_axis{0.0f};
std::atomic<bool> g_holds_input{false};

// The player name from the launcher, read whenever the game asks for scores, since it can change after start.
Services* Active() {
  Services* services = g_services.load(std::memory_order_acquire);
  if (services) services->board.SetDisplayName(online::ValidName(rex::cvar::GetFlagByName(kPlayerNameKey)));
  return services;
}

bool Succeeded(PPCContext& ctx, uint8_t* base, uint32_t overlapped) {
  uint32_t status = ctx.r3.u32;
  if (status == kErrorIoPending && overlapped) status = LoadBE32(base, overlapped);
  return status == kErrorSuccess;
}

bool CreateStatsEnumerator(PPCContext& ctx, uint8_t* base, StatsQuery::Kind kind) {
  Services* services = Active();
  if (!services || ctx.r3.u32 != 0) return false;

  const bool ranked = kind == StatsQuery::Kind::kRanked;
  const uint64_t pivot = ranked ? ctx.r4.u32 : ctx.r4.u64;
  uint32_t handle = 0;
  ctx.r3.u64 = CreateEnumerator(base, services->enumerators, kind, pivot, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32,
                                ctx.r8.u32, ctx.r9.u32, &handle);
  // An online board has a row per player: the game sizes its buffer from this answer.
  if (ctx.r3.u32 == kErrorSuccess && ranked && online::Enabled() && ctx.r8.u32 != 0) {
    const uint32_t rows = std::min(ctx.r5.u32, online::kMaxRows);
    StoreBE32(base, ctx.r8.u32, kResultsHeaderSize + (kViewSize + kRowSize * std::max(rows, 1u)) * ctx.r6.u32);
  }
  return true;
}

// Writes boards into the game's enumeration buffer and completes its overlapped, as ServeEnumeration does.
void ServeViews(uint8_t* base, const std::vector<StatsView>& views, uint32_t buffer, uint32_t size,
                uint32_t items_ptr, uint32_t overlapped) {
  const uint32_t result = WriteStatsResults(base, buffer, size, views) ? kErrorSuccess : kErrorInsufficientBuffer;
  const uint32_t items = result == kErrorSuccess ? 1 : 0;
  if (items_ptr) StoreBE32(base, items_ptr, items);
  CompleteOverlapped(base, overlapped, result, items);
}

void FilterGuestInput(PPCContext& ctx, uint8_t* base, uint32_t user, uint32_t state) {
  if (!state || ctx.r3.u32 != 0) return;
  const uint16_t buttons = LoadBE16(base, state + kInputStateGamepadOffset);
  const int thumb_y = static_cast<int16_t>(LoadBE16(base, state + kInputStateGamepadOffset + kThumbLeftYOffset));
  AchievementsOverlayGate::Verdict verdict;
  {
    std::lock_guard lock(g_gate_mutex);
    verdict = g_gate.Poll(user, buttons, thumb_y);
    g_holds_input.store(g_gate.HoldsInput(), std::memory_order_release);
  }
  g_scroll_axis.store(verdict.scroll, std::memory_order_relaxed);
  if (verdict.close && g_bridge.close_achievements) g_bridge.close_achievements();
  if (verdict.hide_input) {
    std::memset(base + state + kInputStateGamepadOffset, 0, kInputStateGamepadSize);
  } else if (verdict.clear_b) {
    StoreBE<uint16_t>(base, state + kInputStateGamepadOffset, buttons & ~kXinputButtonB);
  }
}

}  // namespace

void Install(InstallParams params) {
  g_bridge = std::move(params.bridge);
  if (!REXCVAR_GET(gw2_local_live)) {
    REXLOG_INFO("Local Live services are disabled (gw2_local_live=false)");
    return;
  }
  online::Configure(params.stats_file.parent_path() / "online_token", params.user.xuid);
  auto* services = new Services(std::move(params.stats_file), std::move(params.user));
  const LoadResult loaded = services->stats.Load();
  REXLOG_INFO("Local Live services ready, stats file {}",
              loaded == LoadResult::kLoaded    ? "loaded"
              : loaded == LoadResult::kMissing ? "not created yet"
                                               : "was corrupt");
  g_services.store(services, std::memory_order_release);
}

void SetAchievementsModal(bool modal) {
  {
    std::lock_guard lock(g_gate_mutex);
    g_gate.SetOpen(modal);
    g_holds_input.store(g_gate.HoldsInput(), std::memory_order_release);
  }
  g_scroll_axis.store(0.0f, std::memory_order_relaxed);
}

bool AchievementsHoldInput() { return g_holds_input.load(std::memory_order_acquire); }

float AchievementsScrollAxis() { return g_scroll_axis.load(std::memory_order_relaxed); }

}  // namespace gw2::live

using namespace gw2;
using namespace gw2::live;

// XUserGetSigninState: the game only treats a player as eligible for scores and leaderboards when it
// reports signed in to Live. ReXGlue's local profile reports signed in locally.
REX_EXTERN(__imp__sub_820955C8);
REX_HOOK_RAW(sub_820955C8) {
  const uint32_t user = ctx.r3.u32;
  __imp__sub_820955C8(ctx, base);
  if (user != 0 || ctx.r3.u32 != kSignedInLocally || !Active()) return;
  ctx.r3.u64 = kSignedInToLive;
}

// XSessionWriteStats(session, xuid, view count, views, overlapped): the score of a finished game.
REX_EXTERN(__imp__sub_820E88F8);
REX_HOOK_RAW(sub_820E88F8) {
  const uint64_t xuid = ctx.r4.u64;
  const uint32_t view_count = ctx.r5.u32;
  const uint32_t views = ctx.r6.u32;
  const uint32_t overlapped = ctx.r7.u32;
  __imp__sub_820E88F8(ctx, base);
  gw2::run::OnScoreWritten(base);
  if (gw2::demo::Watching()) return;  // a replay's score is never kept or sent
  Services* services = Active();
  if (!services || !Succeeded(ctx, base, overlapped)) return;
  if (xuid != services->board.user().xuid) return;

  for (const ScoreWrite& write : ParseScoreWrites(base, views, view_count)) {
    if (services->stats.Record(xuid, write.view, write.score) == RecordResult::kNotSaved) {
      REXLOG_WARN("New best score in view {} kept in memory: the stats file could not be written", write.view);
    }
    // Each finished run's own score, never older local bests: their cheat state is unknown.
    online::SubmitScore(write.view, write.score);
  }
}

// XUserCreateStatsEnumeratorByRank / ByXuid: ReXGlue only stubs the underlying call.
REX_EXTERN(__imp__sub_820956B8);
REX_HOOK_RAW(sub_820956B8) {
  if (!CreateStatsEnumerator(ctx, base, StatsQuery::Kind::kRanked)) __imp__sub_820956B8(ctx, base);
}

REX_EXTERN(__imp__sub_82095718);
REX_HOOK_RAW(sub_82095718) {
  if (!CreateStatsEnumerator(ctx, base, StatsQuery::Kind::kAround)) __imp__sub_82095718(ctx, base);
}

// XEnumerate(handle, buffer, size, items, overlapped). Other handles (friends, achievements) are
// ReXGlue's.
REX_EXTERN(__imp__sub_820958D0);
REX_HOOK_RAW(sub_820958D0) {
  if (Services* services = Active()) {
    const uint32_t handle = ctx.r3.u32;
    if (const auto query = services->enumerators.Take(handle)) {
      const uint32_t overlapped = ctx.r7.u32;
      // The Everyone board comes from the server. The game polls the overlapped (its spinning globe) until the
      // answer arrives, so the fetch runs on its own thread; any failure answers with the local board.
      if (query->kind == StatsQuery::Kind::kRanked && overlapped != 0 && online::Enabled()) {
        StoreBE32(base, overlapped, kErrorIoPending);
        StoreBE32(base, overlapped + 0x18, kErrorIoPending);
        const uint32_t buffer = ctx.r4.u32;
        const uint32_t size = ctx.r5.u32;
        const uint32_t items = ctx.r6.u32;
        std::thread([base, services, buffer, size, items, overlapped, query = *query] {
          std::vector<StatsView> views;
          for (const uint32_t view : query.views) {
            auto board = online::FetchBoard(view, uint32_t(query.pivot), query.max_rows);
            if (!board) break;
            views.push_back(std::move(*board));
          }
          if (views.size() == query.views.size() && StatsResultsSize(views) <= size) {
            ServeViews(base, views, buffer, size, items, overlapped);
          } else {
            ServeEnumeration(base, services->board, query, buffer, size, items, overlapped);
          }
        }).detach();
        ctx.r3.u64 = kErrorIoPending;
        return;
      }
      ctx.r3.u64 = ServeEnumeration(base, services->board, *query, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
      return;
    }
  }
  __imp__sub_820958D0(ctx, base);
}

// XUserReadStats(title, xuid count, xuids, spec count, specs, size ptr, results, overlapped): the
// game's own scores and its friends'. ReXGlue accepts the request and leaves the results empty.
// The game has no friends here, so with online leaderboards on the PC Plus players around the player
// fill the friends' rows: on the mode tiles, and as the score to beat during a run. The game asks for
// the size first (no results, ERROR_INSUFFICIENT_BUFFER) and allocates what that says, so the size
// answer makes room for them; the game sorts the rows and highlights the player's own.
REX_EXTERN(__imp__sub_820955D0);
REX_HOOK_RAW(sub_820955D0) {
  const uint32_t xuid_count = ctx.r4.u32;
  const uint32_t xuids = ctx.r5.u32;
  const uint32_t spec_count = ctx.r6.u32;
  const uint32_t specs = ctx.r7.u32;
  const uint32_t size_ptr = ctx.r8.u32;
  const uint32_t results = ctx.r9.u32;
  const uint32_t overlapped = ctx.r10.u32;
  __imp__sub_820955D0(ctx, base);
  Services* services = Active();
  if (services && !results && size_ptr && ctx.r3.u32 == kErrorInsufficientBuffer && spec_count <= kMaxSpecs &&
      online::Enabled()) {
    StoreBE32(base, size_ptr, LoadBE32(base, size_ptr) + spec_count * uint32_t(online::kNearbyRows) * kRowSize);
    return;
  }
  if (!services || !results || !size_ptr || !Succeeded(ctx, base, overlapped)) return;

  const uint64_t local_xuid = services->board.user().xuid;
  ServeReadStats(base, services->board, xuid_count, xuids, spec_count, specs, LoadBE32(base, size_ptr), results,
                 [local_xuid](uint32_t view, int64_t my_score) {
                   const auto board = online::NearbyBoard(view);
                   return board ? online::PickNearby(*board, my_score, local_xuid) : Nearby{};
                 });
}

// The game's text by number (sub_820826D8 returns string n's 512-byte slot). With the players around you in the
// friends' rows, the friends' names say so: "Friends Leaderboard", the Friends filter and the score display's
// "Next Friend". Online leaderboards off, the game's own text stays.
struct RenamedText {
  uint32_t id;
  const char* text;
  uint32_t guest = 0;  // the copy in guest memory, made on first use
};
RenamedText g_renamed_text[] = {{0x29, "Nearby"}, {0x67, "Nearby Players"}, {0x28, "Next Player"}};

REX_EXTERN(__imp__sub_820826D8);
REX_HOOK_RAW(sub_820826D8) {
  const uint32_t id = ctx.r3.u32;
  if (id == gw2::replays_menu::kWatchReplaysTextId) {
    ctx.r3.u64 = gw2::replays_menu::WatchReplaysText();
    return;
  }
  __imp__sub_820826D8(ctx, base);
  for (RenamedText& renamed : g_renamed_text) {
    if (renamed.id != id || !online::Enabled()) continue;
    if (renamed.guest == 0) {
      // A whole slot, as the game's are, in case anything writes into it.
      const uint32_t slot = rex::system::kernel_memory()->SystemHeapAlloc(520);
      if (slot == 0) return;
      std::memset(base + slot, 0, 520);
      std::memcpy(base + slot, renamed.text, std::strlen(renamed.text));
      renamed.guest = slot;
    }
    ctx.r3.u64 = renamed.guest;
    return;
  }
}

// XamShowAchievementsUI(user): opens the achievements overlay instead of ReXGlue's stub.
REX_EXTERN(__imp__sub_82094DF8);
REX_HOOK_RAW(sub_82094DF8) {
  if (!g_bridge.open_achievements) {
    __imp__sub_82094DF8(ctx, base);
    return;
  }
  g_bridge.open_achievements();
  ctx.r3.u64 = kErrorSuccess;
}

// XInputGetState(user, state): the game stays out of the way while the achievements overlay is up.
REX_EXTERN(__imp__sub_82094B60);
REX_HOOK_RAW(sub_82094B60) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t state = ctx.r4.u32;
  __imp__sub_82094B60(ctx, base);
  FilterGuestInput(ctx, base, user, state);
  gw2::replays_menu::FilterPad(base, user, ctx.r3.u32 == 0 ? state : 0);
}
