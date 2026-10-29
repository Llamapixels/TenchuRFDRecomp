#pragma once
#include "workbench.h"
#include "icons.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// shared by the workbench's pages
namespace wb {
namespace fs = std::filesystem;

std::string U8(const fs::path& p);
fs::path FromU8(const std::string& s);
std::string Lower(std::string s);
std::string SizeText(uint64_t bytes);
// file dialogs; empty when cancelled. filter: pairs of NUL-terminated strings
fs::path OpenFile(const wchar_t* filter, const fs::path& folder = {});
fs::path SaveFile(const wchar_t* filter, const wchar_t* extension, const fs::path& suggested);
fs::path PickFolder(const wchar_t* title, const fs::path& folder = {});
bool Ask(const std::string& text);
void Tell(const std::string& text);
bool ReadFile(const fs::path& path, std::vector<uint8_t>& out);
bool WriteFile(const fs::path& path, const std::vector<uint8_t>& data);  // creates folders, replaces whole
void ShowInExplorer(const fs::path& path);                              // selects a file, or opens a folder
void OpenWith(const fs::path& path, bool choose);                       // the default app, or Windows' "Open with" list

// the game folder: TenchuRFD.exe with extracted\, mods\, textures\ beside it
const fs::path& GameDir();
fs::path Extracted();
std::vector<fs::path> GameArchives();  // .con files in extracted\, relative to it
bool ValidModName(const std::string& name);
void WriteModToml(const fs::path& mod, const std::string& title, const std::string& description);  // when missing

// settings remembered in RFD Switchblade.ini beside the program
std::string Setting(const std::string& key, const std::string& fallback = {});
void SetSetting(const std::string& key, const std::string& value);
float SettingFloat(const std::string& key, float fallback);

// the status bar: the last message, and what is running in the background
void Notify(const std::string& text, bool warning = false);
struct Activity {
    std::string what;
    float progress = -1;  // 0..1, or -1 when unknown
};
// files dropped on the window this frame
const std::vector<fs::path>& Dropped();

// unsaved work: the window asks before closing while any page has some
void SetUnsaved(const char* page, bool unsaved);

// layout helpers
ImFont* HeadingFont();
void Heading(const char* text);
bool IconButton(const char* icon, const char* label, const char* tooltip = nullptr);
// a row of joined buttons, one of them on; true when it changed
bool Segmented(const char* id, int* value, const char* const* labels, int count);
void Hint(const char* text);  // a muted wrapped line
// draws the picture as large as fits in size, keeping its shape, over a checkerboard
void ImageFit(const Picture& picture, ImVec2 size);
// one picture centred in a framed box; a placeholder while it loads (null) or when it failed (empty)
void PictureBox(const std::shared_ptr<Picture>& picture, ImVec2 box, const char* empty = "loading", ImU32 frame = 0);
void GameFolderMissing();

// pictures decoded on worker threads, newest request first; the cache keeps what was drawn lately
struct Bitmap {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;  // BGRA
};
using LoadPicture = std::function<bool(Bitmap&)>;
uint64_t PictureKey(const std::string& text, uint32_t side);
// null while loading; a Picture without id when loading failed. side: largest side kept
std::shared_ptr<Picture> GetPicture(uint64_t key, uint32_t side, const LoadPicture& load);
void PrefetchPicture(uint64_t key, uint32_t side, const LoadPicture& load);
void ForgetPicture(uint64_t key);
void ForgetPictures();  // all, and waits for loads in flight
bool PicturesIdle();

void FilesPage();
void PacksPage();
void LevelsPage();
bool FilesBusy(Activity& a);
bool PacksBusy(Activity& a);
bool LevelsBusy(Activity& a);

}  // namespace wb
