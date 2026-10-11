// Frame counter tests: the counter's text and when it is drawn. Pure logic only; the overlay and the F2 toggle need
// the running runtime.

#include <cstdio>
#include <string>

#include "frame_counter.h"

using namespace gw2;

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

void Section(const char* name) {
  std::printf("%s\n", name);
}

void TestText() {
  Section("frame counter text");
  CHECK(FrameCounterText({60.0, 1000.0 / 60.0, 60}) == "60 FPS  16.7 ms");
  CHECK(FrameCounterText({120.0, 1000.0 / 120.0, 120}) == "120 FPS  8.3 ms");
  CHECK(FrameCounterText({240.0, 1000.0 / 240.0, 240}) == "240 FPS  4.2 ms");
  CHECK(FrameCounterText({59.94, 16.6833, 60}) == "60 FPS  16.7 ms");
}

void TestVisibility() {
  Section("frame counter visibility");
  const GuestFrameStats measured{60.0, 1000.0 / 60.0, 120};
  const GuestFrameStats none{};
  CHECK(!FrameCounterVisible(false, measured));
  CHECK(!FrameCounterVisible(true, none));
  CHECK(FrameCounterVisible(true, measured));
}

}  // namespace

int main() {
  TestText();
  TestVisibility();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
