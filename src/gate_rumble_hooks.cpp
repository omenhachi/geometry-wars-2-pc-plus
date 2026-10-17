#include <array>
#include <chrono>

#include <rex/hook.h>
#include <rex/logging.h>

#include "guest_memory.h"
#include "player_records.h"
#include "rumble.h"

namespace {

using Clock = std::chrono::steady_clock;

// Return address of the GateScore call inside the gate update; other callers of the score function are ignored.
constexpr uint32_t kGateScoreCaller = 0x82047A34;
constexpr uint32_t kMaxListNodes = 64;
constexpr auto kGateCooldown = std::chrono::milliseconds(180);

struct Passage {
  uint32_t gate = 0;
  uint32_t player = 0;
  uint32_t score_id = 0;
};

struct Latch {
  uint32_t gate = 0;
  uint32_t player = 0;
  bool inside = false;
  bool pulsed = false;
  Clock::time_point last_pulse{};
};

std::array<Passage, 8> g_passages;
size_t g_passage_count = 0;
std::array<Latch, 8> g_latches;
size_t g_next_latch = 0;
uint32_t g_local_player = 0;

template <typename Visit>
void ForEachListObject(const uint8_t* base, uint32_t sentinel, Visit visit) {
  uint32_t node = gw2::LoadBE32(base, sentinel + 4);
  for (uint32_t i = 0; node != 0 && i < kMaxListNodes; ++i) {
    const uint32_t object = gw2::LoadBE32(base, node);
    if (object == 0) {
      return;
    }
    visit(object);
    node = gw2::LoadBE32(base, node + 4);
  }
}

uint32_t FindLocalPlayer(const uint8_t* base) {
  uint32_t found = 0;
  ForEachListObject(base, gw2::players::kListSentinelAddress, [&](uint32_t player) {
    const bool removed = (gw2::LoadBE32(base, player + gw2::players::kFlagsOffset) & gw2::players::kRemovedBit) != 0;
    if (found == 0 && !removed && gw2::LoadBE32(base, player + gw2::cheats::kPlayerIndexOffset) == 0) {
      found = player;
    }
  });
  return found;
}

void RecordPassage(uint32_t gate, uint32_t player, uint32_t score_id) {
  if (g_passage_count < g_passages.size()) {
    g_passages[g_passage_count++] = Passage{gate, player, score_id};
  }
}

bool PassageContains(uint32_t gate, uint32_t player) {
  for (size_t i = 0; i < g_passage_count; ++i) {
    if (g_passages[i].gate == gate && g_passages[i].player == player) {
      return true;
    }
  }
  return false;
}

Latch& LatchFor(uint32_t gate, uint32_t player) {
  for (Latch& latch : g_latches) {
    if (latch.gate == gate && latch.player == player) {
      return latch;
    }
  }
  Latch& latch = g_latches[g_next_latch];
  g_next_latch = (g_next_latch + 1) % g_latches.size();
  latch = Latch{gate, player};
  return latch;
}

void EnterGate(Latch& latch, uint32_t score_id) {
  REXLOG_INFO("[DEBUG_SAULO][Gate] enter gate=0x{:08X} score_id={}", latch.gate, score_id);
  const auto now = Clock::now();
  if (latch.pulsed && now - latch.last_pulse < kGateCooldown) {
    REXLOG_INFO("[DEBUG_SAULO][Rumble] gate duplicate suppressed gate=0x{:08X}", latch.gate);
    return;
  }
  latch.pulsed = true;
  latch.last_pulse = now;
  gw2::rumble::RequestGatePulse(latch.gate, score_id, kGateScoreCaller);
}

void EndGateUpdate() {
  for (Latch& latch : g_latches) {
    if (latch.inside && !PassageContains(latch.gate, latch.player)) {
      latch.inside = false;
      REXLOG_INFO("[DEBUG_SAULO][Gate] exit gate=0x{:08X}", latch.gate);
    }
  }
  for (size_t i = 0; i < g_passage_count; ++i) {
    const Passage& passage = g_passages[i];
    Latch& latch = LatchFor(passage.gate, passage.player);
    if (!latch.inside) {
      latch.inside = true;
      EnterGate(latch, passage.score_id);
    }
  }
}

}  // namespace

// The game's GateScore call repeats every update while the ship is in the gate band. Its rising edge is the
// rumble trigger, so only the local player's passages from the gate update's call site are recorded.
REX_EXTERN(__imp__sub_820476F0);
REX_HOOK_RAW(sub_820476F0) {
  g_local_player = FindLocalPlayer(base);
  g_passage_count = 0;
  __imp__sub_820476F0(ctx, base);
  EndGateUpdate();
}

REX_EXTERN(__imp__sub_820787F8);
REX_HOOK_RAW(sub_820787F8) {
  if (static_cast<uint32_t>(ctx.lr) == kGateScoreCaller && g_local_player != 0) {
    const uint32_t player = gw2::LoadBE32(base, ctx.r31.u32);
    if (player == g_local_player) {
      RecordPassage(gw2::LoadBE32(base, ctx.r28.u32), player, ctx.r3.u32);
    }
  }
  __imp__sub_820787F8(ctx, base);
}
