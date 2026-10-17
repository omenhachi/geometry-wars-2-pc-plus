#pragma once

#include <filesystem>

#include "game_files.h"
#include "launcher_platform.h"

namespace gw2 {

// Neutral wording: this is the first screen a player sees, and the failure is reported only after Play.
inline GraphicsFallbackInfo DescribeGameFilesWithoutGraphics(const std::filesystem::path& game_dir,
                                                             GameFileState state) {
  GraphicsFallbackInfo info{Utf8(game_dir), "Setup required", false};
  switch (state) {
    case GameFileState::kValid:
      info.game_files_status = "Ready";
      info.game_files_ready = true;
      break;
    case GameFileState::kDifferentVersion:
      info.game_files_status = "Different version";
      break;
    case GameFileState::kReadError:
      info.game_files_status = "Not readable";
      break;
    case GameFileState::kMissing:
      break;
  }
  return info;
}

}  // namespace gw2
