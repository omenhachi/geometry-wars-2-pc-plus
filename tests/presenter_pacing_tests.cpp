// The Vulkan presenter with a UI drawer that asks for a repaint after every draw, as ImGuiDrawer does while the frame
// counter or another dialog exists. With 60 guest frames a second, each guest frame must be painted about once;
// without guest frames, the UI must keep repainting. Built against the stock SDK headers, like the game, so the same
// program runs against the stock runtime.
// Run (Linux, X11 and a Vulkan driver, e.g. Mesa's lavapipe under Xvfb):
//   SDL_VIDEO_DRIVER=x11 xvfb-run -a gw2_presenter_pacing_tests

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include <rex/logging/api.h>
#include <rex/ui/presenter.h>
#include <rex/ui/ui_drawer.h>
#include <rex/ui/vulkan/provider.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context_sdl.h>

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kWarmup = std::chrono::milliseconds(500);
constexpr int kGuestFrames = 180;
constexpr auto kGuestFramePeriod = std::chrono::microseconds(16667);
constexpr auto kUIOnly = std::chrono::seconds(1);

enum Phase { kWarmupPhase, kGuestPhase, kUIOnlyPhase, kDone, kPhaseCount };

std::atomic<int> g_phase{kWarmupPhase};
std::atomic<int> g_paints[kPhaseCount];
std::atomic<int> g_refreshes{0};

class RepaintingDrawer final : public rex::ui::UIDrawer {
 public:
  explicit RepaintingDrawer(rex::ui::Presenter& presenter) : presenter_(presenter) {}

  void Draw(rex::ui::UIDrawContext&) override {
    ++g_paints[g_phase.load()];
    presenter_.RequestUIPaintFromUIThread();
  }

 private:
  rex::ui::Presenter& presenter_;
};

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  rex::InitLoggingEarly();
  rex::ui::SDLWindowedAppContext app_context;
  if (!app_context.Initialize()) {
    std::printf("FAIL: no SDL video\n");
    return 2;
  }
  auto window = rex::ui::Window::Create(app_context, "gw2_presenter_pacing_tests", 640, 360);
  if (!window || !window->Open()) {
    std::printf("FAIL: no window\n");
    return 2;
  }
  auto provider = rex::ui::vulkan::VulkanProvider::Create(false, true);
  if (!provider) {
    std::printf("FAIL: no Vulkan device with presentation\n");
    return 2;
  }
  std::unique_ptr<rex::ui::Presenter> presenter = provider->CreatePresenter();
  if (!presenter) {
    std::printf("FAIL: no presenter\n");
    return 2;
  }
  window->SetPresenter(presenter.get());
  RepaintingDrawer drawer(*presenter);
  presenter->AddUIDrawerFromUIThread(&drawer, 0);

  // The guest's frames come from another thread, as from the GPU command processor.
  std::thread guest([&] {
    std::this_thread::sleep_for(kWarmup);
    g_phase = kGuestPhase;
    auto next = Clock::now();
    for (int i = 0; i < kGuestFrames; ++i) {
      presenter->RefreshGuestOutput(1280, 720, 1280, 720, [](rex::ui::Presenter::GuestOutputRefreshContext&) {
        ++g_refreshes;
        return true;
      });
      next += kGuestFramePeriod;
      std::this_thread::sleep_until(next);
    }
    g_phase = kUIOnlyPhase;
    std::this_thread::sleep_for(kUIOnly);
    g_phase = kDone;
    app_context.CallInUIThread([&app_context] { app_context.QuitFromUIThread(); });
  });
  app_context.RunMainMessageLoop();
  guest.join();

  presenter->RemoveUIDrawerFromUIThread(&drawer);
  window->SetPresenter(nullptr);
  presenter.reset();
  window.reset();
  provider.reset();

  const int refreshes = g_refreshes.load();
  const int guest_paints = g_paints[kGuestPhase].load();
  const int ui_only_paints = g_paints[kUIOnlyPhase].load();
  std::printf("guest frames %d, paints with them %d (%.2f per frame)\n", refreshes, guest_paints,
              refreshes ? double(guest_paints) / refreshes : 0.0);
  std::printf("paints in %lld ms without guest frames: %d\n",
              static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(kUIOnly).count()),
              ui_only_paints);
  int failures = 0;
  if (refreshes < kGuestFrames) {
    std::printf("FAIL: the guest output was not refreshed %d times\n", kGuestFrames);
    ++failures;
  }
  // A few paints for the surface connection and the transitions between the phases are allowed.
  if (guest_paints > refreshes + refreshes / 4 + 5) {
    std::printf("FAIL: guest frames were presented again for the UI\n");
    ++failures;
  }
  if (guest_paints < refreshes * 3 / 4) {
    std::printf("FAIL: guest frames were not painted\n");
    ++failures;
  }
  if (ui_only_paints < 10) {
    std::printf("FAIL: the UI stopped repainting without guest frames\n");
    ++failures;
  }
  std::printf("%s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
