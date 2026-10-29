#pragma once
// File-replacement mods: each folder in mods\ beside the game mirrors the disc.
// A file at mods\<mod>\<path on disc> replaces that disc file when the game
// next opens it. Enabled mods higher in the F6 > Mods list win. The game's
// executable (default.xbe) is never replaced: its code is precompiled.
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace tenchu::mods {

struct ModStatus {
    std::string name;                        // folder name
    std::string title, author, version, description;  // from an optional mod.toml
    bool enabled = true;
    size_t files = 0;
    size_t hidden = 0;                       // files a mod above also replaces
};

std::filesystem::path ModsDir();
void Rescan();
std::vector<ModStatus> List();
void SetEnabled(const std::string& name, bool enabled);
void Move(const std::string& name, int delta);  // priority, -1 up
// the host file replacing a disc path (parts below the disc root), empty when none
std::string Override(const std::vector<std::string>& parts);
size_t Replaced();                            // disc opens served from mods this session
std::vector<std::string> Recent(size_t max);  // the latest of them, newest first
// mods\disc_files.txt: every disc path and size, for mod makers
bool WriteDiscList(std::string& status);
// [mods] in the port settings
void Load(const void* toml_table);
void Save(void* toml_table);

}  // namespace tenchu::mods
