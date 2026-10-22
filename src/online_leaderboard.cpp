#include "online_leaderboard.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "aim_camera.h"
#include "cheats.h"
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

namespace gw2::online {
namespace {

// Long enough for a slow connection, short enough that the game's spinning globe never lingers.
constexpr int kTimeoutMs = 5000;
constexpr size_t kMaxResponseBytes = 64 * 1024;

std::mutex g_mutex;
std::filesystem::path g_token_file;  // guarded by g_mutex
std::string g_token;                 // guarded by g_mutex; created on first use
uint64_t g_local_xuid = 0;
std::atomic<bool> g_warned{false};

struct Response {
  int status = 0;  // 0: no answer at all
  std::string body;
};

// The release version without a suffix, as the server compares it.
std::string PortVersion() {
  std::string version = GW2_PORT_VERSION;
  return version.substr(0, version.find('-'));
}

#if defined(_WIN32)
std::wstring Widen(std::string_view text) {
  return std::wstring(text.begin(), text.end());  // the URLs and methods here are ASCII
}

Response Send(const char* method, const std::string& path, const std::string& body) {
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
      const wchar_t* headers = body.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : L"Content-Type: application/json\r\n";
      DWORD status = 0;
      DWORD status_size = sizeof(status);
      if (WinHttpSendRequest(request, headers, body.empty() ? 0 : DWORD(-1L),
                             body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                             DWORD(body.size()), DWORD(body.size()), 0) &&
          WinHttpReceiveResponse(request, nullptr) &&
          WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        response.status = int(status);
        for (DWORD available = 0; WinHttpQueryDataAvailable(request, &available) && available;) {
          if (response.body.size() + available > kMaxResponseBytes) break;
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
Response Send(const char*, const std::string&, const std::string&) { return {}; }
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

}  // namespace

std::optional<live::StatsView> ParseBoard(std::string_view json, uint32_t view, std::string_view my_id,
                                          uint64_t my_xuid) {
  const auto root = trg::update::json::Parse(json);
  if (!root || !root->IsObject()) return std::nullopt;
  const auto* total = root->Find("total");
  const auto* rows = root->Find("rows");
  if (!total || !total->AsInt64() || !rows || !rows->IsArray()) return std::nullopt;
  live::StatsView board;
  board.id = view;
  board.total_rows = uint32_t(std::max<int64_t>(0, *total->AsInt64()));
  for (const auto& row : rows->AsArray()) {
    const auto* rank = row.Find("rank");
    const auto* id = row.Find("id");
    const auto* name = row.Find("name");
    const auto* score = row.Find("score");
    if (!rank || !rank->AsInt64() || !id || !id->IsString() || !name || !name->IsString() || !score ||
        !score->AsInt64()) {
      return std::nullopt;
    }
    const uint64_t xuid = !my_id.empty() && id->AsString() == my_id ? my_xuid : XuidForPublicId(id->AsString());
    const std::string shown = ValidName(name->AsString());
    if (xuid == 0 || shown.empty() || board.rows.size() >= kMaxRows) return std::nullopt;
    board.rows.push_back({xuid, uint32_t(*rank->AsInt64()), *score->AsInt64(), shown});
  }
  return board;
}

void Configure(std::filesystem::path token_file, uint64_t local_xuid) {
  std::lock_guard lock(g_mutex);
  g_token_file = std::move(token_file);
  g_local_xuid = local_xuid;
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
  auto board = ParseBoard(response.body, view, PublicId(Token()), local_xuid);
  if (!board) WarnOnce("the server's board could not be read");
  return board;
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
