#pragma once
#include <filesystem>

// Shows a folder (created when missing) or file in Explorer. Runs on its own thread
// with no owner window: ShellExecute from the render thread, owned by the game
// window, can wait on that window's message thread while it waits on a frame.
void TenchuOpenFolder(const std::filesystem::path& path);
