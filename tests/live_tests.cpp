// Tests of the local stats store and the guest-buffer helpers behind the offline Live services.
// Needs no game files and no ReXGlue; build and run it with tools\test.ps1.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "guest_memory.h"
#include "live_stats.h"
#include "local_stats.h"

namespace fs = std::filesystem;
using namespace gw2;
using namespace gw2::live;

static int g_failures = 0;
static void Check(bool ok, const std::string& what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++g_failures;
}

static std::string Slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void Spit(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << s;
}

constexpr uint64_t kXuid = 0xB13EBABEBABEBABEull;

static fs::path FreshRoot(const char* name) {
  fs::path root = fs::temp_directory_path() / "gw2_stats_test" / name;
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);
  return root;
}

static void TestLocalStats() {
  const fs::path root = FreshRoot("store");
  const fs::path file = StatsFilePath(root, 0x584108FF);

  Check(file == root / "584108FF" / "live" / "stats.toml", "stats path is <root>/584108FF/live/stats.toml");
  const auto rel = file.lexically_relative(root);
  Check(!rel.empty() && *rel.begin() != "..", "stats path stays under the user data root");

  {
    LocalStats s(file);
    Check(s.Load() == LoadResult::kMissing, "clean start: no stats file");
    Check(!s.Best(kXuid, 0).has_value(), "clean start: no score in any view");
    Check(!fs::exists(file), "loading does not create the file");

    Check(s.Record(kXuid, 0, 41020) == RecordResult::kSaved, "first score is saved");
    Check(fs::exists(file), "the file exists once a score was recorded");
    Check(s.Best(kXuid, 0) == 41020, "score reads back");
    Check(s.Record(kXuid, 0, 100) == RecordResult::kUnchanged, "a lower score does not replace the best");
    Check(s.Record(kXuid, 0, 41020) == RecordResult::kUnchanged, "an equal score changes nothing");
    Check(s.Best(kXuid, 0) == 41020, "best is still the higher one");
    Check(s.Record(kXuid, 0, 50000) == RecordResult::kSaved, "a higher score replaces the best");
    Check(s.Best(kXuid, 0) == 50000, "new best reads back");

    for (uint32_t v = 1; v < 6; ++v)
      Check(s.Record(kXuid, v, 1000 * (v + 1)) == RecordResult::kSaved, "view " + std::to_string(v) + " recorded");
    bool independent = s.Best(kXuid, 0) == 50000;
    for (uint32_t v = 1; v < 6; ++v) independent = independent && s.Best(kXuid, v) == int64_t(1000 * (v + 1));
    Check(independent, "the six views are independent");

    Check(s.Record(kXuid, 6, 0) == RecordResult::kUnchanged && !s.Best(kXuid, 6), "a score of zero is ignored");
    Check(s.Record(kXuid, 6, -5) == RecordResult::kUnchanged && !s.Best(kXuid, 6), "a negative score is ignored");
    Check(s.Record(kXuid, LocalStats::kMaxViews, 5) == RecordResult::kUnchanged, "an unknown view is ignored");
    Check(s.Record(0x1234, 0, 7) == RecordResult::kSaved && s.Best(0x1234, 0) == 7 && s.Best(kXuid, 0) == 50000,
          "another user has its own scores");
  }

  const std::string text = Slurp(file);
  Check(text.find("[users.B13EBABEBABEBABE]") != std::string::npos && text.find("view_0 = 50000") != std::string::npos,
        "the file is readable text with the user and view");
  Check(!fs::exists(file.string() + ".tmp"), "no temporary file is left behind");

  {
    LocalStats s(file);
    Check(s.Load() == LoadResult::kLoaded, "restart: the file loads");
    bool same = s.Best(kXuid, 0) == 50000 && s.Best(0x1234, 0) == 7;
    for (uint32_t v = 1; v < 6; ++v) same = same && s.Best(kXuid, v) == int64_t(1000 * (v + 1));
    Check(same, "restart: every score is back");
    Check(s.Record(kXuid, 0, 49000) == RecordResult::kUnchanged, "restart: a lower score still does not replace it");
  }

  fs::remove_all(root / "584108FF");
  {
    LocalStats s(file);
    Check(s.Load() == LoadResult::kMissing && !s.Best(kXuid, 0), "deleting the data gives a clean state again");
  }
}

static void TestCorruptAndEdited() {
  const fs::path root = FreshRoot("corrupt");
  const fs::path file = StatsFilePath(root, 0x584108FF);

  for (const char* bad :
       {"garbage that is not toml", "[users.XYZ]\nview_0 = 1\n", "[users.B13EBABEBABEBABE]\nview_0 = abc\n",
        "[users.B13EBABEBABEBABE]\nview_99 = 1\n", "version = 2\n", "view_0 = 5\n", "[users.B13EBABEBABEBABE\n"}) {
    Spit(file, bad);
    LocalStats s(file);
    const LoadResult r = s.Load();
    Check(r == LoadResult::kCorrupt && !s.Best(kXuid, 0), std::string("corrupt file is rejected: ") + bad);
    Check(!fs::exists(file) && fs::exists(file.string() + ".corrupt"), "the corrupt file is moved aside");
    Check(s.Record(kXuid, 1, 77) == RecordResult::kSaved && s.Best(kXuid, 1) == 77,
          "a fresh store works after a corrupt file");
    fs::remove(file.string() + ".corrupt");
    fs::remove(file);
  }

  Spit(file, std::string(2 << 20, 'x'));
  {
    LocalStats s(file);
    Check(s.Load() == LoadResult::kCorrupt, "an oversized file is rejected");
  }
  fs::remove(file.string() + ".corrupt");

  Spit(file,
       "# hand edited\r\nversion = 1\r\n\r\n[users.B13EBABEBABEBABE]\r\nview_0 = 10\r\nview_0 = 30\r\n  view_2 =   -4  "
       "\r\n");
  {
    LocalStats s(file);
    Check(s.Load() == LoadResult::kLoaded, "CRLF, comments and spaces are accepted");
    Check(s.Best(kXuid, 0) == 30, "a duplicate keeps the higher score");
    Check(s.Best(kXuid, 2) == -4, "values are parsed as signed numbers");
  }

  Spit(file, "version = 1\n\n[users.B13EBABEBABEBABE]\nview_3 = 123\n");
  Spit(file.string() + ".tmp", "half written garbage");
  {
    LocalStats s(file);
    Check(s.Load() == LoadResult::kLoaded && s.Best(kXuid, 3) == 123, "a leftover temporary file does not matter");
    Check(s.Record(kXuid, 3, 456) == RecordResult::kSaved, "the next save replaces the leftover");
    Check(!fs::exists(file.string() + ".tmp"), "the leftover temporary file is gone");
  }

  // A save that cannot create its temporary file leaves the existing file untouched.
  Spit(file, "version = 1\n\n[users.B13EBABEBABEBABE]\nview_0 = 900\n");
  fs::remove(file.string() + ".tmp");
  fs::create_directories(file.string() + ".tmp");
  {
    LocalStats s(file);
    Check(s.Load() == LoadResult::kLoaded, "existing file loads");
    Check(s.Record(kXuid, 0, 1000) == RecordResult::kNotSaved, "a failed save is reported");
    Check(s.Best(kXuid, 0) == 1000, "the new best still counts in memory");
    Check(Slurp(file).find("view_0 = 900") != std::string::npos, "the existing file keeps the old score");
  }
  fs::remove_all(file.string() + ".tmp");
}

static void TestConcurrency() {
  const fs::path root = FreshRoot("threads");
  LocalStats s(StatsFilePath(root, 0x584108FF));
  s.Load();
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&s, t] {
      for (int i = 1; i <= 50; ++i) s.Record(kXuid, uint32_t(i % 6), int64_t(t * 1000 + i));
    });
  }
  for (auto& t : threads) t.join();
  LocalStats again(StatsFilePath(root, 0x584108FF));
  again.Load();
  bool match = true;
  for (uint32_t v = 0; v < 6; ++v)
    match = match && again.Best(kXuid, v) == s.Best(kXuid, v) && s.Best(kXuid, v).has_value();
  Check(match, "concurrent writers end with the file matching memory");
}

// Guest memory stand-in: addresses are offsets into a buffer.
struct Mem {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(0x10000, 0xCD);
  uint8_t* base() { return bytes.data(); }
  uint32_t U32(uint32_t a) { return LoadBE32(base(), a); }
  uint64_t U64(uint32_t a) { return LoadBE64(base(), a); }
  void Put32(uint32_t a, uint32_t v) { StoreBE32(base(), a, v); }
  void Put64(uint32_t a, uint64_t v) { StoreBE64(base(), a, v); }
  void Fill(uint32_t a, uint32_t n, uint8_t v) { std::memset(base() + a, v, n); }
  void PutSpecs(uint32_t a, std::initializer_list<uint32_t> views) {
    uint32_t i = 0;
    Fill(a, kSpecSize * uint32_t(views.size()), 0);
    for (uint32_t v : views) Put32(a + kSpecSize * i++, v);
  }
};

static void TestLeaderboard() {
  const fs::path root = FreshRoot("board");
  LocalStats stats(StatsFilePath(root, 0x584108FF));
  stats.Load();
  LocalLeaderboard board(stats, {kXuid, "User"});

  auto empty = board.Ranked(0, 1, 10);
  Check(empty.id == 0 && empty.total_rows == 0 && empty.rows.empty(), "rank board without a score is empty");
  Check(board.Around(0, kXuid, 10).rows.empty(), "my-score board without a score is empty");
  auto selected = board.Selected(0, {kXuid});
  Check(selected.rows.size() == 1 && selected.rows[0].rank == 0 && selected.rows[0].rating == 0,
        "a stats read still answers for the local player, unranked");

  stats.Record(kXuid, 2, 777);
  auto ranked = board.Ranked(2, 1, 10);
  Check(ranked.total_rows == 1 && ranked.rows.size() == 1 && ranked.rows[0].rank == 1 && ranked.rows[0].rating == 777 &&
            ranked.rows[0].xuid == kXuid && ranked.rows[0].gamertag == "User",
        "rank board has the local player at rank 1");
  Check(board.Ranked(2, 2, 10).rows.empty() && board.Ranked(2, 2, 10).total_rows == 1,
        "rank board past rank 1 has no rows");
  Check(board.Ranked(2, 1, 0).rows.empty(), "rank board asked for no rows has none");
  Check(board.Ranked(1, 1, 10).rows.empty(), "other views stay empty");
  Check(board.Around(2, kXuid, 10).rows.size() == 1, "my-score board has the local player");
  Check(board.Around(2, 0xE000123456789ABCull, 10).rows.empty(),
        "my-score board for someone else is empty (no invented users)");
  auto friends = board.Selected(2, {0xE000123456789ABCull, kXuid, kXuid, 5});
  Check(friends.rows.size() == 1 && friends.rows[0].rating == 777 && friends.rows[0].rank == 1,
        "a stats read returns only the local player, once, with its score");
  Check(board.Selected(2, {0xE000123456789ABCull}).rows.empty(), "a stats read for strangers returns nothing");
}

static void TestResultsBuffer() {
  Mem m;
  std::vector<StatsView> views(2);
  views[0].id = 4;
  views[0].total_rows = 1;
  views[0].rows.push_back({kXuid, 1, 123456789012ll, "User"});
  views[1].id = 5;
  const uint32_t buf = 0x1000;

  Check(StatsResultsSize(views) == 8 + 2 * 16 + 48, "result size covers header, views and rows");
  Check(!WriteStatsResults(m.base(), buf, StatsResultsSize(views) - 1, views),
        "a buffer that is one byte short is refused");
  Check(m.U32(buf) == 0xCDCDCDCD, "a refused write leaves the buffer untouched");
  Check(!WriteStatsResults(m.base(), 0, 4096, views), "a missing buffer is refused");
  Check(WriteStatsResults(m.base(), buf, StatsResultsSize(views), views), "an exact buffer is accepted");

  Check(m.U32(buf) == 2 && m.U32(buf + 4) == buf + 8, "results: view count and view pointer");
  const uint32_t v0 = buf + 8, v1 = v0 + 16, row = buf + 8 + 32;
  Check(m.U32(v0) == 4 && m.U32(v0 + 4) == 1 && m.U32(v0 + 8) == 1 && m.U32(v0 + 12) == row,
        "view 0 fields and row pointer");
  Check(m.U32(v1) == 5 && m.U32(v1 + 4) == 0 && m.U32(v1 + 8) == 0 && m.U32(v1 + 12) == 0,
        "view 1 is empty with no row pointer");
  Check(m.U64(row) == kXuid && m.U32(row + 8) == 1 && m.U64(row + 16) == 123456789012ull, "row: xuid, rank, rating");
  Check(std::string(reinterpret_cast<char*>(m.base() + row + 24)) == "User" && m.base()[row + 28] == 0,
        "row: gamertag is NUL terminated");
  Check(m.U32(row + 40) == 0 && m.U32(row + 44) == 0, "row: no columns");

  views[0].rows[0].gamertag = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  WriteStatsResults(m.base(), buf, 4096, views);
  Check(m.base()[row + 24 + 15] == 0 && std::string(reinterpret_cast<char*>(m.base() + row + 24)) == "ABCDEFGHIJKLMNO",
        "a long gamertag is cut to 15 characters and terminated");
}

static void TestEnumerators() {
  Mem m;
  EnumeratorRegistry registry;
  m.PutSpecs(0x2000, {3, 4, 5});
  const uint32_t size_ptr = 0x3000, handle_ptr = 0x3004;
  uint32_t handle = 0;

  Check(CreateEnumerator(m.base(), registry, StatsQuery::Kind::kRanked, 1, 10, 0, 0x2000, size_ptr, handle_ptr) ==
            kErrorInvalidParameter,
        "create refuses zero specs");
  Check(CreateEnumerator(m.base(), registry, StatsQuery::Kind::kRanked, 1, 10, kMaxSpecs + 1, 0x2000, size_ptr,
                         handle_ptr) == kErrorInvalidParameter,
        "create refuses too many specs");
  Check(CreateEnumerator(m.base(), registry, StatsQuery::Kind::kRanked, 1, 10, 1, 0, size_ptr, handle_ptr) ==
            kErrorInvalidParameter,
        "create refuses missing specs");
  Check(CreateEnumerator(m.base(), registry, StatsQuery::Kind::kRanked, 1, 10, 1, 0x2000, 0, handle_ptr) ==
            kErrorInvalidParameter,
        "create refuses a missing size pointer");
  Check(CreateEnumerator(m.base(), registry, StatsQuery::Kind::kRanked, 1, 10, 1, 0x2000, size_ptr, 0) ==
            kErrorInvalidParameter,
        "create refuses a missing handle pointer");
  Check(registry.pending() == 0, "refused creates register nothing");

  Check(CreateEnumerator(m.base(), registry, StatsQuery::Kind::kRanked, 1, 10, 3, 0x2000, size_ptr, handle_ptr,
                         &handle) == kErrorSuccess,
        "create succeeds");
  Check(m.U32(size_ptr) == 8 + 64 * 3, "buffer size negotiation: header plus a view and a row per spec");
  Check(m.U32(handle_ptr) == handle && (handle & 0xFF000000) == EnumeratorRegistry::kHandleBase &&
            (handle & 0xFF000000) != 0xF8000000,
        "the handle is in the port's range, away from runtime handles");
  uint32_t second = 0;
  CreateEnumerator(m.base(), registry, StatsQuery::Kind::kAround, kXuid, 10, 1, 0x2000, size_ptr, handle_ptr, &second);
  Check(second != handle && registry.pending() == 2, "enumerator handles are distinct");

  Check(!registry.Take(0xF8000004) && !registry.Take(0xEC000999), "handles that are not ours are not claimed");
  auto query = registry.Take(handle);
  Check(query && query->views == std::vector<uint32_t>({3, 4, 5}) && query->kind == StatsQuery::Kind::kRanked &&
            query->pivot == 1,
        "the query remembers its views and pivot");
  Check(!registry.Take(handle) && registry.pending() == 1, "a served enumerator is retired");

  EnumeratorRegistry small;
  std::vector<uint32_t> handles;
  for (size_t i = 0; i < EnumeratorRegistry::kMaxPending + 10; ++i) handles.push_back(small.Add({}));
  Check(small.pending() == EnumeratorRegistry::kMaxPending, "unserved enumerators are capped");
  Check(!small.Take(handles.front()) && small.Take(handles.back()), "the oldest unserved enumerator is dropped first");
}

static void TestServe() {
  const fs::path root = FreshRoot("serve");
  LocalStats stats(StatsFilePath(root, 0x584108FF));
  stats.Load();
  stats.Record(kXuid, 1, 5000);
  LocalLeaderboard board(stats, {kXuid, "User"});
  Mem m;
  EnumeratorRegistry registry;
  m.PutSpecs(0x2000, {1});
  uint32_t handle = 0;
  CreateEnumerator(m.base(), registry, StatsQuery::Kind::kRanked, 1, 10, 1, 0x2000, 0x3000, 0x3004, &handle);
  const uint32_t cb = m.U32(0x3000);
  const StatsQuery query = *registry.Take(handle);

  const uint32_t buf = 0x4000, overlapped = 0x5000, items = 0x5100;
  m.Fill(overlapped, 32, 0);
  uint32_t r = ServeEnumeration(m.base(), board, query, buf, cb, 0, overlapped);
  Check(r == kErrorIoPending, "overlapped enumeration returns IO_PENDING");
  Check(m.U32(overlapped) == 0 && m.U32(overlapped + 4) == 1 && m.U32(overlapped + 0x18) == 0,
        "the overlapped carries success, one item and no extended error");
  Check(m.U32(buf) == 1 && m.U32(m.U32(buf + 4) + 8) == 1 && m.U64(m.U32(m.U32(buf + 4) + 12) + 16) == 5000,
        "the results hold the local score");

  m.Fill(overlapped, 32, 0xEE);
  r = ServeEnumeration(m.base(), board, query, buf, cb - 1, 0, overlapped);
  Check(r == kErrorIoPending && m.U32(overlapped) == kErrorInsufficientBuffer &&
            m.U32(overlapped + 0x18) == kErrorInsufficientBuffer && m.U32(overlapped + 4) == 0,
        "a short buffer completes the overlapped with INSUFFICIENT_BUFFER");

  r = ServeEnumeration(m.base(), board, query, 0, cb, items, 0);
  Check(r == kErrorInsufficientBuffer && m.U32(items) == 0, "no buffer, no overlapped: the status comes back directly");
  r = ServeEnumeration(m.base(), board, query, buf, cb, items, 0);
  Check(r == kErrorSuccess && m.U32(items) == 1, "synchronous enumeration reports its item count");

  StatsQuery empty_query;
  empty_query.views = {0, 2};
  m.Fill(overlapped, 32, 0);
  r = ServeEnumeration(m.base(), board, empty_query, buf, 8 + 64 * 2, 0, overlapped);
  Check(
      r == kErrorIoPending && m.U32(buf) == 2 && m.U32(m.U32(buf + 4) + 8) == 0 && m.U32(m.U32(buf + 4) + 16 + 8) == 0,
      "views without a score are empty but present");

  // XUserReadStats
  m.PutSpecs(0x6000, {0, 1, 2});
  m.Put64(0x6800, 0xE000AAAABBBBCCCCull);
  m.Put64(0x6808, kXuid);
  const uint32_t results = 0x7000;
  Check(!ServeReadStats(m.base(), board, 2, 0x6800, 3, 0x6000, 8 + 3 * 16 + 3 * 48 - 1, results),
        "a short stats buffer is refused");
  Check(ServeReadStats(m.base(), board, 2, 0x6800, 3, 0x6000, 8 + 3 * 16 + 3 * 48, results),
        "stats buffer of the right size is filled");
  const uint32_t views = m.U32(results + 4);
  Check(m.U32(results) == 3 && m.U32(views) == 0 && m.U32(views + 16) == 1 && m.U32(views + 32) == 2,
        "one view per requested spec, in order");
  const uint32_t row1 = m.U32(views + 16 + 12);
  Check(m.U32(views + 8) == 1 && m.U32(views + 16 + 8) == 1 && m.U64(row1 + 16) == 5000 && m.U32(row1 + 8) == 1,
        "view with a score: rank 1 and the score for the local player only");
  const uint32_t row0 = m.U32(views + 12);
  Check(m.U64(row0) == kXuid && m.U32(row0 + 8) == 0 && m.U64(row0 + 16) == 0, "view without a score: unranked, zero");
  Check(!ServeReadStats(m.base(), board, 2, 0x6800, 3, 0x6000, 4096, 0), "no results pointer: nothing is written");
  m.Fill(0x7800, 64, 0xAB);
  ServeReadStats(m.base(), board, 1, 0x6808, 3, 0x6000, 4096, 0x7800);
  Check(m.U32(0x7800) == 3, "a stats read with only the local player works");
  ServeReadStats(m.base(), board, 500, 0x6800, 3, 0x6000, 4096, 0x7900);
  Check(m.U32(0x7900) == 3, "an absurd xuid count is clamped, not trusted");

  // Players around the local one: after the requested rows, as many as the game's buffer holds.
  const NearbyRows nearby = [](uint32_t, int64_t mine) {
    Nearby around;
    for (int i = 1; i <= 5; ++i) around.rows.push_back({0x0009000000000100ull + i, uint32_t(6 - i), mine + 1000 * i, "Other"});
    around.my_rank = mine > 0 ? 6 : 0;
    return around;
  };
  const uint32_t full = 8 + 3 * 16 + 3 * 6 * 48;
  const uint32_t around = 0x8000;
  Check(ServeReadStats(m.base(), board, 1, 0x6808, 3, 0x6000, full, around, nearby),
        "room for five more per view: all of them are written");
  const uint32_t around_views = m.U32(around + 4);
  Check(m.U32(around_views + 8) == 6 && m.U32(around_views + 16 + 8) == 6 && m.U32(around_views + 32 + 8) == 6 &&
            m.U32(around_views + 4) == 6,
        "six rows per view, and the total says so");
  const uint32_t view1_rows = m.U32(around_views + 16 + 12);
  Check(m.U64(view1_rows) == kXuid && m.U64(view1_rows + 16) == 5000 && m.U64(view1_rows + 48 + 16) == 6000,
        "the requested row comes first, then the others, picked around the local score");
  Check(m.U32(view1_rows + 8) == 6 && m.U32(view1_rows + 48 + 8) == 5,
        "the local row takes its rank among the others, who keep theirs");
  Check(m.U32(m.U32(around_views + 12) + 8) == 0, "a view never played stays unranked");
  Check(m.U64(m.U32(around_views + 12) + 48 + 16) == 1000, "a view never played is picked around zero");

  Check(ServeReadStats(m.base(), board, 1, 0x6808, 3, 0x6000, full - 48, around, nearby),
        "one row short: still written");
  Check(m.U32(around_views + 8) == 6 && m.U32(around_views + 16 + 8) == 6 && m.U32(around_views + 32 + 8) == 5,
        "the last view gives up a nearby row");
  Check(ServeReadStats(m.base(), board, 1, 0x6808, 3, 0x6000, 8 + 3 * 16 + 3 * 48, around, nearby),
        "the size the game asked for before: only the requested rows");
  Check(m.U32(around_views + 8) == 1 && m.U32(around_views + 16 + 8) == 1 && m.U32(around_views + 32 + 8) == 1,
        "no nearby rows fit, none are written");
  Check(!ServeReadStats(m.base(), board, 1, 0x6808, 3, 0x6000, 8 + 3 * 16 + 3 * 48 - 1, around, nearby),
        "too short for the requested rows: refused as before");
}

static void TestScoreWrites() {
  Mem m;
  const uint32_t views = 0x2000, props = 0x2100;
  m.Put32(views, 4);
  m.Put32(views + 4, 1);
  m.Put32(views + 8, props);
  m.Fill(props, 24, 0);
  m.Put32(props, kScorePropertyId);
  m.base()[props + 8] = 2;
  m.Put64(props + 16, 123456);

  auto w = ParseScoreWrites(m.base(), views, 1);
  Check(w.size() == 1 && w[0].view == 4 && w[0].score == 123456, "a score write is parsed: view and 64-bit score");

  m.Put64(props + 16, uint64_t(int64_t(-9)));
  w = ParseScoreWrites(m.base(), views, 1);
  Check(w.size() == 1 && w[0].score == -9, "the score keeps its sign");

  m.Put32(props, 0x20000002);
  Check(ParseScoreWrites(m.base(), views, 1).empty(), "another property is not a score");
  m.Put32(props, kScorePropertyId);
  m.base()[props + 8] = 1;
  Check(ParseScoreWrites(m.base(), views, 1).empty(), "a score of the wrong type is ignored");
  m.base()[props + 8] = 2;
  Check(ParseScoreWrites(m.base(), 0, 1).empty() && ParseScoreWrites(m.base(), views, 17).empty(),
        "bad view pointers and counts are ignored");
  m.Put32(views + 8, 0);
  Check(ParseScoreWrites(m.base(), views, 1).empty(), "a view without properties is ignored");
  m.Put32(views + 8, props);
  m.Put32(views + 4, 99);
  Check(ParseScoreWrites(m.base(), views, 1).empty(), "an absurd property count is ignored");

  Mem two;
  const uint32_t a = 0x3000, pa = 0x3100, pb = 0x3200;
  for (uint32_t i = 0; i < 2; ++i) {
    const uint32_t p = i ? pb : pa;
    two.Fill(p, 24, 0);
    two.Put32(p, kScorePropertyId);
    two.base()[p + 8] = 2;
    two.Put64(p + 16, 100 + i);
    two.Put32(a + 12 * i, 10 + i);
    two.Put32(a + 12 * i + 4, 1);
    two.Put32(a + 12 * i + 8, p);
  }
  auto both = ParseScoreWrites(two.base(), a, 2);
  Check(both.size() == 2 && both[0].view == 10 && both[1].view == 11 && both[1].score == 101,
        "several views are parsed in order");
}

int main() {
  TestLocalStats();
  TestCorruptAndEdited();
  TestConcurrency();
  TestLeaderboard();
  TestResultsBuffer();
  TestEnumerators();
  TestServe();
  TestScoreWrites();
  std::error_code ec;
  fs::remove_all(fs::temp_directory_path() / "gw2_stats_test", ec);
  std::printf("%s: %d failure(s)\n", g_failures ? "LIVE TESTS FAILED" : "LIVE TESTS OK", g_failures);
  return g_failures ? 1 : 0;
}
