#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace gw2 {

constexpr uint16_t kXinputDpadUp = 0x0001;
constexpr uint16_t kXinputDpadDown = 0x0002;
constexpr uint16_t kXinputButtonB = 0x2000;
constexpr int kXinputThumbDeadzone = 8000;
constexpr int kXinputThumbMax = 32767;

constexpr float kOverlayViewportFraction = 0.7f;
constexpr float kOverlayViewportMargin = 16.0f;

// Window size along one axis: about 70% of the viewport, clamped to [min_size, max_size] and kept inside it.
inline float FitWindowExtent(float viewport, float min_size, float max_size) {
  const float wanted = std::clamp(viewport * kOverlayViewportFraction, min_size, max_size);
  return std::max(0.0f, std::min(wanted, viewport - 2.0f * kOverlayViewportMargin));
}

// Controller rules while the achievements overlay is up. Each guest XInputGetState read is one Poll.
class AchievementsOverlayGate {
 public:
  static constexpr uint32_t kUsers = 4;

  struct Verdict {
    bool hide_input = false;
    bool close = false;
    float scroll = 0.0f;  // -1 up .. 1 down
  };

  // A B still held when the overlay closes must not reach the game as a new press.
  void SetOpen(bool open) {
    swallow_b_ = !open && AnyBDown();
    open_ = open;
  }

  Verdict Poll(uint32_t user, uint16_t buttons, int thumb_y) {
    Verdict verdict;
    if (user < kUsers) {
      const bool b_down = (buttons & kXinputButtonB) != 0;
      verdict.close = open_ && b_down && !b_down_[user];
      b_down_[user] = b_down;
    }
    if (verdict.close) swallow_b_ = true;
    if (!open_ && !AnyBDown()) swallow_b_ = false;
    verdict.hide_input = open_ || swallow_b_;
    if (open_) verdict.scroll = ScrollFrom(buttons, thumb_y);
    return verdict;
  }

 private:
  bool AnyBDown() const {
    return std::any_of(b_down_.begin(), b_down_.end(), [](bool down) { return down; });
  }

  static float ScrollFrom(uint16_t buttons, int thumb_y) {
    if (buttons & kXinputDpadUp) return -1.0f;
    if (buttons & kXinputDpadDown) return 1.0f;
    const int magnitude = std::abs(thumb_y);
    if (magnitude <= kXinputThumbDeadzone) return 0.0f;
    const float amount = std::min(1.0f, static_cast<float>(magnitude - kXinputThumbDeadzone) /
                                            static_cast<float>(kXinputThumbMax - kXinputThumbDeadzone));
    return thumb_y > 0 ? -amount : amount;
  }

  bool open_ = false;
  bool swallow_b_ = false;
  std::array<bool, kUsers> b_down_{};
};

}  // namespace gw2
