#include "rumble.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <utility>

#include <rex/hook.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_memory.h"
#include "launcher_cvars.h"
#include "player_records.h"

REX_EXTERN(__imp__sub_820383B8);

namespace gw2::rumble {
namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

// Lower value means higher priority.
enum class Event : uint8_t {
  kGameOver,
  kDeath,
  kBomb,
  kGravityOverload,
  kGravityExplosion,
  kExtraLife,
  kExtraBomb,
  kRespawn,
  kGate,
  kShieldHit
};

constexpr auto kShieldCooldown = milliseconds(250);
constexpr auto kAmbientTimeout = milliseconds(100);
constexpr uint32_t kLocalUser = 0;
constexpr std::array<float, 5> kKingLevels{0.00f, 0.10f, 0.18f, 0.28f, 0.40f};

struct Step {
  float left;
  float right;
  uint32_t duration_ms;
};

struct Pattern {
  std::array<Step, 3> steps;
  size_t count;
};

constexpr Pattern PatternFor(Event event) {
  switch (event) {
    case Event::kGameOver:
      return {{Step{1.00f, 1.00f, 220}, Step{0.00f, 0.00f, 100}, Step{1.00f, 0.70f, 300}}, 3};
    case Event::kDeath:
      return {{Step{1.00f, 0.55f, 320}}, 1};
    case Event::kBomb:
      return {{Step{1.00f, 1.00f, 200}}, 1};
    case Event::kGravityOverload:
      return {{Step{0.80f, 0.65f, 180}}, 1};
    case Event::kGravityExplosion:
      return {{Step{0.60f, 0.50f, 140}}, 1};
    case Event::kExtraLife:
      return {{Step{0.45f, 0.70f, 160}}, 1};
    case Event::kExtraBomb:
      return {{Step{0.35f, 0.55f, 120}}, 1};
    case Event::kRespawn:
      return {{Step{0.35f, 0.60f, 150}}, 1};
    case Event::kGate:
      return {{Step{0.30f, 0.30f, 80}}, 1};
    case Event::kShieldHit:
      return {{Step{0.20f, 0.20f, 60}}, 1};
  }
  return {};
}

const char* Name(Event event) {
  switch (event) {
    case Event::kGameOver:
      return "game_over";
    case Event::kDeath:
      return "death";
    case Event::kBomb:
      return "bomb";
    case Event::kGravityOverload:
      return "gravity_well_overload";
    case Event::kGravityExplosion:
      return "gravity_well_explosion";
    case Event::kExtraLife:
      return "extra_life";
    case Event::kExtraBomb:
      return "extra_bomb";
    case Event::kRespawn:
      return "respawn";
    case Event::kGate:
      return "gate";
    case Event::kShieldHit:
      return "shield_hit";
  }
  return "";
}

// The game's XInputSetState wrapper reads the controller state through this pointer on every call.
constexpr uint32_t kVibrationGateAddress = 0x820005D4;
// Scratch word for the user index that sub_820383B8 reads through r3. It lies below the callee's own frame.
constexpr uint32_t kUserSlotOffset = 128;

struct Playback {
  bool active = false;
  Event event = Event::kBomb;
  uint32_t user = 0;
  size_t step = 0;
  Clock::time_point started{};
  Clock::time_point step_started{};
};

struct Motors {
  uint32_t user = 0;
  float left = 0.0f;
  float right = 0.0f;
  uint32_t result = 0;
};

struct Queued {
  bool valid = false;
  Event event = Event::kBomb;
  uint32_t user = 0;
};

struct KingZone {
  float x = 0.0f;
  float y = 0.0f;
  float radius = 0.0f;
  float fraction = 0.0f;
  bool seen = false;
  Clock::time_point seen_at{};
};

struct LocalPlayer {
  float x = 0.0f;
  float y = 0.0f;
  float radius = 0.0f;
  bool seen = false;
  Clock::time_point seen_at{};
};

struct State {
  std::mutex mutex;
  bool accepting = true;
  bool enabled = false;
  Queued queued;
  Queued deferred;
  Playback playback;
  Motors motors;
  std::array<bool, players::kRecordCount> death_seen{};
  std::array<bool, players::kRecordCount> respawn_expected{};
  bool shield_pulse_seen = false;
  Clock::time_point last_shield_pulse{};
  KingZone king_zone;
  LocalPlayer local_player;
  uint8_t king_stage = 0;
};

State g_state;

bool Committed(uint32_t address) {
  rex::memory::BaseHeap* heap = rex::system::kernel_memory()->LookupHeap(address);
  rex::memory::HeapAllocationInfo info{};
  return heap && heap->QueryRegionInfo(address, &info) && (info.state & rex::memory::kMemoryAllocationCommit);
}

// Guest memory the vibration call reads. Checked first so a missing page cannot fault the guest.
bool CanDrive(uint8_t* base, uint32_t slot) {
  const uint32_t gate = gw2::LoadBE32(base, kVibrationGateAddress);
  return gate != 0 && Committed(gate) && Committed(gate + 3) && Committed(slot) && Committed(slot + 3);
}

uint32_t SetMotors(PPCContext& ctx, uint8_t* base, uint32_t slot, uint32_t user, float left, float right) {
  gw2::StoreBE32(base, slot, user);
  rex::CallFrame frame(ctx);
  frame.ctx.r3.u64 = slot;
  frame.ctx.f1.f64 = left;
  frame.ctx.f2.f64 = right;
  __imp__sub_820383B8(frame, base);
  return frame.ctx.r3.u32;
}

// Caller holds g_state.mutex. Calls into the guest only when the motor state changes.
bool ApplyMotors(PPCContext& ctx, uint8_t* base, uint32_t user, float left, float right) {
  Motors& motors = g_state.motors;
  if (motors.user == user && motors.left == left && motors.right == right) {
    return true;
  }
  const uint32_t slot = ctx.r1.u32 - kUserSlotOffset;
  if (!CanDrive(base, slot)) {
    REXLOG_INFO("[DEBUG_SAULO][Rumble] skipped: guest vibration memory not committed gate=0x{:08X}",
                gw2::LoadBE32(base, kVibrationGateAddress));
    return false;
  }
  if (motors.user != user && (motors.left != 0.0f || motors.right != 0.0f)) {
    SetMotors(ctx, base, slot, motors.user, 0.0f, 0.0f);
  }
  const uint32_t result = SetMotors(ctx, base, slot, user, left, right);
  motors = {user, left, right, result};
  return true;
}

// Caller holds g_state.mutex.
void StopPlayback(PPCContext& ctx, uint8_t* base, Clock::time_point now) {
  Playback& playback = g_state.playback;
  ApplyMotors(ctx, base, g_state.motors.user, 0.0f, 0.0f);
  REXLOG_INFO("[DEBUG_SAULO][Rumble] stop event={} user={} elapsed_ms={}", Name(playback.event), playback.user,
              std::chrono::duration_cast<milliseconds>(now - playback.started).count());
  playback.active = false;
}

// Caller holds g_state.mutex.
void StartPlayback(PPCContext& ctx, uint8_t* base, Event event, uint32_t user, Clock::time_point now) {
  const Step first = PatternFor(event).steps[0];
  if (!ApplyMotors(ctx, base, user, first.left, first.right)) {
    return;
  }
  g_state.playback = {true, event, user, 0, now, now};
  REXLOG_INFO("[DEBUG_SAULO][Rumble] start event={} user={} left={:.3f} right={:.3f} duration_ms={} xinput=0x{:08X}",
              Name(event), user, first.left, first.right, first.duration_ms, g_state.motors.result);
}

// Caller holds g_state.mutex. Moves through the steps whose time has run out, so each motor change happens once.
void AdvancePlayback(PPCContext& ctx, uint8_t* base, Clock::time_point now) {
  Playback& playback = g_state.playback;
  const Pattern pattern = PatternFor(playback.event);
  while (now - playback.step_started >= milliseconds(pattern.steps[playback.step].duration_ms)) {
    playback.step_started += milliseconds(pattern.steps[playback.step].duration_ms);
    if (++playback.step == pattern.count) {
      StopPlayback(ctx, base, now);
      return;
    }
    const Step& step = pattern.steps[playback.step];
    if (!ApplyMotors(ctx, base, playback.user, step.left, step.right)) {
      StopPlayback(ctx, base, now);
      return;
    }
    if (pattern.count > 1) {
      REXLOG_INFO("[DEBUG_SAULO][Rumble] step event={} index={} left={:.3f} right={:.3f} duration_ms={} xinput=0x{:08X}",
                  Name(playback.event), playback.step, step.left, step.right, step.duration_ms, g_state.motors.result);
    }
  }
}

bool IsDeferrable(Event event) {
  return event == Event::kGravityOverload || event == Event::kGravityExplosion || event == Event::kExtraLife ||
         event == Event::kExtraBomb;
}

// Caller holds g_state.mutex. A deferrable event that loses the queue is kept for the next free slot, not dropped.
void DeferLocked(Queued dropped) {
  if (!IsDeferrable(dropped.event)) {
    return;
  }
  if (!g_state.deferred.valid || dropped.event <= g_state.deferred.event) {
    g_state.deferred = dropped;
  }
}

// Caller holds g_state.mutex. A higher-priority request replaces the queued one; equal priority takes the newest.
void QueueLocked(Event event, uint32_t user, uint32_t caller) {
  if (!g_state.enabled) {
    return;
  }
  REXLOG_INFO("[DEBUG_SAULO][Rumble] event={} user={} caller=0x{:08X}", Name(event), user, caller);
  const Queued incoming{true, event, user};
  if (!g_state.queued.valid || event <= g_state.queued.event) {
    if (g_state.queued.valid) {
      DeferLocked(g_state.queued);
    }
    g_state.queued = incoming;
  } else {
    DeferLocked(incoming);
  }
}

uint8_t KingStageFor(float fraction) {
  if (fraction > 0.75f) {
    return 1;
  }
  if (fraction > 0.50f) {
    return 2;
  }
  if (fraction > 0.25f) {
    return 3;
  }
  return 4;
}

bool Fresh(bool seen, Clock::time_point seen_at, Clock::time_point now) {
  return seen && now - seen_at <= kAmbientTimeout;
}

// Caller holds g_state.mutex. The same containment test as the zone's own capture check: strictly inside.
bool KingInside(Clock::time_point now) {
  const KingZone& zone = g_state.king_zone;
  const LocalPlayer& player = g_state.local_player;
  if (!Fresh(zone.seen, zone.seen_at, now) || !Fresh(player.seen, player.seen_at, now)) {
    return false;
  }
  const float dx = zone.x - player.x;
  const float dy = zone.y - player.y;
  const float reach = zone.radius + player.radius;
  return dx * dx + dy * dy < reach * reach;
}

// Caller holds g_state.mutex.
void TrackKingStage(uint8_t stage) {
  uint8_t& current = g_state.king_stage;
  if (stage == current) {
    return;
  }
  if (stage == 0) {
    REXLOG_INFO("[DEBUG_SAULO][Rumble] event=king exit");
  } else if (current == 0) {
    REXLOG_INFO("[DEBUG_SAULO][Rumble] event=king enter stage={}", stage);
  } else {
    REXLOG_INFO("[DEBUG_SAULO][Rumble] event=king stage={}", stage);
  }
  current = stage;
}

// Caller holds g_state.mutex. Ambient feedback is the idle motor state; a pulse overrides it until it ends.
void ApplyAmbient(PPCContext& ctx, uint8_t* base, Clock::time_point now) {
  const uint8_t king_stage = KingInside(now) ? KingStageFor(g_state.king_zone.fraction) : 0;
  TrackKingStage(king_stage);
  const float level = kKingLevels[king_stage];
  ApplyMotors(ctx, base, kLocalUser, level, level);
}

// Caller holds g_state.mutex. Follows the Vibration setting and returns whether rumble may play. Turning it off drops
// every pending pulse so none plays after it is turned on again.
bool SyncEnabled(PPCContext& ctx, uint8_t* base) {
  const bool enabled = REXCVAR_GET(gw2_vibration);
  if (enabled != g_state.enabled) {
    g_state.enabled = enabled;
    REXLOG_INFO("[DEBUG_SAULO][Rumble] enabled={}", enabled ? 1 : 0);
    if (!enabled) {
      g_state.queued = {};
      g_state.deferred = {};
      g_state.playback.active = false;
      g_state.shield_pulse_seen = false;
      TrackKingStage(0);
    }
  }
  if (!enabled) {
    ApplyMotors(ctx, base, g_state.motors.user, 0.0f, 0.0f);
  }
  return enabled;
}

}  // namespace

void RequestBombPulse(uint32_t user, uint32_t caller) {
  std::lock_guard lock(g_state.mutex);
  if (g_state.accepting) {
    QueueLocked(Event::kBomb, user, caller);
  }
}

void RequestExtraLifePulse(uint32_t user, uint32_t caller) {
  std::lock_guard lock(g_state.mutex);
  if (g_state.accepting) {
    QueueLocked(Event::kExtraLife, user, caller);
  }
}

void RequestExtraBombPulse(uint32_t user, uint32_t caller) {
  std::lock_guard lock(g_state.mutex);
  if (g_state.accepting) {
    QueueLocked(Event::kExtraBomb, user, caller);
  }
}

void RequestGravityOverloadPulse(uint32_t caller) {
  std::lock_guard lock(g_state.mutex);
  if (g_state.accepting) {
    QueueLocked(Event::kGravityOverload, kLocalUser, caller);
  }
}

void RequestGravityExplosionPulse(uint32_t caller) {
  std::lock_guard lock(g_state.mutex);
  if (g_state.accepting) {
    QueueLocked(Event::kGravityExplosion, kLocalUser, caller);
  }
}

void RequestGatePulse(uint32_t gate, uint32_t score_id, uint32_t caller) {
  std::lock_guard lock(g_state.mutex);
  if (g_state.accepting && g_state.enabled) {
    REXLOG_INFO("[DEBUG_SAULO][Rumble] event=gate user={} gate=0x{:08X} score_id={}", kLocalUser, gate, score_id);
    QueueLocked(Event::kGate, kLocalUser, caller);
  }
}

// Shield absorbs are frequent while touching enemies, so they are rate limited and never queue behind a pulse.
void RequestShieldHitPulse(uint32_t user, uint32_t caller) {
  std::lock_guard lock(g_state.mutex);
  const auto now = Clock::now();
  if (!g_state.accepting || !g_state.enabled || g_state.playback.active) {
    return;
  }
  if (g_state.shield_pulse_seen && now - g_state.last_shield_pulse < kShieldCooldown) {
    return;
  }
  g_state.shield_pulse_seen = true;
  g_state.last_shield_pulse = now;
  QueueLocked(Event::kShieldHit, user, caller);
}

// Repeated kills of the same death are ignored until the player spawns again.
void PlayerKilled(uint32_t user, bool final_life, bool game_over_possible, uint32_t caller) {
  std::lock_guard lock(g_state.mutex);
  if (user == kLocalUser) {
    g_state.local_player.seen = false;
  }
  if (!g_state.accepting || user >= players::kRecordCount || g_state.death_seen[user]) {
    return;
  }
  g_state.death_seen[user] = true;
  g_state.deferred = {};
  if (final_life && game_over_possible) {
    QueueLocked(Event::kGameOver, user, caller);
    return;
  }
  g_state.respawn_expected[user] = !final_life;
  QueueLocked(Event::kDeath, user, caller);
}

// The first spawn of a player has no preceding death, so it does not count as a respawn.
void PlayerSpawned(uint32_t user, uint32_t caller) {
  std::lock_guard lock(g_state.mutex);
  if (!g_state.accepting || user >= players::kRecordCount) {
    return;
  }
  const bool respawn = std::exchange(g_state.respawn_expected[user], false);
  g_state.death_seen[user] = false;
  if (respawn) {
    QueueLocked(Event::kRespawn, user, caller);
  }
}

void ObserveKingZone(float x, float y, float radius, float fraction) {
  std::lock_guard lock(g_state.mutex);
  if (!g_state.accepting || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) ||
      !std::isfinite(fraction)) {
    return;
  }
  g_state.king_zone = {x, y, radius, std::clamp(fraction, 0.0f, 1.0f), true, Clock::now()};
}

void ObserveLocalPlayer(float x, float y, float radius) {
  std::lock_guard lock(g_state.mutex);
  if (!g_state.accepting || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius)) {
    return;
  }
  g_state.local_player = {x, y, radius, true, Clock::now()};
}

// Once per game frame on the guest thread; the only place guest code is called for rumble.
void ServiceFrame(PPCContext& ctx, uint8_t* base) {
  std::lock_guard lock(g_state.mutex);
  if (!g_state.accepting) {
    return;
  }
  if (!SyncEnabled(ctx, base)) {
    return;
  }
  const auto now = Clock::now();
  if (!g_state.queued.valid && g_state.deferred.valid) {
    g_state.queued = std::exchange(g_state.deferred, Queued{});
  }
  if (g_state.queued.valid &&
      (!g_state.playback.active || g_state.queued.event <= g_state.playback.event)) {
    const Queued queued = std::exchange(g_state.queued, Queued{});
    StartPlayback(ctx, base, queued.event, queued.user, now);
  }
  if (g_state.playback.active) {
    AdvancePlayback(ctx, base, now);
  }
  if (!g_state.playback.active) {
    ApplyAmbient(ctx, base, now);
  }
}

// Host-only: guest code must not run during teardown.
void Shutdown(rex::system::IInputSystem* input_system) {
  Motors stopping;
  {
    std::lock_guard lock(g_state.mutex);
    g_state.accepting = false;
    g_state.queued = {};
    g_state.deferred = {};
    g_state.playback.active = false;
    stopping = std::exchange(g_state.motors, Motors{});
  }
  auto* input = dynamic_cast<rex::input::InputSystem*>(input_system);
  if ((stopping.left != 0.0f || stopping.right != 0.0f) && input) {
    rex::input::X_INPUT_VIBRATION stop{};
    input->SetState(stopping.user, &stop);
  }
}

}  // namespace gw2::rumble
