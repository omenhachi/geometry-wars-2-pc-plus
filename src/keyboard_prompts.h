#pragma once

#include <filesystem>

namespace gw2 {

// The keyboard and mouse controls diagram the help screens show with keyboard prompts, next to controller.tga.
inline constexpr char kDiagramFile[] = "controller_kbm.tga";

// Copies the port's diagram (assets/controls) into the game folder when it is missing or differs, so a fresh or
// repaired install has it. Before the runtime mounts the game folder, which it lists once.
void InstallKeyboardDiagram(const std::filesystem::path& game_data_root);

}  // namespace gw2
