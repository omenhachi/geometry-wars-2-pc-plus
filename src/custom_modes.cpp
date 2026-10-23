// Custom modes: the library, the mode being played and the hooks that give it its rules (custom_modes.h).
//
// The rules reach the game in three places:
//  - each mode's setup function (0x82034EA0..0x82035130) writes its settings into one block: starting lives (+15860)
//    and bombs (+15864), which the round start (sub_82032FF8) copies to each player, and the time limit (+15848). A
//    custom mode's values are written straight after the setup runs. Not every setup writes every field (only
//    Deadline's writes a time limit), so before any setup runs, whatever a custom mode changed is put back: an
//    official round always finds the block as the game left it;
//  - the enemy spawner (sub_8203BEE8, one switch over every type, from every mode's spawning): type 16 is the gate
//    pair, and without gates it is skipped. Its callers ignore the enemy it returns, and it returns none when it can't
//    allocate one, so none is safe;
//  - each simulation tick: the game step (0x8219D280) is scaled for the tick and put back after it, before the replay
//    records it, so a replay keeps the scaled steps; and a time score is written to the player's score after it.

#include "custom_modes.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>

#include "demo.h"
#include "guest_memory.h"
#include "player_records.h"

REXCVAR_DEFINE_BOOL(gw2_custom_mode_test_keys, false, "Geometry Wars", "F8 plays the first custom mode (for testing)");

REX_EXTERN(sub_8208FFF0);  // switches the screen (here to the in-game HUD, sub_8207CA48)
REX_EXTERN(sub_82032F90);  // queues the function that sets up the next round
REX_EXTERN(sub_820334F8);  // asks for a round to start, for the pad in r3

namespace gw2::custom_modes {
namespace {

namespace rules = gw2::custom_mode;

constexpr uint32_t kSettings = 0x82199470;  // the block every mode's setup writes
constexpr uint32_t kTimeLimitOffset = 15848;
constexpr uint32_t kModeOffset = 15844;
constexpr uint32_t kLivesOffset = 15860;
constexpr uint32_t kBombsOffset = 15864;
constexpr uint32_t kStepAddress = 0x8219D280;
constexpr uint32_t kStartState = 0x821993F0;  // 0 or 3 in the menus, 1 as a round begins, 2 in play, 4 at Game Over
constexpr uint32_t kPlaying = 2;
constexpr uint32_t kPlayingPad = 0x821993E0 + 12;
constexpr uint32_t kHudScreen = 0x8207CA48;
constexpr uint32_t kModeSetups[] = {0x82034EA0, 0x82034F38, 0x82034FA8, 0x82035030, 0x820350B0, 0x82035130};
constexpr uint32_t kScoreOffset = 40;  // the player record's score (u64)
constexpr char kBestsFile[] = "bests.txt";
// The game's 48-byte profile per user (sub_82093600), as saved in setting 0x63E83FFF: two volumes, then from +16 the
// mask of unlocked modes and each mode's unlock progress.
constexpr uint32_t kProfiles = 0x821F47B8;
constexpr uint32_t kProfileSize = 48;
constexpr uint32_t kProfileUsers = 4;
constexpr uint32_t kProgressOffset = 16;
constexpr uint32_t kProgressSize = 32;

struct Touched {
  uint32_t address;
  uint32_t before;
};

struct State {
  std::filesystem::path built_in;
  std::filesystem::path user;
  std::optional<rules::Mode> requested;
  std::optional<Current> active;
  std::vector<Touched> touched;    // fields a custom mode changed, with what they held
  std::optional<uint32_t> step;    // the step as it was before this tick's scaling
  // Every user's unlocks and unlock progress as they were before play that doesn't count began.
  std::optional<std::array<uint8_t, kProfileUsers * kProgressSize>> progress;
};

std::mutex g_mutex;
State g_state;
std::atomic<bool> g_active{false};
std::atomic<bool> g_no_gates{false};
std::atomic<bool> g_infinite_bombs{false};
std::atomic<int64_t> g_last_tick_ms{0};

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void SetActive(std::optional<Current> active) {
  g_state.active = std::move(active);
  const bool on = g_state.active.has_value();
  g_active.store(on, std::memory_order_relaxed);
  g_no_gates.store(on && !g_state.active->mode.rules.gates, std::memory_order_relaxed);
  g_infinite_bombs.store(on && g_state.active->mode.rules.infinite_bombs, std::memory_order_relaxed);
}

void Touch(uint8_t* base, uint32_t address, uint32_t value) {
  const bool seen = std::any_of(g_state.touched.begin(), g_state.touched.end(),
                                [&](const Touched& t) { return t.address == address; });
  if (!seen) g_state.touched.push_back({address, LoadBE32(base, address)});
  StoreBE32(base, address, value);
}

void PutBack(uint8_t* base) {
  for (auto it = g_state.touched.rbegin(); it != g_state.touched.rend(); ++it) StoreBE32(base, it->address, it->before);
  g_state.touched.clear();
}

std::map<std::string, uint64_t> ReadBests(const std::filesystem::path& folder) {
  std::map<std::string, uint64_t> bests;
  std::ifstream in(folder / kBestsFile);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    std::string id;
    uint64_t score = 0;
    if (fields >> id >> score && id.size() == 16) bests[id] = std::max(bests[id], score);
  }
  return bests;
}

void WriteBests(const std::filesystem::path& folder, const std::map<std::string, uint64_t>& bests) {
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  const std::filesystem::path temp = folder / (std::string(kBestsFile) + ".tmp");
  {
    std::ofstream out(temp, std::ios::trunc);
    for (const auto& [id, score] : bests) out << id << ' ' << score << '\n';
    if (!out) {
      REXLOG_WARN("Custom modes: could not write {}", temp.string());
      return;
    }
  }
  std::filesystem::rename(temp, folder / kBestsFile, error);
  if (error) REXLOG_WARN("Custom modes: could not save the bests: {}", error.message());
}

void ScanFolder(const std::filesystem::path& root, bool built_in, const std::map<std::string, uint64_t>& bests,
                std::vector<Entry>& out) {
  std::error_code error;
  if (root.empty() || !std::filesystem::is_directory(root, error)) return;
  std::vector<std::filesystem::path> folders;
  for (const auto& item : std::filesystem::directory_iterator(root, error)) {
    if (!item.is_directory(error)) continue;
    const std::string name = item.path().filename().string();
    if (name.empty() || name[0] == '_' || name[0] == '.') continue;  // _template and the like
    if (std::filesystem::is_regular_file(item.path() / kModeFile, error)) folders.push_back(item.path());
  }
  std::sort(folders.begin(), folders.end());
  constexpr uintmax_t kMaxModeFile = 64 * 1024;
  for (const auto& folder : folders) {
    Entry entry;
    entry.folder = folder.filename().string();
    entry.built_in = built_in;
    const std::filesystem::path file = folder / kModeFile;
    const uintmax_t size = std::filesystem::file_size(file, error);
    std::string text;
    if (!error && size <= kMaxModeFile) {
      std::ifstream in(file, std::ios::binary);
      text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (error || size > kMaxModeFile) {
      entry.errors.push_back({0, "mode.toml can't be read"});
    } else {
      rules::ParseResult parsed = rules::Parse(text);
      entry.errors = std::move(parsed.errors);
      entry.warnings = std::move(parsed.warnings);
      entry.newer_format = parsed.newer_format;
      if (parsed.mode) {
        entry.id = rules::Id(parsed.mode->rules);
        if (const auto best = bests.find(entry.id); best != bests.end()) entry.best = best->second;
        entry.mode = std::move(parsed.mode);
      }
    }
    out.push_back(std::move(entry));
  }
}

}  // namespace

namespace detail {

constexpr uint32_t kGatePair = 16;  // sub_8203BEE8's type for a pair of gates (two of enemy type 24)

// After a mode's setup has written its settings: the custom mode's own, for its base mode only.
void AfterSetup(uint32_t mode, uint8_t* base) {
  std::lock_guard lock(g_mutex);
  if (!g_state.active) return;
  const rules::Rules& r = g_state.active->mode.rules;
  if (r.base != mode) return;
  if (r.lives) Touch(base, kSettings + kLivesOffset, uint32_t(*r.lives));
  if (r.bombs) Touch(base, kSettings + kBombsOffset, uint32_t(*r.bombs));
  if (r.time_limit) Touch(base, kSettings + kTimeLimitOffset, std::bit_cast<uint32_t>(*r.time_limit));
  REXLOG_INFO("Custom modes: \"{}\" set up on mode {}", g_state.active->mode.name, mode);
}

void BeforeSetup(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  PutBack(base);
}

bool NoGates() {
  return g_no_gates.load(std::memory_order_relaxed);
}

void EndForSinglePlayer() {
  std::lock_guard lock(g_mutex);
  if (g_state.active) REXLOG_INFO("Custom modes: \"{}\" ends; the Single Player screen starts a round", g_state.active->mode.name);
  SetActive(std::nullopt);
}

}  // namespace detail

void SetFolders(std::filesystem::path built_in, std::filesystem::path user) {
  std::lock_guard lock(g_mutex);
  g_state.built_in = std::move(built_in);
  g_state.user = std::move(user);
}

std::filesystem::path UserFolder() {
  std::lock_guard lock(g_mutex);
  return g_state.user;
}

std::vector<Entry> Scan() {
  std::filesystem::path built_in, user;
  {
    std::lock_guard lock(g_mutex);
    built_in = g_state.built_in;
    user = g_state.user;
  }
  const auto bests = ReadBests(user);
  std::vector<Entry> entries;
  ScanFolder(user, false, bests, entries);
  ScanFolder(built_in, true, bests, entries);
  return entries;
}

void Play(const rules::Mode& mode) {
  std::lock_guard lock(g_mutex);
  g_state.requested = mode;
}

std::optional<Current> Active() {
  std::lock_guard lock(g_mutex);
  return g_state.active;
}

bool IsActive() {
  return g_active.load(std::memory_order_relaxed);
}

bool InfiniteBombs() {
  return g_infinite_bombs.load(std::memory_order_relaxed);
}

bool Ticking() {
  return NowMs() - g_last_tick_ms.load(std::memory_order_relaxed) < 250;
}

void ForReplay(std::optional<rules::Mode> mode) {
  std::lock_guard lock(g_mutex);
  g_state.requested.reset();
  if (!mode) {
    SetActive(std::nullopt);
    return;
  }
  Current current;
  current.id = rules::Id(mode->rules);
  current.mode = std::move(*mode);
  current.replay = true;
  SetActive(std::move(current));
}

std::string ScoreText(const rules::Rules& r, uint64_t score) {
  if (r.score == rules::Score::kTime) return rules::TimeText(score);
  std::string digits = std::to_string(score);
  for (int i = int(digits.size()) - 3; i > 0; i -= 3) digits.insert(size_t(i), ",");
  return digits;
}

void StartRequested(PPCContext& ctx, uint8_t* base) {
  uint32_t mode = 0;
  {
    std::lock_guard lock(g_mutex);
    if (!g_state.requested) return;
    // Only from the menus: a round in play or ending is left alone, and the request waits.
    const uint32_t start = LoadBE32(base, kStartState);
    if (start != 0 && start != 3) return;
    Current current;
    current.id = rules::Id(g_state.requested->rules);
    current.mode = std::move(*g_state.requested);
    g_state.requested.reset();
    mode = current.mode.rules.base;
    REXLOG_INFO("Custom modes: playing \"{}\" ({}), on mode {}", current.mode.name, current.id, mode);
    SetActive(std::move(current));
  }
  uint32_t pad = LoadBE32(base, kPlayingPad);
  if (pad > 3) pad = 0;
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
}

void EachFrame(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  // Back in the menus after its round, and once its score has been sent (and kept from the boards), the custom mode is
  // over. The Game Over screen is already a menu here, and the score is sent from it, so the send is waited for; a
  // round left without one keeps the mode until another starts. A replay's mode ends with the replay (demo.cpp).
  const uint32_t start = LoadBE32(base, kStartState);
  if (g_state.active && !g_state.active->replay && g_state.active->score_written && (start == 0 || start == 3)) {
    REXLOG_INFO("Custom modes: \"{}\" ends; back in the menus", g_state.active->mode.name);
    SetActive(std::nullopt);
  }
  const bool counts = demo::CountsOfficially();
  if (!counts && !g_state.progress) {
    auto& saved = g_state.progress.emplace();
    for (uint32_t user = 0; user < kProfileUsers; ++user) {
      std::memcpy(saved.data() + user * kProgressSize, base + kProfiles + user * kProfileSize + kProgressOffset,
                  kProgressSize);
    }
    return;
  }
  if (!g_state.progress) return;
  bool changed = false;
  for (uint32_t user = 0; user < kProfileUsers; ++user) {
    uint8_t* live = base + kProfiles + user * kProfileSize + kProgressOffset;
    const uint8_t* saved = g_state.progress->data() + user * kProgressSize;
    if (std::memcmp(live, saved, kProgressSize) != 0) {
      std::memcpy(live, saved, kProgressSize);
      changed = true;
    }
  }
  if (changed) REXLOG_INFO("Custom modes: unlock progress from play that doesn't count was put back");
  if (counts) g_state.progress.reset();
}

void BeforeSimTick(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  g_state.step.reset();
  if (!g_state.active || g_state.active->mode.rules.game_speed == 1.0f) return;
  const uint32_t bits = LoadBE32(base, kStepAddress);
  g_state.step = bits;
  StoreBE32(base, kStepAddress, std::bit_cast<uint32_t>(std::bit_cast<float>(bits) * g_state.active->mode.rules.game_speed));
}

void AfterSimTick(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  if (!g_state.active) return;
  Current& active = *g_state.active;
  const float step = std::bit_cast<float>(LoadBE32(base, kStepAddress));
  if (g_state.step) StoreBE32(base, kStepAddress, *g_state.step);
  g_state.step.reset();
  if (LoadBE32(base, kStartState) == kPlaying) g_last_tick_ms.store(NowMs(), std::memory_order_relaxed);
  if (active.run_over || LoadBE32(base, kStartState) != kPlaying) return;
  if (std::isfinite(step) && step > 0.0f) active.elapsed += step;
  if (active.mode.rules.score == rules::Score::kTime) {
    StoreBE64(base, players::kFirstRecordAddress + kScoreOffset, uint64_t(std::floor(active.elapsed * 100.0)));
  }
}

void OnRoundStart(uint8_t* base) {
  std::lock_guard lock(g_mutex);
  if (!g_state.active) return;
  g_state.active->elapsed = 0;
  g_state.active->run_over = false;
  g_state.active->score_written = false;
  if (LoadBE32(base, players::kPlayerCountAddress) != 1) {
    SetActive(std::nullopt);  // custom modes are single-player
    return;
  }
  ++g_state.active->round;
}

void OnRunEnd(const uint8_t* base) {
  std::string id, name;
  std::filesystem::path folder;
  uint64_t score = 0;
  {
    std::lock_guard lock(g_mutex);
    if (!g_state.active) return;
    g_state.active->run_over = true;
    if (g_state.active->replay || demo::Watching()) return;
    id = g_state.active->id;
    name = g_state.active->mode.name;
    folder = g_state.user;
    score = LoadBE64(base, players::kFirstRecordAddress + kScoreOffset);
  }
  if (folder.empty() || score == 0) return;
  auto bests = ReadBests(folder);
  if (bests[id] >= score) return;
  bests[id] = score;
  WriteBests(folder, bests);
  REXLOG_INFO("Custom modes: a new best in \"{}\": {}", name, score);
}

void OnScoreWritten() {
  std::lock_guard lock(g_mutex);
  if (g_state.active) g_state.active->score_written = true;
}

void RegisterKeys() {
  rex::ui::RegisterBind("bind_gw2_custom_mode_play_first", "F8", "Play the first custom mode (test)", [] {
    if (!REXCVAR_GET(gw2_custom_mode_test_keys)) return;
    for (const Entry& entry : Scan()) {
      if (!entry.mode) continue;
      REXLOG_INFO("Custom modes: test key plays {}", entry.folder);
      Play(*entry.mode);
      return;
    }
    REXLOG_INFO("Custom modes: no playable mode found");
  });
}

}  // namespace gw2::custom_modes

// The six modes' setup functions, queued by sub_82032F90 and run as the round is prepared.
#define GW2_CUSTOM_MODE_SETUP(address, mode)              REX_EXTERN(__imp__sub_##address);                       REX_HOOK_RAW(sub_##address) {                             gw2::custom_modes::detail::BeforeSetup(base);           __imp__sub_##address(ctx, base);                        gw2::custom_modes::detail::AfterSetup(mode, base);    }
GW2_CUSTOM_MODE_SETUP(82034EA0, 0)
GW2_CUSTOM_MODE_SETUP(82034F38, 1)
GW2_CUSTOM_MODE_SETUP(82034FA8, 2)
GW2_CUSTOM_MODE_SETUP(82035030, 3)
GW2_CUSTOM_MODE_SETUP(820350B0, 4)
GW2_CUSTOM_MODE_SETUP(82035130, 5)
#undef GW2_CUSTOM_MODE_SETUP

// Spawns an enemy of type r3 (1-25) and returns it, or 0 when none could be made; callers ignore the result.
REX_EXTERN(__imp__sub_8203BEE8);
REX_HOOK_RAW(sub_8203BEE8) {
  if (ctx.r3.u32 == gw2::custom_modes::detail::kGatePair && gw2::custom_modes::detail::NoGates()) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8203BEE8(ctx, base);
}

// The Single Player screen starts a mode: the round that follows is the game's own.
REX_EXTERN(__imp__sub_8208D410);
REX_HOOK_RAW(sub_8208D410) {
  gw2::custom_modes::detail::EndForSinglePlayer();
  __imp__sub_8208D410(ctx, base);
}
