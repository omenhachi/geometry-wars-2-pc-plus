// SPIKE: demo recording and playback (demo.h). Everything is keyed by the round's simulation tick:
//  - at round start, the three random states: the Mersenne Twister the game seeds once at boot (sub_82030010 from
//    sub_82037C20, state at kTwister), the C runtime's rand() seed (per thread, +20 in sub_82131318's block) and the
//    small particle generator (kParticleSeed);
//  - the controllers as each tick sees them: the game polls every pad into its slots (sub_82038300, kPadArray) once a
//    frame, and Player::Update reads the slots during the tick. At high frame rates there are several polls per tick,
//    so the slots are captured just before each tick; on replay every poll is given the coming tick's slots, so the
//    per-frame code in between sees what the tick will;
//  - every Player::Fire aim direction (the mouse writes it directly);
//  - every tick's step: the frame pacer (frame_limiter.cpp) nudges the game's 1/60 s step to match real time, so
//    the steps differ from run to run;
//  - a checksum per tick: score, round time and a hash of the whole twister state, so any difference in random draws
//    shows at once.

#include "demo.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>

#include "guest_memory.h"
#include "player_records.h"

REXCVAR_DEFINE_STRING(gw2_demo, "", "Geometry Wars", "SPIKE: record or replay single-player rounds");
REXCVAR_DEFINE_STRING(gw2_demo_file, "", "Geometry Wars", "SPIKE: the demo file to write or play");

REX_EXTERN(sub_82131318);  // the C runtime's per-thread data block
REX_EXTERN(sub_8208FFF0);  // switches the screen (here to the in-game HUD, sub_8207CA48)
REX_EXTERN(sub_82032F90);  // queues the function that sets up the next round
REX_EXTERN(sub_820334F8);  // asks for a round to start, for the pad in r3

namespace gw2::demo {
namespace {

constexpr uint32_t kTwister = 0x821A1E28;  // 624 words, then the next index
constexpr uint32_t kTwisterSize = 2500;
constexpr uint32_t kParticleSeed = 0x82180720;
constexpr uint32_t kRandSeedOffset = 20;
constexpr uint32_t kElapsedAddress = 0x8219CE68;
constexpr uint32_t kModeAddress = 0x8219D254;
constexpr uint32_t kStepAddress = 0x8219D280;  // the game's step in seconds (float)
// The per-frame input state (sub_82033E18): two hold counters, the buttons of the frame before (against which new
// presses are found: a round started from the menus begins with the button that started it still held), then the
// pad that plays, the only one read during the round.
constexpr uint32_t kInputStateAddress = 0x821993E0;
constexpr uint32_t kInputStateSize = 16;
constexpr uint32_t kPlayingPadOffset = 12;
constexpr uint32_t kPadArray = 0x8219D3D4;  // count, then a pointer to the slots
constexpr uint32_t kMaxPads = 4;
constexpr uint32_t kSlotStride = 24;         // user index, connected, then the 16-byte XINPUT_STATE
constexpr uint32_t kSlotDataOffset = 4;
constexpr uint32_t kSlotDataSize = 20;

struct Pads {
  uint8_t slots[kMaxPads][kSlotDataSize] = {};
};
struct Fire {
  uint32_t tick;
  uint32_t x, y;
};
struct Check {
  uint64_t score;
  uint32_t elapsed;
  uint32_t twister;
};

struct Demo {
  uint32_t mode = 0;
  uint8_t twister[kTwisterSize] = {};
  uint32_t rand_seed = 0;
  uint32_t particle_seed = 0;
  uint8_t input_state[kInputStateSize] = {};
  std::vector<Pads> pads;  // one per tick
  std::vector<Fire> fires;
  std::vector<uint32_t> steps;  // one per tick
  // Who drew from the twister in each tick (the caller's address; the top bit set for draws outside a tick), for
  // finding a draw the replay does not repeat. Not saved: compared live against the recording's file below.
  std::vector<uint32_t> draws;
  std::vector<uint32_t> draw_starts;  // per tick, where its draws begin in `draws`
  std::vector<Check> checks;    // one per tick
};

// How the Single Player screen starts a mode (sub_8208D410): the HUD screen, the mode's setup queued, then the
// start asked for. The start state must be idle.
constexpr uint32_t kStartStateAddress = 0x821993F0;  // 0 or 3 menus, 1 begin, 2 playing, 4 game over
constexpr uint32_t kHudScreen = 0x8207CA48;
constexpr uint32_t kModeSetups[] = {0x82034EA0, 0x82034F38, 0x82034FA8, 0x82035030, 0x820350B0, 0x82035130};
std::atomic<bool> g_watch_requested{false};

std::mutex g_mutex;
Demo g_demo;
bool g_active = false;  // in a round being recorded or replayed
bool g_replay = false;
uint32_t g_tick = 0;
size_t g_fire = 0;
bool g_diverged = false;
bool g_in_tick = false;
std::vector<uint32_t> g_tick_draws;  // this tick's draws, while replaying
bool g_draws_differed = false;

bool Recording() { return REXCVAR_GET(gw2_demo) == "record"; }
bool Replaying() { return REXCVAR_GET(gw2_demo) == "replay"; }

uint32_t Hash(const uint8_t* data, size_t size) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < size; ++i) h = (h ^ data[i]) * 16777619u;
  return h;
}

Check Snapshot(const uint8_t* base) {
  return {LoadBE64(base, players::kFirstRecordAddress + 40), LoadBE32(base, kElapsedAddress),
          Hash(base + kTwister, kTwisterSize)};
}

template <typename T>
void Put(std::ofstream& out, const T& value) {
  out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}
template <typename T>
bool Get(std::ifstream& in, T& value) {
  return bool(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

void Save(const Demo& demo) {
  std::ofstream out(REXCVAR_GET(gw2_demo_file), std::ios::binary);
  out.write("GW2D", 4);
  Put(out, demo.mode);
  out.write(reinterpret_cast<const char*>(demo.twister), kTwisterSize);
  Put(out, demo.rand_seed);
  Put(out, demo.particle_seed);
  out.write(reinterpret_cast<const char*>(demo.input_state), kInputStateSize);
  Put(out, uint32_t(demo.pads.size()));
  out.write(reinterpret_cast<const char*>(demo.pads.data()), std::streamsize(demo.pads.size() * sizeof(Pads)));
  Put(out, uint32_t(demo.fires.size()));
  out.write(reinterpret_cast<const char*>(demo.fires.data()), std::streamsize(demo.fires.size() * sizeof(Fire)));
  Put(out, uint32_t(demo.steps.size()));
  out.write(reinterpret_cast<const char*>(demo.steps.data()), std::streamsize(demo.steps.size() * sizeof(uint32_t)));
  Put(out, uint32_t(demo.draws.size()));
  out.write(reinterpret_cast<const char*>(demo.draws.data()), std::streamsize(demo.draws.size() * sizeof(uint32_t)));
  Put(out, uint32_t(demo.draw_starts.size()));
  out.write(reinterpret_cast<const char*>(demo.draw_starts.data()),
            std::streamsize(demo.draw_starts.size() * sizeof(uint32_t)));
  Put(out, uint32_t(demo.checks.size()));
  out.write(reinterpret_cast<const char*>(demo.checks.data()), std::streamsize(demo.checks.size() * sizeof(Check)));
  REXLOG_INFO("Demo: saved {} ticks, {} pad states, {} shots to {}", demo.checks.size(), demo.pads.size(),
              demo.fires.size(), REXCVAR_GET(gw2_demo_file));
}

bool Load(Demo& demo) {
  std::ifstream in(REXCVAR_GET(gw2_demo_file), std::ios::binary);
  char magic[4];
  uint32_t n = 0;
  if (!in.read(magic, 4) || std::memcmp(magic, "GW2D", 4) != 0) return false;
  if (!Get(in, demo.mode) || !in.read(reinterpret_cast<char*>(demo.twister), kTwisterSize)) return false;
  if (!Get(in, demo.rand_seed) || !Get(in, demo.particle_seed)) return false;
  if (!in.read(reinterpret_cast<char*>(demo.input_state), kInputStateSize)) return false;
  constexpr uint32_t kLimit = 10'000'000;
  if (!Get(in, n) || n > kLimit) return false;
  demo.pads.resize(n);
  if (!in.read(reinterpret_cast<char*>(demo.pads.data()), std::streamsize(n * sizeof(Pads)))) return false;
  if (!Get(in, n) || n > kLimit) return false;
  demo.fires.resize(n);
  if (!in.read(reinterpret_cast<char*>(demo.fires.data()), std::streamsize(n * sizeof(Fire)))) return false;
  if (!Get(in, n) || n > kLimit) return false;
  demo.steps.resize(n);
  if (!in.read(reinterpret_cast<char*>(demo.steps.data()), std::streamsize(n * sizeof(uint32_t)))) return false;
  if (!Get(in, n) || n > kLimit * 10) return false;
  demo.draws.resize(n);
  if (!in.read(reinterpret_cast<char*>(demo.draws.data()), std::streamsize(n * sizeof(uint32_t)))) return false;
  if (!Get(in, n) || n > kLimit) return false;
  demo.draw_starts.resize(n);
  if (!in.read(reinterpret_cast<char*>(demo.draw_starts.data()), std::streamsize(n * sizeof(uint32_t)))) return false;
  if (!Get(in, n) || n > kLimit) return false;
  demo.checks.resize(n);
  return bool(in.read(reinterpret_cast<char*>(demo.checks.data()), std::streamsize(n * sizeof(Check))));
}

// The pad slots, read or written. Slots past the game's count are left alone.
uint32_t PadSlots(const uint8_t* base, uint32_t& count) {
  count = std::min(LoadBE32(base, kPadArray), kMaxPads);
  return LoadBE32(base, kPadArray + 4);
}

Pads ReadPads(const uint8_t* base) {
  Pads pads;
  uint32_t count = 0;
  const uint32_t slots = PadSlots(base, count);
  for (uint32_t i = 0; slots != 0 && i < count; ++i) {
    std::memcpy(pads.slots[i], base + slots + i * kSlotStride + kSlotDataOffset, kSlotDataSize);
  }
  return pads;
}

void WritePads(uint8_t* base, const Pads& pads) {
  uint32_t count = 0;
  const uint32_t slots = PadSlots(base, count);
  for (uint32_t i = 0; slots != 0 && i < count; ++i) {
    std::memcpy(base + slots + i * kSlotStride + kSlotDataOffset, pads.slots[i], kSlotDataSize);
  }
}

// Stores `value` at `tick`: the round setup may run inside a tick, whose entry BeforeRoundStart has already taken.
template <typename T>
void PutAt(std::vector<T>& list, uint32_t tick, const T& value) {
  if (list.size() == tick) list.push_back(value);
  else if (tick < list.size()) list[tick] = value;
}

}  // namespace

void BeforeRoundStart(PPCContext& ctx, uint8_t* base) {
  if (!Recording() && !Replaying()) return;
  if (LoadBE32(base, players::kPlayerCountAddress) != 1) return;
  PPCContext call = ctx;
  sub_82131318(call, base);
  const uint32_t rand_seed = call.r3.u32 + kRandSeedOffset;
  std::lock_guard lock(g_mutex);
  g_tick = 0;
  g_fire = 0;
  g_diverged = false;
  g_draws_differed = false;
  g_tick_draws.clear();
  g_replay = Replaying();
  if (g_replay) {
    g_demo = Demo{};
    if (!Load(g_demo)) {
      REXLOG_WARN("Demo: could not read {}", REXCVAR_GET(gw2_demo_file));
      g_active = false;
      return;
    }
    std::memcpy(base + kTwister, g_demo.twister, kTwisterSize);
    StoreBE32(base, rand_seed, g_demo.rand_seed);
    StoreBE32(base, kParticleSeed, g_demo.particle_seed);
    std::memcpy(base + kInputStateAddress, g_demo.input_state, kInputStateSize);
    if (!g_demo.steps.empty()) StoreBE32(base, kStepAddress, g_demo.steps[0]);
    if (!g_demo.pads.empty()) WritePads(base, g_demo.pads[0]);
    REXLOG_INFO("Demo: replaying {} ticks (mode {}, this round's mode {})", g_demo.checks.size(), g_demo.mode,
                LoadBE32(base, kModeAddress));
  } else {
    g_demo = Demo{};
    g_demo.mode = LoadBE32(base, kModeAddress);
    std::memcpy(g_demo.twister, base + kTwister, kTwisterSize);
    g_demo.rand_seed = LoadBE32(base, rand_seed);
    g_demo.particle_seed = LoadBE32(base, kParticleSeed);
    std::memcpy(g_demo.input_state, base + kInputStateAddress, kInputStateSize);
    g_demo.steps.push_back(LoadBE32(base, kStepAddress));
    g_demo.pads.push_back(ReadPads(base));
    REXLOG_INFO("Demo: recording mode {}", g_demo.mode);
  }
  g_active = true;
}

void BeforeSimTick(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  if (!g_active) return;
  g_in_tick = true;
  if (!g_replay) {
    PutAt(g_demo.steps, g_tick, LoadBE32(base, kStepAddress));
    PutAt(g_demo.pads, g_tick, ReadPads(base));
  } else {
    if (g_tick < g_demo.steps.size()) StoreBE32(base, kStepAddress, g_demo.steps[g_tick]);
    if (g_tick < g_demo.pads.size()) WritePads(base, g_demo.pads[g_tick]);
  }
}

void OnSimTick(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  if (!g_active) return;
  g_in_tick = false;
  const Check now = Snapshot(base);
  if (!g_replay) {
    g_demo.checks.push_back(now);
    g_demo.draw_starts.push_back(uint32_t(g_demo.draws.size()));  // where the next tick's draws begin
  } else if (g_tick < g_demo.checks.size()) {
    const uint32_t begin = g_tick == 0 ? 0 : g_demo.draw_starts[g_tick - 1];
    const uint32_t end = g_tick < g_demo.draw_starts.size() ? g_demo.draw_starts[g_tick] : begin;
    const std::vector<uint32_t> want_draws(g_demo.draws.begin() + begin, g_demo.draws.begin() + end);
    if (!g_draws_differed && want_draws != g_tick_draws) {
      g_draws_differed = true;
      std::string got_text, want_text;
      for (uint32_t c : g_tick_draws) got_text += std::format(" {:08X}", c);
      for (uint32_t c : want_draws) want_text += std::format(" {:08X}", c);
      REXLOG_WARN("Demo: twister draws differ at tick {} ({} vs {} recorded)", g_tick, g_tick_draws.size(), want_draws.size());
      REXLOG_WARN("Demo:   replay:  {}", got_text);
      REXLOG_WARN("Demo:   recorded:{}", want_text);
    }
    g_tick_draws.clear();
    const Check& want = g_demo.checks[g_tick];
    if (!g_diverged && (want.score != now.score || want.elapsed != now.elapsed || want.twister != now.twister)) {
      g_diverged = true;
      REXLOG_WARN("Demo: DIVERGED at tick {}: score {} vs {}, time {:08X} vs {:08X}, twister {:08X} vs {:08X}",
                  g_tick, now.score, want.score, now.elapsed, want.elapsed, now.twister, want.twister);
    }
    if (g_tick % 600 == 0 || g_tick + 1 == g_demo.checks.size()) {
      REXLOG_INFO("Demo: tick {} score {} (recorded {}){}", g_tick, now.score, want.score,
                  g_diverged ? " diverged" : " in step");
    }
  }
  ++g_tick;
}

void StartWatching(PPCContext& ctx, uint8_t* base) {
  if (!g_watch_requested.exchange(false)) return;
  Demo demo;
  if (!Replaying() || !Load(demo) || demo.mode >= std::size(kModeSetups)) {
    REXLOG_WARN("Demo: nothing to watch");
    return;
  }
  if (LoadBE32(base, kStartStateAddress) != 0) {
    REXLOG_WARN("Demo: the game is not idle (start state {})", LoadBE32(base, kStartStateAddress));
    return;
  }
  PPCContext call = ctx;
  call.r3.u64 = 0;
  call.r4.u64 = 0;
  call.r5.u64 = kHudScreen;
  call.r6.u64 = 0;
  call.r7.u64 = 0;
  call.r8.u64 = 0;
  sub_8208FFF0(call, base);
  call = ctx;
  call.r3.u64 = kModeSetups[demo.mode];
  sub_82032F90(call, base);
  uint32_t pad = 0;
  std::memcpy(&pad, demo.input_state + kPlayingPadOffset, 4);
  call = ctx;
  call.r3.u64 = std::byteswap(pad);  // stored as the game keeps it, big-endian
  sub_820334F8(call, base);
  // The frame that begins the round (sub_82033E18, later this frame) keeps its pads as the ones held before the round,
  // as when Start or A on the Single Player screen begins one: the recording's first pads are still held then, so a
  // round started with Start does not begin with a fresh Start press, which would pause it.
  if (!demo.pads.empty()) WritePads(base, demo.pads[0]);
  REXLOG_INFO("Demo: starting mode {} to watch, pad {}", demo.mode, std::byteswap(pad));
}

void AfterPadPoll(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  if (!g_active || !g_replay || g_demo.pads.empty()) return;
  // Every poll sees the slots the coming tick will: the viewer's own controller never reaches the round.
  WritePads(base, g_demo.pads[std::min<size_t>(g_tick, g_demo.pads.size() - 1)]);
}

void OnFire(uint8_t* base, uint32_t player, uint32_t direction) {
  if (direction == 0 || !players::IsPlayer(base, player)) return;
  std::lock_guard lock(g_mutex);
  if (!g_active) return;
  if (!g_replay) {
    g_demo.fires.push_back({g_tick, LoadBE32(base, direction), LoadBE32(base, direction + 4)});
    return;
  }
  // This tick's shots, in order.
  while (g_fire < g_demo.fires.size() && g_demo.fires[g_fire].tick < g_tick) ++g_fire;
  if (g_fire >= g_demo.fires.size() || g_demo.fires[g_fire].tick != g_tick) return;
  const Fire& fire = g_demo.fires[g_fire++];
  StoreBE32(base, direction, fire.x);
  StoreBE32(base, direction + 4, fire.y);
}

void RegisterKeys() {
  rex::ui::RegisterBind("bind_gw2_demo_watch", "F9", "Watch the demo", [] { g_watch_requested.store(true); });
}

void OnRunEnd() {
  std::lock_guard lock(g_mutex);
  if (!g_active) return;
  g_active = false;
  if (!g_replay) {
    Save(g_demo);
  } else {
    REXLOG_INFO("Demo: replay ended at tick {} of {}, {}", g_tick, g_demo.checks.size(),
                g_diverged ? "DIVERGED" : "in step throughout");
  }
}

void OnTwisterDraw(uint32_t caller) {
  std::lock_guard lock(g_mutex);
  if (!g_active) return;
  const uint32_t entry = caller | (g_in_tick ? 0u : 0x80000000u);
  if (!g_replay) g_demo.draws.push_back(entry);
  else g_tick_draws.push_back(entry);
}

}  // namespace gw2::demo

// The frame's pad poll (r3 the pad array): on replay, the slots are replaced once it has filled them.
REX_EXTERN(__imp__sub_82038300);
REX_HOOK_RAW(sub_82038300) {
  __imp__sub_82038300(ctx, base);
  gw2::demo::AfterPadPoll(base);
  gw2::demo::StartWatching(ctx, base);
}
