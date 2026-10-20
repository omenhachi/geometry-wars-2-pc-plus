#pragma once

#include <filesystem>

#include "updater/update_controller.h"

namespace gw2 {

trg::update::UpdateConfig MakeUpdateConfig(const std::filesystem::path& install_dir);

}  // namespace gw2
