#pragma once
// Where we keep the player's files (never inside the original install).

#include <filesystem>

namespace d2k::app {

// Our counterpart of the install's GameSave folder (high scores, saved
// games): <user data>/GameSave, created on demand. Empty if unavailable.
std::filesystem::path game_save_dir();

} // namespace d2k::app
