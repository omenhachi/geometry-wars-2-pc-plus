#pragma once

#include <string_view>

namespace gw2::bloom {

enum class Mode { kOriginal, kReduced, kOff };

// Multiplies the game's bloom gains.
constexpr float Scale(Mode mode) {
  switch (mode) {
    case Mode::kOriginal:
      return 1.0f;
    case Mode::kReduced:
      return 0.5f;
    case Mode::kOff:
      return 0.0f;
  }
  return 1.0f;
}

constexpr std::string_view ModeName(Mode mode) {
  switch (mode) {
    case Mode::kOriginal:
      return "original";
    case Mode::kReduced:
      return "reduced";
    case Mode::kOff:
      return "off";
  }
  return "original";
}

// Anything that is not a known name keeps the original bloom.
constexpr Mode ParseMode(std::string_view value) {
  if (value == ModeName(Mode::kReduced)) {
    return Mode::kReduced;
  }
  if (value == ModeName(Mode::kOff)) {
    return Mode::kOff;
  }
  return Mode::kOriginal;
}

}  // namespace gw2::bloom
