// Replay library tests: what is kept (the newest ten, each mode's best, the starred), the file names, the favourites
// file refusing paths, and scanning, pruning and starring in a real folder.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "demo_file.h"
#include "demo_library.h"

using namespace gw2::demo_library;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

bool Has(const std::vector<std::string>& list, const std::string& name) {
  return std::find(list.begin(), list.end(), name) != list.end();
}

void TestToPrune() {
  std::vector<Entry> entries;
  // Twenty Deadline runs, a minute apart, scoring 100 to 2000; the oldest is the best.
  for (int i = 0; i < 20; ++i) {
    entries.push_back({"d" + std::to_string(i), 0, uint64_t(i == 0 ? 99999 : 100 * (i + 1)), 1000 + 60 * i, 0, false});
  }
  entries[3].starred = true;
  // One old Pacifism run: the only one of its mode, so its best.
  entries.push_back({"p0", 4, 50, 10, 0, false});
  const auto prune = ToPrune(entries);
  CHECK(!Has(prune, "d19") && !Has(prune, "d10"));  // the newest ten
  CHECK(Has(prune, "d9") && Has(prune, "d1"));
  CHECK(!Has(prune, "d0"));  // Deadline's best
  CHECK(!Has(prune, "d3"));  // starred
  CHECK(!Has(prune, "p0"));  // Pacifism's best
  CHECK(prune.size() == 21 - 10 - 3);
  // Fewer than ten: nothing goes.
  CHECK(ToPrune({{"a", 0, 1, 1, 0, false}, {"b", 1, 1, 2, 0, false}}).empty());
  CHECK(ToPrune({}).empty());
}

void TestNames() {
  // 2026-10-23 05:30:12 UTC
  CHECK(FileName(4, 72826275, 1792733412) == "20261023-053012-pacifism-72826275.gw2demo");
  CHECK(FileName(0, 0, 0) == "19700101-000000-deadline-0.gw2demo");
  CHECK(ModeSlug(9) == "unknown");
}

void TestFavourites() {
  const auto parsed = ParseFavourites("a.gw2demo\r\nb.gw2demo\n\n../evil.gw2demo\nC:\\x.gw2demo\nsub/x.gw2demo\nnotes.txt\n");
  CHECK(parsed.size() == 2 && parsed.count("a.gw2demo") && parsed.count("b.gw2demo"));
  CHECK(ParseFavourites(FormatFavourites(parsed)) == parsed);
  CHECK(ParseFavourites("").empty());
}

void WriteDemo(const std::filesystem::path& folder, uint32_t mode, uint64_t score, int64_t at) {
  gw2::demo_file::Demo demo;
  demo.mode = mode;
  demo.final_score = score;
  demo.recorded_at = at;
  const auto bytes = gw2::demo_file::Write(demo);
  std::ofstream out(folder / FileName(mode, score, at), std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

void TestFolder() {
  const std::filesystem::path folder = std::filesystem::temp_directory_path() / "gw2_demo_library_tests";
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder);
  for (int i = 0; i < 14; ++i) WriteDemo(folder, 0, 1000 + i, 1792733412 + i * 60);
  WriteDemo(folder, 4, 5, 1700000000);
  {
    std::ofstream junk(folder / "broken.gw2demo", std::ios::binary);
    junk << "not a demo";
  }
  auto entries = Scan(folder);
  CHECK(entries.size() == 15);  // the broken file is left out
  CHECK(!entries.empty() && entries.front().score == 1013);  // newest first
  const std::string oldest_deadline = FileName(0, 1000, 1792733412);
  CHECK(SetStarred(folder, oldest_deadline, true));
  entries = Scan(folder);
  bool starred = false;
  for (const Entry& e : entries) starred |= e.file == oldest_deadline && e.starred;
  CHECK(starred);
  // 15 replays: the newest ten, the starred oldest, Deadline's best (among the newest) and Pacifism's best stay.
  CHECK(Prune(folder) == 3);
  entries = Scan(folder);
  CHECK(entries.size() == 12);
  CHECK(std::filesystem::exists(folder / oldest_deadline));
  CHECK(std::filesystem::exists(folder / FileName(4, 5, 1700000000)));
  CHECK(std::filesystem::exists(folder / "broken.gw2demo"));  // never deleted: not ours to judge
  CHECK(SetStarred(folder, oldest_deadline, false));
  CHECK(Prune(folder) == 1);
  std::filesystem::remove_all(folder);
}


// The replay to share for a board's best: same mode, same custom mode (none for the game's own), same score.
void TestFindRun() {
  std::vector<Entry> entries(4);
  entries[0].file = "a"; entries[0].mode = 0; entries[0].score = 500; entries[0].recorded_at = 10;
  entries[1].file = "b"; entries[1].mode = 0; entries[1].score = 500; entries[1].recorded_at = 20;
  entries[2].file = "c"; entries[2].mode = 0; entries[2].score = 700; entries[2].custom_name = "Master Deadline";
  entries[3].file = "d"; entries[3].mode = 1; entries[3].score = 700;
  CHECK(FindRun(entries, 0, "", 500) == "b");  // the newest of two
  CHECK(FindRun(entries, 0, "Master Deadline", 700) == "c");
  CHECK(!FindRun(entries, 0, "", 700));  // a Master run is never Deadline's
  CHECK(!FindRun(entries, 0, "Master Deadline", 500));
  CHECK(FindRun(entries, 1, "", 700) == "d");
  CHECK(!FindRun(entries, 2, "", 500));
  CHECK(!FindRun({}, 0, "", 500));
}

}  // namespace

int main() {
  TestFindRun();
  TestToPrune();
  TestNames();
  TestFavourites();
  TestFolder();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
