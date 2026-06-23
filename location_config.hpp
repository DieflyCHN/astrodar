#pragma once

#include <filesystem>
#include <vector>

#include "observer.hpp"

// Linux 使用 XDG_CONFIG_HOME（或 ~/.config），macOS 使用
// ~/Library/Application Support。配置仅保存用户明确保存的面板。
std::filesystem::path locationConfigurationPath();
std::vector<LocationPanel> loadLocationPanels();
bool saveLocationPanels(const std::vector<LocationPanel>& panels);
