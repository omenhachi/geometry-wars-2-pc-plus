// Replays: every single-player round is recorded, and a saved one can be watched (demo.h, demo_file.h).
//
// A round replays exactly from:
//  - its random states at the start: the Mersenne Twister the game seeds once at boot (sub_82030010 from
//    sub_82037C20), the C runtime's rand() seed (per thread, +20 in sub_82131318's block) and a small particle
//    generator. Visual effects draw from their own twister (effect_random.cpp), so only gameplay moves this one;
//  - the per-frame input state (the buttons held before the round, so its first frame finds no new press, and the pad
//    that plays);
//  - each tick's step: the frame pacer (frame_limiter.cpp) nudges the game's 1/60 s step to real time, so the steps
//    differ from run to run;
//  - each tick's pad slots: the game polls every pad into its slots (sub_82038300) once a frame and the tick reads
//    them, so on playback every poll is given the coming tick's slots, whatever the frame rate;
//  - the aim of every shot (Player::Fire), which mouse aim writes directly.
// The score after every second is kept too, so playback can tell when it has gone out of step.
//
// While a replay plays it must never count: its score is not recorded or sent, and it unlocks and saves nothing
// (live_fix.cpp, achievements and the profile write here). Watching() covers the replay's round and its Game Over.

#include "demo.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/keybinds.h>

#include "benchmark.h"
#include "cheats.h"
#include "custom_modes.h"
#include "demo_file.h"
#include "demo_library.h"
#include "master_page.h"
#include "game_files.h"
#include "guest_memory.h"
#include "launcher_cvars.h"
#include "player_records.h"

REXCVAR_DEFINE_BOOL(gw2_replays, true, "Geometry Wars", "Record every single-player run, to watch again");
REXCVAR_DEFINE_BOOL(gw2_replay_test_keys, false, "Geometry Wars", "F9 watches the newest replay (for testing)");

REX_EXTERN(sub_82131318);  // the C runtime's per-thread data block
REX_EXTERN(sub_8208FFF0);  // switches the screen (here to the in-game HUD, sub_8207CA48)
REX_EXTERN(sub_82032F90);  // queues the function that sets up the next round
REX_EXTERN(sub_820334F8);  // asks for a round to start, for the pad in r3

namespace gw2::demo {
namespace {

namespace file = gw2::demo_file;
namespace custom_mode = gw2::custom_mode;
namespace library = gw2::demo_library;

constexpr uint32_t kTwister = 0x821A1E28;
constexpr uint32_t kParticleSeed = 0x82180720;
constexpr uint32_t kRandSeedOffset = 20;
constexpr uint32_t kModeAddress = 0x8219D254;
constexpr uint32_t kStepAddress = 0x8219D280;  // the game's step in seconds (float)
// The per-frame input state (sub_82033E18): two hold counters, the buttons held the frame before (against which new
// presses are found), then the pad that plays.
constexpr uint32_t kInputStateAddress = 0x821993E0;
constexpr uint32_t kPlayingPadOffset = 12;
constexpr uint32_t kPadArray = 0x8219D3D4;  // count, then a pointer to the slots
constexpr uint32_t kSlotStride = 24;        // user index, connected, then the 16-byte XINPUT_STATE
constexpr uint32_t kSlotDataOffset = 4;
constexpr uint32_t kSlotButtonsOffset = 12;  // the gamepad's buttons, after the packet number
constexpr uint16_t kStartButton = 0x0010;
constexpr uint16_t kBackButton = 0x0020;
// How the Single Player screen starts a mode (sub_8208D410): the HUD screen, the mode's setup queued, then the start
// asked for, for the pad that plays. kStartState is 0 or 3 in the menus, 1 while a round begins, 2 while it plays and
// 4 at its Game Over.
constexpr uint32_t kStartState = 0x821993F0;
constexpr uint32_t kHudScreen = 0x8207CA48;
constexpr uint32_t kModeSetups[] = {0x82034EA0, 0x82034F38, 0x82034FA8, 0x82035030, 0x820350B0, 0x82035130};

enum class Phase { kIdle, kRecording, kWatching };

struct State {
  Phase phase = Phase::kIdle;
  file::Demo demo;
  uint32_t tick = 0;
  size_t shot = 0;
  bool round_over = false;  // watching: the replay's round has ended or was stopped; its Game Over may still show
  bool stopped = false;     // the viewer pressed Start: the round is theirs now, and still counts for nothing
  bool viewer_noted = false;
  uint16_t viewer_buttons = 0;  // the viewer's own pads, the poll before
  bool out_of_step = false;
  uint32_t out_of_step_tick = 0;
};

std::mutex g_mutex;
State g_state;
std::filesystem::path g_folder;
std::optional<file::Demo> g_requested;  // to watch, once the game thread can start it
std::atomic<bool> g_watching{false};

uint64_t Score(const uint8_t* base) {
  return LoadBE64(base, players::kFirstRecordAddress + 40);
}

file::Pads ReadPads(const uint8_t* base) {
  file::Pads pads{};
  const uint32_t count = std::min(LoadBE32(base, kPadArray), file::kMaxPads);
  const uint32_t slots = LoadBE32(base, kPadArray + 4);
  for (uint32_t i = 0; slots != 0 && i < count; ++i) {
    std::memcpy(pads.data() + i * file::kPadSize, base + slots + i * kSlotStride + kSlotDataOffset, file::kPadSize);
  }
  return pads;
}

void WritePads(uint8_t* base, const file::Pads& pads) {
  const uint32_t count = std::min(LoadBE32(base, kPadArray), file::kMaxPads);
  const uint32_t slots = LoadBE32(base, kPadArray + 4);
  for (uint32_t i = 0; slots != 0 && i < count; ++i) {
    std::memcpy(base + slots + i * kSlotStride + kSlotDataOffset, pads.data() + i * file::kPadSize, file::kPadSize);
  }
}

bool AnyCheat() {
  using cheats::Cheat;
  return cheats::Enabled(Cheat::Invincible) || cheats::Enabled(Cheat::InfiniteShield) ||
         cheats::Enabled(Cheat::InfiniteBombs);
}

// Saves a finished run on its own thread, then lets older replays go.
void SaveInBackground(file::Demo demo, std::filesystem::path folder) {
  std::thread([demo = std::move(demo), folder = std::move(folder)] {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::string name = library::FileName(demo.mode, demo.final_score, demo.recorded_at);
    const std::filesystem::path path = folder / name;
    const std::filesystem::path temp = folder / (name + ".tmp");
    const std::vector<uint8_t> bytes = file::Write(demo);
    {
      std::ofstream out(temp, std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
      if (!out) {
        REXLOG_WARN("Replays: could not write {}", path.string());
        return;
      }
    }
    std::filesystem::rename(temp, path, error);
    if (error) {
      REXLOG_WARN("Replays: could not save {}: {}", path.string(), error.message());
      return;
    }
    const size_t pruned = library::Prune(folder);
    REXLOG_INFO("Replays: saved {} ({} ticks, {} KB){}", name, demo.ticks(), bytes.size() / 1024,
                pruned ? std::format(", {} older replay(s) let go", pruned) : "");
  }).detach();
}

}  // namespace

void SetFolder(std::filesystem::path folder) {
  std::lock_guard lock(g_mutex);
  g_folder = std::move(folder);
}

std::filesystem::path Folder() {
  std::lock_guard lock(g_mutex);
  return g_folder;
}

bool Watching() {
  return g_watching.load(std::memory_order_relaxed);
}

boards::Counting HowRunCounts() {
  if (Watching()) return boards::Counting::kNone;
  if (custom_modes::IsActive()) return custom_modes::OwnBoard() ? boards::Counting::kOwnBoard : boards::Counting::kNone;
  return boards::Counting::kOfficial;
}

WatchResult Watch(const std::filesystem::path& path) {
  std::error_code error;
  const uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size > file::kMaxFileSize) return WatchResult::kUnreadable;
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  if (!in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))) return WatchResult::kUnreadable;
  file::ReadResult read = file::Read(bytes);
  switch (read.error) {
    case file::ReadError::kNone:
      break;
    case file::ReadError::kNewerFormat:
      return WatchResult::kNewer;
    case file::ReadError::kOtherRules:
      return WatchResult::kOtherVersion;
    default:
      return WatchResult::kUnreadable;
  }
  if (read.demo->ticks() == 0 || read.demo->mode >= std::size(kModeSetups)) return WatchResult::kUnreadable;
  if (read.demo->flags & file::kFlagCustom) {
    const auto rules = custom_mode::Read(read.demo->custom_rules);
    if (!rules || rules->base != read.demo->mode) return WatchResult::kUnreadable;
  }
  std::lock_guard lock(g_mutex);
  g_requested = std::move(read.demo);
  return WatchResult::kStarted;
}

void StartRequested(PPCContext& ctx, uint8_t* base) {
  uint32_t mode = 0, pad = 0;
  {
    std::lock_guard lock(g_mutex);
    if (!g_requested) return;
    // Only from the menus: a round in play or ending is left alone, and the request waits.
    const uint32_t start = LoadBE32(base, kStartState);
    if (start != 0 && start != 3) return;
    g_state = State{};
    g_state.phase = Phase::kWatching;
    g_state.demo = std::move(*g_requested);
    g_requested.reset();
    g_watching.store(true, std::memory_order_relaxed);
    mode = g_state.demo.mode;
    std::optional<custom_mode::Mode> custom;
    if (g_state.demo.flags & file::kFlagCustom) {
      custom.emplace();
      custom->name = g_state.demo.custom_name;
      custom->author = g_state.demo.custom_author;
      custom->rules = *custom_mode::Read(g_state.demo.custom_rules);  // checked by Watch
    }
    custom_modes::ForReplay(std::move(custom));
    std::memcpy(&pad, g_state.demo.input_state.data() + kPlayingPadOffset, 4);
    pad = std::byteswap(pad);  // kept as the game keeps it, big-endian
    if (pad > 3) pad = 0;
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
  call.r3.u64 = kModeSetups[mode];
  sub_82032F90(call, base);
  call = ctx;
  call.r3.u64 = pad;
  sub_820334F8(call, base);
  // The frame that begins the round (sub_82033E18, later this frame) keeps its pads as the ones held before the
  // round, as when Start or A on the Single Player screen begins one: the recording's first pads are held then, so a
  // round begun with Start does not open with a fresh Start press, which would pause it.
  std::lock_guard lock(g_mutex);
  WritePads(base, g_state.demo.PadsAt(0));
  REXLOG_INFO("Replays: watching mode {}, {} ticks, score {}", mode, g_state.demo.ticks(), g_state.demo.final_score);
}

void BeforeRoundStart(PPCContext& ctx, uint8_t* base) {
  PPCContext call = ctx;
  sub_82131318(call, base);
  const uint32_t rand_seed = call.r3.u32 + kRandSeedOffset;
  const bool single_player = LoadBE32(base, players::kPlayerCountAddress) == 1;
  std::lock_guard lock(g_mutex);
  if (g_state.phase == Phase::kWatching && !g_state.round_over) {
    // The replay's own round: its random states and input state come back.
    const file::Demo& demo = g_state.demo;
    std::memcpy(base + kTwister, demo.twister.data(), file::kTwisterSize);
    StoreBE32(base, rand_seed, demo.rand_seed);
    StoreBE32(base, kParticleSeed, demo.particle_seed);
    std::memcpy(base + kInputStateAddress, demo.input_state.data(), file::kInputStateSize);
    StoreBE32(base, kStepAddress, demo.steps[0]);
    WritePads(base, demo.PadsAt(0));
    return;
  }
  // Any other round, Retry after a replay included, is the player's own.
  g_state = State{};
  g_watching.store(false, std::memory_order_relaxed);
  if (!single_player || !REXCVAR_GET(gw2_replays)) return;
  g_state.phase = Phase::kRecording;
  file::Demo& demo = g_state.demo;
  if (const auto custom = custom_modes::Active()) {
    // A custom mode's run keeps its rules, so it plays back without the mode file.
    demo.flags |= file::kFlagCustom;
    demo.custom_name = custom->mode.name;
    demo.custom_author = custom->mode.author;
    const std::vector<uint8_t> rules = custom_mode::Write(custom->mode.rules);
    demo.custom_rules = rules;
  }
  demo.port_version = GW2_PORT_VERSION;
  demo.game_sha256 = kSupportedGame.xex_sha256;
  demo.player_name = REXCVAR_GET(gw2_player_name);
  demo.mode = LoadBE32(base, kModeAddress);
  std::memcpy(demo.twister.data(), base + kTwister, file::kTwisterSize);
  demo.rand_seed = LoadBE32(base, rand_seed);
  demo.particle_seed = LoadBE32(base, kParticleSeed);
  std::memcpy(demo.input_state.data(), base + kInputStateAddress, file::kInputStateSize);
  demo.steps.push_back(LoadBE32(base, kStepAddress));
  file::RecordPads(demo, 0, ReadPads(base));
}

void BeforeSimTick(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  State& s = g_state;
  if (s.phase == Phase::kRecording) {
    // The round setup may run inside a tick, whose entry BeforeRoundStart has already made.
    const uint32_t step = LoadBE32(base, kStepAddress);
    if (s.demo.steps.size() == s.tick) s.demo.steps.push_back(step);
    else if (s.tick < s.demo.steps.size()) s.demo.steps[s.tick] = step;
    if (s.demo.steps.size() > file::kMaxTicks) {
      s = State{};  // an hour: stop recording rather than grow without end
      return;
    }
    file::RecordPads(s.demo, s.tick, ReadPads(base));
  } else if (s.phase == Phase::kWatching && !s.round_over && s.tick < s.demo.ticks()) {
    StoreBE32(base, kStepAddress, s.demo.steps[s.tick]);
    WritePads(base, s.demo.PadsAt(s.tick));
  }
}

void OnSimTick(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  State& s = g_state;
  if (s.phase == Phase::kIdle || (s.phase == Phase::kWatching && s.round_over)) return;
  ++s.tick;
  if (s.tick % file::kCheckpointTicks != 0) return;
  const uint64_t score = Score(base);
  if (s.phase == Phase::kRecording) {
    s.demo.checkpoints.push_back(score);
    return;
  }
  const size_t index = s.tick / file::kCheckpointTicks - 1;
  if (!s.out_of_step && index < s.demo.checkpoints.size() && s.demo.checkpoints[index] != score) {
    s.out_of_step = true;
    s.out_of_step_tick = s.tick;
    REXLOG_WARN("Replays: out of step after {}s (score {}, recorded {})", s.tick / 60, score,
                s.demo.checkpoints[index]);
  }
}

void AfterPadPoll(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  State& s = g_state;
  if (s.phase != Phase::kWatching) return;
  if (s.round_over) {
    // The replay is over once the game is back in its menus.
    const uint32_t start = LoadBE32(base, kStartState);
    if (start == 0 || start == 3) {
      s = State{};
      g_watching.store(false, std::memory_order_relaxed);
      custom_modes::ForReplay(std::nullopt);
    }
    return;
  }
  // The viewer's own Start or Back stops the replay: their pads then reach the game (its pause menu), and the rest of
  // the round still counts for nothing. What they held as it began (the press that chose it) is only noted.
  uint16_t viewer = 0;
  const uint32_t count = std::min(LoadBE32(base, kPadArray), file::kMaxPads);
  const uint32_t slots = LoadBE32(base, kPadArray + 4);
  for (uint32_t i = 0; slots != 0 && i < count; ++i) viewer |= LoadBE16(base, slots + i * kSlotStride + kSlotButtonsOffset);
  const uint16_t pressed = s.viewer_noted ? uint16_t(viewer & ~s.viewer_buttons) : 0;
  s.viewer_buttons = viewer;
  s.viewer_noted = true;
  if (pressed & (kStartButton | kBackButton)) {
    s.round_over = true;
    s.stopped = true;
    REXLOG_INFO("Replays: stopped by the viewer after {} of {} ticks", s.tick, s.demo.ticks());
    return;
  }
  if (s.demo.ticks() > 0) WritePads(base, s.demo.PadsAt(std::min(s.tick, s.demo.ticks() - 1)));
}

void OnFire(uint8_t* base, uint32_t player, uint32_t direction) {
  if (direction == 0 || !players::IsPlayer(base, player)) return;
  std::lock_guard lock(g_mutex);
  State& s = g_state;
  if (s.phase == Phase::kRecording) {
    if (s.demo.shots.size() < s.demo.steps.size() * file::kMaxShotsPerTick) {
      s.demo.shots.push_back({s.tick, LoadBE32(base, direction), LoadBE32(base, direction + 4)});
    }
    return;
  }
  if (s.phase != Phase::kWatching || s.round_over) return;
  while (s.shot < s.demo.shots.size() && s.demo.shots[s.shot].tick < s.tick) ++s.shot;
  if (s.shot >= s.demo.shots.size() || s.demo.shots[s.shot].tick != s.tick) return;
  const file::Shot& shot = s.demo.shots[s.shot++];
  StoreBE32(base, direction, shot.x);
  StoreBE32(base, direction + 4, shot.y);
}

void OnRunEnd(const uint8_t* base) {
  std::lock_guard lock(g_mutex);
  State& s = g_state;
  if (s.phase == Phase::kWatching) {
    if (!s.round_over) {
      s.round_over = true;
      REXLOG_INFO("Replays: replay ended after {} of {} ticks{}", s.tick, s.demo.ticks(),
                  s.out_of_step ? ", out of step" : ", in step throughout");
    }
    return;
  }
  if (s.phase != Phase::kRecording) return;
  file::Demo demo = std::move(s.demo);
  s = State{};
  if (AnyCheat()) return;  // runs with cheats on are not kept
  // A pad change or shot in the tick the run ended in, past the last whole tick, is trimmed so the file reads back.
  while (!demo.pad_changes.empty() && demo.pad_changes.back().tick >= demo.ticks()) demo.pad_changes.pop_back();
  while (!demo.shots.empty() && demo.shots.back().tick >= demo.ticks()) demo.shots.pop_back();
  if (demo.checkpoints.size() > demo.ticks() / file::kCheckpointTicks + 1) {
    demo.checkpoints.resize(demo.ticks() / file::kCheckpointTicks + 1);
  }
  if (demo.ticks() == 0 || demo.pad_changes.empty() || g_folder.empty()) return;
  demo.final_score = Score(base);
  demo.recorded_at =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  SaveInBackground(std::move(demo), g_folder);
}

ReplayStatus Status() {
  std::lock_guard lock(g_mutex);
  const State& s = g_state;
  if (s.phase != Phase::kWatching) return {};
  return {true, s.demo.mode, s.demo.player_name, s.tick, s.demo.ticks(), s.out_of_step, s.out_of_step_tick, s.stopped};
}

void RegisterKeys() {
  rex::ui::RegisterBind("bind_gw2_replay_watch_newest", "F9", "Watch the newest replay (test)", [] {
    if (!REXCVAR_GET(gw2_replay_test_keys)) return;
    const std::filesystem::path folder = Folder();
    const auto entries = library::Scan(folder);
    if (entries.empty()) {
      REXLOG_INFO("Replays: none saved yet in {}", folder.string());
      return;
    }
    const WatchResult result = Watch(folder / entries.front().file);
    REXLOG_INFO("Replays: watch {}: {}", entries.front().file, result == WatchResult::kStarted ? "starting" : "refused");
  });
}

}  // namespace gw2::demo

// The frame's pad poll (r3 the pad array): on playback the slots are replaced once it has filled them, and a watch
// asked for starts here, on the game thread.
REX_EXTERN(__imp__sub_82038300);
REX_HOOK_RAW(sub_82038300) {
  __imp__sub_82038300(ctx, base);
  gw2::demo::AfterPadPoll(base);
  gw2::demo::StartRequested(ctx, base);
  gw2::custom_modes::StartRequested(ctx, base);
  gw2::custom_modes::EachFrame(base);
  gw2::benchmark::EachFrame(base);
  gw2::master_page::EachFrame(ctx, base);
}

// XUserWriteAchievements (count, achievements, overlapped): a replay or a custom mode unlocks nothing. With no achievements the request
// still completes as usual.
REX_EXTERN(__imp__sub_820954A0);
REX_HOOK_RAW(sub_820954A0) {
  if (!gw2::boards::KeepsProgress(gw2::demo::HowRunCounts())) ctx.r3.u64 = 0;
  __imp__sub_820954A0(ctx, base);
}

// XamUserWriteProfileSettings (user, count, settings, overlapped): a replay or a custom mode saves no progress. The game's settings are
// swapped for a copy marked unset, which ReXGlue skips and still answers as saved.
REX_EXTERN(__imp__sub_820E8A20);
REX_HOOK_RAW(sub_820E8A20) {
  constexpr uint32_t kSettingSize = 40;  // X_USER_PROFILE_SETTING
  constexpr uint32_t kTypeOffset = 24;   // its data's type byte
  constexpr uint32_t kMaxSettings = 8;
  static uint32_t scratch = 0;
  const uint32_t count = ctx.r4.u32;
  if (!gw2::boards::KeepsProgress(gw2::demo::HowRunCounts()) && count > 0 && count <= kMaxSettings && ctx.r5.u32 != 0) {
    if (scratch == 0) scratch = rex::system::kernel_memory()->SystemHeapAlloc(kSettingSize * kMaxSettings);
    if (scratch != 0) {
      std::memcpy(base + scratch, base + ctx.r5.u32, kSettingSize * count);
      for (uint32_t i = 0; i < count; ++i) base[scratch + i * kSettingSize + kTypeOffset] = 0;
      ctx.r5.u64 = scratch;
      REXLOG_INFO("Replays: a profile save during a replay or custom mode was skipped");
    }
  }
  __imp__sub_820E8A20(ctx, base);
}
