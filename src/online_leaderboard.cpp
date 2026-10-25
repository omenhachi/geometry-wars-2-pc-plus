#include "online_leaderboard.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/types.h>

#include "aim_camera.h"
#include "boards.h"
#include "cheats.h"
#include "custom_modes.h"
#include "demo.h"
#include "demo_library.h"
#include "launcher_cvars.h"
#include "launcher_settings.h"
#include "updater/json.h"
#include "updater/sha256.h"
#include "window_host.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#endif

// The SDK's XamUserGetName body, exported by rexruntime.dll. The SDK has no header for it.
namespace rex::kernel::xam {

u32 XamUserGetName_entry(u32 user_index, mapped_string buffer, u32 buffer_len);

}  // namespace rex::kernel::xam

namespace gw2::online {
namespace {

// Long enough for a slow connection, short enough that the game's spinning globe never lingers.
constexpr int kTimeoutMs = 5000;
constexpr size_t kMaxResponseBytes = 64 * 1024;

std::mutex g_mutex;
std::filesystem::path g_token_file;  // guarded by g_mutex
std::string g_token;                 // guarded by g_mutex; created on first use
uint64_t g_local_xuid = 0;
// Every view's board for the players around you, fetched in the background (under g_mutex).
constexpr uint32_t kViews = boards::kBoards;
std::array<std::optional<live::StatsView>, kViews> g_nearby;
std::atomic<bool> g_nearby_fetching{false};
// Each board's rows with a replay as last fetched, by the xuid the game shows them with: their public ids.
std::array<std::vector<std::pair<uint64_t, std::string>>, kViews> g_replay_rows;  // guarded by g_mutex
std::atomic<bool> g_nearby_fresh{false};  // fetched since this start

// Next to the token, in the player's user data.
std::filesystem::path NearbyFile() {
  return g_token_file.parent_path() / "nearby.txt";
}
std::atomic<bool> g_warned{false};

struct Response {
  int status = 0;  // 0: no answer at all
  std::string body;
};

// A request's body type and headers beyond the defaults, and how much of the answer is read.
struct SendOptions {
  std::wstring content_type = L"application/json";
  std::wstring headers;  // each "Name: value\r\n"
  size_t max_response = kMaxResponseBytes;
};

// The release version without a suffix, as the server compares it.
std::string PortVersion() {
  std::string version = GW2_PORT_VERSION;
  return version.substr(0, version.find('-'));
}

std::wstring Widen(std::string_view text) {
  return std::wstring(text.begin(), text.end());  // the URLs and methods here are ASCII
}

#if defined(_WIN32)
Response Send(const char* method, const std::string& path, const std::string& body, const SendOptions& options = {}) {
  Response response;
  const std::wstring url = Widen(std::string(kServerUrl) + path);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  wchar_t host[256] = {};
  wchar_t url_path[1024] = {};
  parts.lpszHostName = host;
  parts.dwHostNameLength = DWORD(std::size(host));
  parts.lpszUrlPath = url_path;
  parts.dwUrlPathLength = DWORD(std::size(url_path));
  wchar_t extra[1024] = {};
  parts.lpszExtraInfo = extra;
  parts.dwExtraInfoLength = DWORD(std::size(extra));
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return response;
  const std::wstring object = std::wstring(url_path) + extra;

  HINTERNET session = WinHttpOpen(L"GeometryWars2PCPlus", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return response;
  WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
  if (HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0)) {
    const std::wstring verb = Widen(method);
    if (HINTERNET request = WinHttpOpenRequest(connection, verb.c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) {
      const std::wstring headers =
          (body.empty() ? std::wstring() : L"Content-Type: " + options.content_type + L"\r\n") + options.headers;
      DWORD status = 0;
      DWORD status_size = sizeof(status);
      if (WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                             headers.empty() ? 0 : DWORD(-1L),
                             body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                             DWORD(body.size()), DWORD(body.size()), 0) &&
          WinHttpReceiveResponse(request, nullptr) &&
          WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        response.status = int(status);
        for (DWORD available = 0; WinHttpQueryDataAvailable(request, &available) && available;) {
          if (response.body.size() + available > options.max_response) {
            response.status = 0;  // cut short: no answer
            response.body.clear();
            break;
          }
          const size_t at = response.body.size();
          response.body.resize(at + available);
          DWORD read = 0;
          if (!WinHttpReadData(request, response.body.data() + at, available, &read)) break;
          response.body.resize(at + read);
        }
      }
      WinHttpCloseHandle(request);
    }
    WinHttpCloseHandle(connection);
  }
  WinHttpCloseHandle(session);
  return response;
}

bool RandomBytes(unsigned char* out, size_t size) {
  return BCryptGenRandom(nullptr, out, ULONG(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}
#else
// Not built for Linux yet: it would need its own tested client, so online boards stay local there.
Response Send(const char*, const std::string&, const std::string&, const SendOptions& = {}) { return {}; }
bool RandomBytes(unsigned char*, size_t) { return false; }
#endif

bool HasSavedToken() {
  std::lock_guard lock(g_mutex);
  std::error_code error;
  return !g_token.empty() || (!g_token_file.empty() && std::filesystem::exists(g_token_file, error));
}

// This PC's secret token: 64 hex digits, made once and kept in the token file. Empty if it cannot be made or kept.
std::string Token() {
  std::lock_guard lock(g_mutex);
  if (!g_token.empty() || g_token_file.empty()) return g_token;
  std::ifstream in(g_token_file);
  std::string saved;
  std::getline(in, saved);
  if (saved.size() == 64 && saved.find_first_not_of("0123456789abcdef") == std::string::npos) {
    g_token = saved;
    return g_token;
  }
  unsigned char bytes[32];
  if (!RandomBytes(bytes, sizeof(bytes))) return {};
  static constexpr char kHex[] = "0123456789abcdef";
  std::string token;
  for (const unsigned char b : bytes) {
    token += kHex[b >> 4];
    token += kHex[b & 15];
  }
  std::error_code error;
  std::filesystem::create_directories(g_token_file.parent_path(), error);
  std::ofstream out(g_token_file, std::ios::trunc);
  out << token << "\n";
  if (!out.flush()) {
    REXLOG_WARN("Online leaderboards: could not save the player token to {}", g_token_file.string());
    return {};
  }
  g_token = token;
  return g_token;
}

std::string PublicId(const std::string& token) {
  return token.empty() ? std::string() : trg::update::Sha256Hex(token).substr(0, 16);
}

std::string PlayerName() {
  return ValidName(rex::cvar::GetFlagByName(kPlayerNameKey));
}

// What the run was aimed with, so the boards can be split later: the mouse if it aimed recently, the keyboard if it
// was the last thing used, otherwise a controller.
const char* InputUsed() {
  if (aim::MouseAimedRecently()) return "mouse";
  if (PromptsUseKeyboard() && rex::cvar::GetFlagByName(kKeyboardKey) == "true") return "keyboard";
  return "controller";
}

void WarnOnce(const std::string& what) {
  if (!g_warned.exchange(true)) {
    REXLOG_WARN("Online leaderboards: {}; using the local boards (logged once)", what);
  }
}

// Kept beside the token once a replay has been shared: the notice is shown once, ever.
std::filesystem::path SharedNoticeFile() {
  return g_token_file.parent_path() / "replay-shared.txt";
}
std::atomic<bool> g_notice_pending{false};
std::atomic<bool> g_asked_for_wanted{false};

// Sends the saved replay of the player's best on `board` that scored `score`. The game saves a finished run's replay
// on its own thread as the round ends, about when the score goes, so for a run `just_played` it is looked for a few
// times.
void ShareReplay(uint32_t board, int64_t score, bool just_played) {
  const uint32_t mode = boards::BaseMode(board);
  const custom_modes::Master* master = board >= boards::kGameBoards ? custom_modes::MasterFor(mode) : nullptr;
  if (board >= boards::kGameBoards && !master) return;
  const std::string custom_name = master ? master->mode.name : std::string();
  const std::filesystem::path folder = demo::Folder();
  std::optional<std::string> file;
  for (int attempt = 0; attempt < (just_played ? 6 : 1) && !file; ++attempt) {
    if (attempt) std::this_thread::sleep_for(std::chrono::seconds(1));
    file = demo_library::FindRun(demo_library::Scan(folder), mode, custom_name, uint64_t(score));
  }
  if (!file) {
    REXLOG_INFO("Online replays: no saved replay of the {} on board {} to share", score, board);
    return;
  }
  std::error_code error;
  const uintmax_t size = std::filesystem::file_size(folder / *file, error);
  if (error || size == 0 || size > kMaxReplayBytes) return;
  std::string bytes(size_t(size), '\0');
  std::ifstream in(folder / *file, std::ios::binary);
  if (!in.read(bytes.data(), std::streamsize(bytes.size()))) return;
  SendOptions options;
  options.content_type = L"application/octet-stream";
  options.headers = L"Authorization: Bearer " + Widen(Token()) + L"\r\nX-GW2-Version: " + Widen(PortVersion()) + L"\r\n";
  const Response response = Send("POST", "/v1/replays/" + std::to_string(board), bytes, options);
  if (response.status != 200) {
    REXLOG_INFO("Online replays: the replay of {} on board {} was not kept (HTTP {})", score, board, response.status);
    return;
  }
  REXLOG_INFO("Online replays: shared the replay of {} on board {} ({} bytes)", score, board, bytes.size());
  std::lock_guard lock(g_mutex);
  if (!std::filesystem::exists(SharedNoticeFile(), error)) {
    std::ofstream(SharedNoticeFile()) << "The first replay was shared; the game said so once.\n";
    g_notice_pending = true;
  }
}

// Once a start: the bests the server would take a replay of, sent when this PC still has them.
void ShareWantedReplays() {
  if (!SharesReplays() || g_asked_for_wanted.exchange(true)) return;
  std::thread([] {
    const Response response = Send("POST", "/v1/replays/wanted", "{\"token\":\"" + Token() + "\"}");
    if (response.status != 200) return;
    const auto root = trg::update::json::Parse(response.body);
    const auto* wanted = root && root->IsObject() ? root->Find("wanted") : nullptr;
    if (!wanted || !wanted->IsArray()) return;
    for (const auto& item : wanted->AsArray()) {
      const auto* board = item.Find("mode");
      const auto* score = item.Find("score");
      if (!board || !board->AsInt64() || !score || !score->AsInt64()) continue;
      if (*board->AsInt64() < 0 || *board->AsInt64() >= int64_t(boards::kBoards)) continue;
      ShareReplay(uint32_t(*board->AsInt64()), *score->AsInt64(), false);
    }
  }).detach();
}

}  // namespace

void Configure(std::filesystem::path token_file, uint64_t local_xuid) {
  std::lock_guard lock(g_mutex);
  g_token_file = std::move(token_file);
  g_local_xuid = local_xuid;
  std::ifstream in(NearbyFile(), std::ios::binary);
  const std::string saved{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  for (live::StatsView& board : ParseNearby(saved)) {
    const uint32_t view = board.id;
    if (view < kViews) g_nearby[view] = std::move(board);
  }
}

bool Enabled() {
#if defined(_WIN32)
  return REXCVAR_GET(gw2_online_leaderboards) && !PlayerName().empty();
#else
  return false;
#endif
}

std::string Describe() {
#if !defined(_WIN32)
  return "Off (not available on this platform yet)";
#else
  if (!REXCVAR_GET(gw2_online_leaderboards)) return "Off";
  return PlayerName().empty() ? "On, but no valid player name, so scores stay local"
                              : "On as \"" + PlayerName() + "\" (" + std::string(kServerUrl) + ")";
#endif
}

void SubmitScore(uint32_t view, int64_t score) {
  if (score <= 0) return;
  if (!Enabled()) {
    REXLOG_INFO("Online leaderboards: score {} for view {} kept local ({})", score, view,
                !REXCVAR_GET(gw2_online_leaderboards) ? "the setting is off" : "no valid player name");
    return;
  }
  if (cheats::Enabled(cheats::Cheat::Invincible) || cheats::Enabled(cheats::Cheat::InfiniteShield) ||
      cheats::Enabled(cheats::Cheat::InfiniteBombs)) {
    REXLOG_INFO("Online leaderboards: a run with cheats on is kept on the local board only");
    return;
  }
  const std::string body = std::string("{\"token\":\"") + Token() + "\",\"name\":\"" + PlayerName() +
                           "\",\"mode\":" + std::to_string(view) + ",\"score\":" + std::to_string(score) +
                           ",\"input\":\"" + InputUsed() + "\",\"version\":\"" + PortVersion() + "\"}";
  std::thread([body, view, score] {
    const Response response = Send("POST", "/v1/scores", body);
    if (response.status == 200) {
      REXLOG_INFO("Online leaderboards: score {} sent for view {}", score, view);
      RefreshNearby();  // so the tiles show where the run landed
      // A new best in the top 50: the server asks for its replay.
      const auto root = trg::update::json::Parse(response.body);
      const auto* replay = root && root->IsObject() ? root->Find("replay") : nullptr;
      if (replay && replay->IsBool() && replay->AsBool() && SharesReplays()) ShareReplay(view, score, true);
    } else {
      WarnOnce(response.status == 0 ? "the server could not be reached"
                                    : "the server refused a score (HTTP " + std::to_string(response.status) + ")");
    }
  }).detach();
}

std::optional<live::StatsView> FetchBoard(uint32_t view, uint32_t first_rank, uint32_t count) {
  if (!Enabled()) return std::nullopt;
  count = std::clamp<uint32_t>(count, 1, kMaxRows);
  const Response response = Send("GET", "/v1/boards/" + std::to_string(view) + "?from=" +
                                            std::to_string(std::max<uint32_t>(1, first_rank)) +
                                            "&count=" + std::to_string(count), {});
  if (response.status != 200) {
    WarnOnce(response.status == 0 ? "the server could not be reached"
                                  : "the server answered HTTP " + std::to_string(response.status));
    return std::nullopt;
  }
  uint64_t local_xuid;
  {
    std::lock_guard lock(g_mutex);
    local_xuid = g_local_xuid;
  }
  const std::string my_id = PublicId(Token());
  auto board = ParseBoard(response.body, view, my_id, local_xuid);
  if (!board) {
    WarnOnce("the server's board could not be read");
    return board;
  }
  if (const auto runs = ParseTopRuns(response.body, my_id); runs && view < kViews) {
    std::lock_guard lock(g_mutex);
    auto& rows = g_replay_rows[view];
    for (const TopRun& run : *runs) {
      const uint64_t xuid = run.mine ? local_xuid : XuidForPublicId(run.id);
      std::erase_if(rows, [xuid](const auto& row) { return row.first == xuid; });
      if (!run.game.empty()) rows.emplace_back(xuid, run.id);
    }
  }
  return board;
}

std::optional<live::StatsView> NearbyBoard(uint32_t view) {
  if (view >= kViews || !Enabled()) return std::nullopt;
  if (!g_nearby_fresh) RefreshNearby();
  std::lock_guard lock(g_mutex);
  return g_nearby[view];
}

void RefreshNearby() {
  ShareWantedReplays();
  if (!Enabled() || g_nearby_fetching.exchange(true)) return;
  std::thread([] {
    // Every board is asked for, and the refresh counts once the game's own are in: a server without the Master
    // boards yet answers those with an error, which must not make every read ask again.
    uint32_t fetched = 0;
    bool game_boards = true;
    for (uint32_t view = 0; view < kViews; ++view) {
      auto board = FetchBoard(view, 1, kMaxRows);
      if (!board) {
        if (view < boards::kGameBoards) game_boards = false;
        continue;
      }
      std::lock_guard lock(g_mutex);
      g_nearby[view] = std::move(*board);
      ++fetched;
    }
    if (game_boards) {
      g_nearby_fresh = true;
      std::vector<live::StatsView> boards;
      std::filesystem::path file;
      {
        std::lock_guard lock(g_mutex);
        for (const auto& board : g_nearby) {
          if (board) boards.push_back(*board);
        }
        file = NearbyFile();
      }
      const std::string text = FormatNearby(boards);
      const std::filesystem::path staging = file.string() + ".tmp";
      std::error_code error;
      {
        std::ofstream out(staging, std::ios::binary | std::ios::trunc);
        out.write(text.data(), std::streamsize(text.size()));
      }
      std::filesystem::rename(staging, file, error);
      if (error) std::filesystem::remove(staging, error);
    }
    REXLOG_INFO("Online leaderboards: players around you {} ({} of {} boards)", game_boards ? "updated" : "not updated",
                fetched, kViews);
    g_nearby_fetching = false;
  }).detach();
}

bool SharesReplays() {
  return Enabled() && REXCVAR_GET(gw2_share_replays);
}

std::optional<std::vector<TopRun>> FetchTopRuns(uint32_t board) {
  if (!Enabled() || board >= boards::kBoards) return std::nullopt;
  const Response response =
      Send("GET", "/v1/boards/" + std::to_string(board) + "?from=1&count=" + std::to_string(kMaxRows), {});
  if (response.status != 200) return std::nullopt;
  return ParseTopRuns(response.body, PublicId(Token()));
}

std::optional<std::vector<uint8_t>> DownloadReplay(uint32_t board, const std::string& id) {
  if (!Enabled() || board >= boards::kBoards || XuidForPublicId(id) == 0) return std::nullopt;
  SendOptions options;
  options.max_response = kMaxReplayBytes;
  const Response response = Send("GET", "/v1/replays/" + std::to_string(board) + "/" + id, {}, options);
  if (response.status != 200 || response.body.empty()) return std::nullopt;
  return std::vector<uint8_t>(response.body.begin(), response.body.end());
}

std::optional<std::string> ReplayIdFor(uint32_t board, uint64_t xuid) {
  if (board >= kViews || xuid == 0) return std::nullopt;
  std::lock_guard lock(g_mutex);
  for (const auto& [shown, id] : g_replay_rows[board]) {
    if (shown == xuid) return id;
  }
  return std::nullopt;
}

bool TakeSharedNotice() {
  return g_notice_pending.exchange(false);
}

void DeleteMyScores(std::function<void(bool, std::string)> done) {
  std::thread([done = std::move(done)] {
    const std::string token = HasSavedToken() ? Token() : std::string();
    if (token.empty()) {
      done(true, "Nothing to delete: this PC has never sent a score.");
      return;
    }
    const Response response = Send("DELETE", "/v1/players", "{\"token\":\"" + token + "\"}");
    if (response.status == 200) {
      done(true, "Your online scores were deleted.");
    } else {
      done(false, response.status == 0 ? "The leaderboard server could not be reached. Try again later."
                                       : "The server could not delete the scores (HTTP " +
                                             std::to_string(response.status) + ").");
    }
  }).detach();
}

}  // namespace gw2::online

// Named like the SDK's export, so REX_EXTERN would call this hook again: the original is called by its entry point.
// The local profile is called "User"; with a player name set, the game shows that instead, in its corner and on the
// player's own leaderboard row.
REX_HOOK_RAW(__imp__XamUserGetName) {
  const uint32_t user_index = ctx.r3.u32;
  const uint32_t buffer = ctx.r4.u32;
  const uint32_t length = ctx.r5.u32;
  rex::ppc::HostToGuestFunction<rex::kernel::xam::XamUserGetName_entry>(ctx, base);
  if (ctx.r3.u32 != 0 || user_index != 0 || buffer == 0 || length == 0) {
    return;
  }
  const std::string name = gw2::online::ValidName(rex::cvar::GetFlagByName(gw2::kPlayerNameKey));
  if (name.empty()) {
    return;
  }
  // The SDK copies at most 16 bytes; a 15 character name and its terminator fit.
  const size_t size = std::min<size_t>(std::min<uint32_t>(length, 16), name.size() + 1);
  std::memcpy(base + buffer, name.data(), size - 1);
  base[buffer + size - 1] = 0;
}
