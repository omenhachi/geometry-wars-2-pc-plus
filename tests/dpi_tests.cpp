// Per-monitor DPI helpers: scale derivation, logical/physical conversions and invalid-value fallbacks.
// Run: gw2_dpi_tests

#include <cmath>
#include <cstdio>

#include "dpi.h"

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

bool Near(float a, float b) {
  return std::fabs(a - b) < 0.0001f;
}

void TestScales() {
  std::printf("display scales\n");
  CHECK(Near(DpiScaleFromDpi(96), 1.0f));
  CHECK(Near(DpiScaleFromDpi(144), 1.5f));
  CHECK(Near(DpiScaleFromDpi(216), 2.25f));
}

void TestConversions() {
  std::printf("logical and physical conversions\n");
  CHECK(ScaleLogicalToPhysical(1280, 1.0f) == 1280);
  CHECK(ScaleLogicalToPhysical(1280, 1.5f) == 1920);
  CHECK(ScaleLogicalToPhysical(1280, 2.25f) == 2880);
  CHECK(ScalePhysicalToLogical(1280, 1.0f) == 1280);
  CHECK(ScalePhysicalToLogical(1920, 1.5f) == 1280);
  CHECK(ScalePhysicalToLogical(2880, 2.25f) == 1280);
}

void TestRepeatedConversions() {
  std::printf("repeated monitor moves do not drift\n");
  int size = 1280;
  for (int i = 0; i < 100; ++i) {
    size = ScalePhysicalToLogical(ScaleLogicalToPhysical(size, 2.25f), 2.25f);
    size = ScalePhysicalToLogical(ScaleLogicalToPhysical(size, 1.0f), 1.0f);
  }
  CHECK(size == 1280);

  size = 720;
  for (int i = 0; i < 100; ++i) {
    size = ScalePhysicalToLogical(ScaleLogicalToPhysical(size, 2.25f), 2.25f);
    size = ScalePhysicalToLogical(ScaleLogicalToPhysical(size, 1.5f), 1.5f);
  }
  CHECK(size == 720);
}

void TestInvalidScales() {
  std::printf("invalid DPI and scale fall back to 1\n");
  CHECK(Near(DpiScaleFromDpi(0), 1.0f));
  CHECK(Near(ValidDpiScale(0.0f), 1.0f));
  CHECK(Near(ValidDpiScale(-1.0f), 1.0f));
  CHECK(ScaleLogicalToPhysical(1280, 0.0f) == 1280);
  CHECK(ScalePhysicalToLogical(1280, -1.0f) == 1280);
}

}  // namespace

int main() {
  TestScales();
  TestConversions();
  TestRepeatedConversions();
  TestInvalidScales();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
