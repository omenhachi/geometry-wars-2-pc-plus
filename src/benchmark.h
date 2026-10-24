#pragma once

// Benchmark (benchmark.cpp): --gw2_benchmark=<replay file> plays that replay as soon as the menus are up and times every
// frame, writing the results to benchmark.txt. The launcher's Run benchmark plays the replay that ships with the game
// for a minute, and the results show over the game, copied for sharing.

#include <cstdint>
#include <filesystem>
#include <string>

namespace rex::ui {
class ImGuiDrawer;
}

namespace gw2::benchmark {

inline constexpr int kLauncherSeconds = 60;

void SetResultsFile(std::filesystem::path file);
// The replay every PC plays (assets/benchmark), so results compare; empty when it is missing.
std::filesystem::path BundledReplay();
// The launcher's Run benchmark: the bundled replay, for kLauncherSeconds, in the game that starts next.
void Request();
// The arguments that carry a requested benchmark into a restarted game (empty when none is requested).
std::wstring RelaunchArgs();

// Every frame (the pad poll, demo.cpp).
void EachFrame(const uint8_t* base);
// The results over the game once it has run, copied to the clipboard as they appear.
void CreateOverlay(rex::ui::ImGuiDrawer* drawer);

}  // namespace gw2::benchmark
