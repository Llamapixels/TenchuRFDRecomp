#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include "ui.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>

using Microsoft::WRL::ComPtr;

namespace wb {
namespace {
fs::path g_game;
ImFont* g_heading = nullptr;
std::map<std::string, std::string> g_settings;
bool g_settings_loaded = false;
std::string g_message;
bool g_message_warning = false;
std::chrono::steady_clock::time_point g_message_time;
std::vector<fs::path> g_dropped, g_dropped_next;
std::map<std::string, bool> g_unsaved;
int g_page = 0;

fs::path ExeDir() {
    wchar_t buffer[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    return fs::path(std::wstring(buffer, n)).parent_path();
}
// the settings, kept in ModWorkbench.ini before the program was renamed (read from there until the new file is saved)
fs::path SettingsFile() { return ExeDir() / "RFD Switchblade.ini"; }
void LoadSettings() {
    g_settings_loaded = true;
    std::error_code ec;
    std::ifstream in(fs::exists(SettingsFile(), ec) ? SettingsFile() : ExeDir() / "ModWorkbench.ini", std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        const size_t eq = line.find('=');
        if (eq != std::string::npos && line[0] != '#') g_settings[line.substr(0, eq)] = line.substr(eq + 1);
    }
}
void SaveSettings() {
    std::ofstream out(SettingsFile(), std::ios::binary | std::ios::trunc);
    out << "# RFD Switchblade settings\n";
    for (auto& [k, v] : g_settings) out << k << "=" << v << "\n";
}
bool LooksLikeGame(const fs::path& dir) {
    std::error_code ec;
    return !dir.empty() && fs::is_directory(dir / "extracted", ec);
}
void SetGame(const fs::path& dir) {
    g_game = dir;
    SetSetting("game", U8(dir));
    Notify("Game folder: " + U8(dir));
}
std::wstring Wide(const std::string& s) {
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
    return w;
}
void ChooseGame() {
    fs::path dir = PickFolder(L"The folder with TenchuRFD.exe", g_game);
    if (dir.empty()) return;
    if (LooksLikeGame(dir)) SetGame(dir);
    else Tell("There is no extracted folder there. Start the game once and pick your disc image: it unpacks the game files into extracted beside it.");
}

struct Page {
    const char* icon;
    const char* name;
    const char* about;
    void (*draw)();
};
const Page kPages[] = {
    {ICON_FOLDER_OPEN, "Game files", "Browse, edit and pack them", FilesPage},
    {ICON_PICTURE, "Texture packs", "Match PS2 packs to the game", PacksPage},
    {ICON_GRID, "Levels", "View stages, make custom levels", LevelsPage},
};

void HelpPopup() {
    if (!ImGui::BeginPopupModal("Help", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34);
    Heading("Game files");
    ImGui::TextWrapped(
        "Everything on the disc, unpacked by the game on its first start. Files inside an archive (Media\\x.con) are in the "
        "x.con.files folder beside it. Change a file and the game uses it: it rebuilds the archive when it starts. "
        "Edit opens a texture in your image editor and converts it back each time you save there. Changed files can be "
        "turned into a mod to share, and an archive can be packed into a game-ready .con.");
    ImGui::Spacing();
    Heading("Texture packs");
    ImGui::TextWrapped(
        "Matches a PS2 Wrath of Heaven pack to this game. Sure matches are taken; Review shows the close calls a page at "
        "a time: click the wrong ones, then accept the page. Browse shows every pack image for one texture, closest first. "
        "Save matches writes rfd_matches.toml into the pack folder: the game reads it, and it travels with the pack.");
    ImGui::Spacing();
    Heading("Levels");
    ImGui::TextWrapped(
        "View a stage shows the game's maps as the game draws them, with their collision. Make a level builds a custom "
        "level: a ground around a mission's start, boxes for walls and platforms, and where the player starts. Save writes "
        "it into the levels folder beside the game; in the game, F6 > Levels > Use, then start that mission. A level is a "
        "folder: zip it to share it.");
    ImGui::Spacing();
    Heading("Keys");
    ImGui::TextUnformatted(
        "Review   Enter accept page, Space skip page, click a card to reject it\n"
        "Browse   Up/Down texture, Left/Right image, Enter use, S skip, A automatic\n"
        "Anywhere Ctrl+S save matches, Ctrl+Z undo, drop files on the window to import\n"
        "Levels   right-drag turn, middle-drag or Shift+right-drag move, wheel zoom, F frame, click a box to select it");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(ImGui::GetFontSize() * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
}  // namespace

std::string U8(const fs::path& p) {
    auto s = p.u8string();
    return {s.begin(), s.end()};
}
fs::path FromU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
std::string SizeText(uint64_t n) {
    char b[32];
    if (n >= 1 << 20) std::snprintf(b, sizeof(b), "%.1f MB", n / 1048576.0);
    else if (n >= 1 << 10) std::snprintf(b, sizeof(b), "%.0f KB", n / 1024.0);
    else std::snprintf(b, sizeof(b), "%llu B", static_cast<unsigned long long>(n));
    return b;
}

fs::path OpenFile(const wchar_t* filter, const fs::path& folder) {
    wchar_t file[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = static_cast<HWND>(MainWindow());
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = DWORD(std::size(file));
    const std::wstring start = folder.wstring();
    ofn.lpstrInitialDir = start.empty() ? nullptr : start.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? fs::path(file) : fs::path();
}

fs::path SaveFile(const wchar_t* filter, const wchar_t* extension, const fs::path& suggested) {
    wchar_t file[MAX_PATH * 4] = {};
    wcsncpy_s(file, suggested.filename().c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = static_cast<HWND>(MainWindow());
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = DWORD(std::size(file));
    ofn.lpstrDefExt = extension;
    const std::wstring start = suggested.parent_path().wstring();
    ofn.lpstrInitialDir = start.empty() ? nullptr : start.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&ofn) ? fs::path(file) : fs::path();
}

fs::path PickFolder(const wchar_t* title, const fs::path& folder) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(title);
    ComPtr<IShellItem> start;
    std::error_code ec;
    if (!folder.empty() && fs::is_directory(folder, ec) &&
        SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&start))))
        dialog->SetFolder(start.Get());
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->Show(static_cast<HWND>(MainWindow()))) || FAILED(dialog->GetResult(&item)) ||
        FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        return {};
    fs::path out(path);
    CoTaskMemFree(path);
    return out;
}

bool Ask(const std::string& text) {
    return MessageBoxW(static_cast<HWND>(MainWindow()), Wide(text).c_str(), L"RFD Switchblade", MB_YESNO | MB_ICONQUESTION) == IDYES;
}
void Tell(const std::string& text) {
    MessageBoxW(static_cast<HWND>(MainWindow()), Wide(text).c_str(), L"RFD Switchblade", MB_ICONINFORMATION);
}

bool ReadFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

bool WriteFile(const fs::path& path, const std::vector<uint8_t>& data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path temp = path;
    temp += L".partial";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!out) return false;
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

void ShowInExplorer(const fs::path& path) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str())) {
        SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
        ILFree(item);
    }
}

void OpenWith(const fs::path& path, bool choose) {
    if (choose) {
        OPENASINFO info{path.c_str(), nullptr, OAIF_ALLOW_REGISTRATION | OAIF_EXEC};
        SHOpenWithDialog(static_cast<HWND>(MainWindow()), &info);
        return;
    }
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(static_cast<HWND>(MainWindow()), L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        OpenWith(path, true);  // nothing is set to open this type
}

const fs::path& GameDir() { return g_game; }
fs::path Extracted() { return g_game / "extracted"; }

std::vector<fs::path> GameArchives() {
    std::vector<fs::path> out;
    std::error_code ec;
    if (g_game.empty()) return out;
    const fs::path root = Extracted();
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec) && _wcsicmp(it->path().extension().c_str(), L".files") == 0) {
            it.disable_recursion_pending();  // an archive's unpacked files
            continue;
        }
        if (it->is_regular_file(ec) && _wcsicmp(it->path().extension().c_str(), L".con") == 0)
            out.push_back(it->path().lexically_relative(root));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool ValidModName(const std::string& name) {
    if (name.empty() || name.find_first_of("<>:\"/\\|?*") != std::string::npos || name == "." || name == "..") return false;
    return name.back() != ' ' && name.back() != '.';
}

void WriteModToml(const fs::path& mod, const std::string& title, const std::string& description) {
    std::error_code ec;
    if (fs::exists(mod / "mod.toml", ec)) return;
    auto quote = [](const std::string& s) {
        std::string out;
        for (char c : s) out += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
        return "\"" + out + "\"";
    };
    fs::create_directories(mod, ec);
    std::ofstream(mod / "mod.toml", std::ios::binary) << "title = " << quote(title) << "\ndescription = " << quote(description) << "\n";
}

std::string Setting(const std::string& key, const std::string& fallback) {
    if (!g_settings_loaded) LoadSettings();
    auto it = g_settings.find(key);
    return it == g_settings.end() ? fallback : it->second;
}
void SetSetting(const std::string& key, const std::string& value) {
    if (!g_settings_loaded) LoadSettings();
    if (g_settings[key] == value) return;
    g_settings[key] = value;
    SaveSettings();
}
float SettingFloat(const std::string& key, float fallback) {
    const std::string s = Setting(key);
    return s.empty() ? fallback : std::strtof(s.c_str(), nullptr);
}

void Notify(const std::string& text, bool warning) {
    g_message = text;
    g_message_warning = warning;
    g_message_time = std::chrono::steady_clock::now();
}
const std::vector<fs::path>& Dropped() { return g_dropped; }
void SetUnsaved(const char* page, bool unsaved) { g_unsaved[page] = unsaved; }

ImFont* HeadingFont() { return g_heading; }
void Heading(const char* text) {
    ImGui::PushFont(g_heading);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}
bool IconButton(const char* icon, const char* label, const char* tooltip) {
    const bool pressed = ImGui::Button((std::string(icon) + "  " + label).c_str());
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}
bool Segmented(const char* id, int* value, const char* const* labels, int count) {
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, ImGui::GetStyle().ItemSpacing.y));
    for (int i = 0; i < count; i++) {
        if (i) ImGui::SameLine();
        const bool on = *value == i;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(.18f, .48f, .08f, 1) : ImVec4(.06f, .12f, .05f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1, 1, 1, 1) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Button(labels[i]) && !on) {
            *value = i;
            changed = true;
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void ImageFit(const Picture& picture, ImVec2 size) {
    if (!picture.id || size.x < 4 || size.y < 4) return;
    const float scale = std::min(size.x / picture.w, size.y / picture.h);
    const ImVec2 shown(picture.w * scale, picture.h * scale);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float cell = std::max(4.f, ImGui::GetFontSize() * .5f);
    draw->PushClipRect(at, ImVec2(at.x + shown.x, at.y + shown.y), true);
    for (float y = 0; y < shown.y; y += cell)
        for (float x = 0; x < shown.x; x += cell)
            draw->AddRectFilled(ImVec2(at.x + x, at.y + y), ImVec2(at.x + x + cell, at.y + y + cell),
                                (int(x / cell) + int(y / cell)) & 1 ? IM_COL32(58, 62, 58, 255) : IM_COL32(84, 88, 84, 255));
    draw->PopClipRect();
    ImGui::Image(picture.id, shown);
}

void PictureBox(const std::shared_ptr<Picture>& picture, ImVec2 box, const char* empty, ImU32 frame) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + box.x, at.y + box.y), IM_COL32(0, 0, 0, 70), 4);
    if (picture && picture->id) {
        const float scale = std::min(box.x / picture->w, box.y / picture->h);
        ImGui::SetCursorScreenPos(ImVec2(at.x + (box.x - picture->w * scale) / 2, at.y + (box.y - picture->h * scale) / 2));
        ImageFit(*picture, ImVec2(picture->w * scale, picture->h * scale));
    } else if (empty && *empty) {
        const char* text = picture ? "cannot be read" : empty;
        const ImVec2 size = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2(at.x + (box.x - size.x) / 2, at.y + (box.y - size.y) / 2), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    }
    if (frame) draw->AddRect(at, ImVec2(at.x + box.x, at.y + box.y), frame, 4, 0, std::max(2.f, ImGui::GetFontSize() / 8));
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy(box);
}

void GameFolderMissing() {
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 2));
    Heading("Where is the game?");
    Hint("Choose the folder with TenchuRFD.exe. Start the game once first: on its first start it unpacks every game file into "
         "the extracted folder beside it, which is what RFD Switchblade works on.");
    ImGui::Spacing();
    if (IconButton(ICON_FOLDER, "Choose game folder...")) ChooseGame();
}

Automation& Automate() {
    static Automation a;
    return a;
}

bool Settled() {
    Activity a;
    return !FilesBusy(a) && !PacksBusy(a) && !LevelsBusy(a) && PicturesIdle();
}

void Start(ImFont* heading) {
    g_heading = heading;
    g_page = std::clamp(std::atoi(Setting("page", "0").c_str()), 0, int(std::size(kPages)) - 1);
    if (Automate().page >= 0) g_page = std::clamp(Automate().page, 0, int(std::size(kPages)) - 1);
    if (LooksLikeGame(FromU8(Setting("game")))) {
        g_game = FromU8(Setting("game"));
        return;
    }
    // shipped in tools\ beside the game
    for (const fs::path& dir : {ExeDir(), ExeDir().parent_path()})
        if (LooksLikeGame(dir)) {
            g_game = dir;
            return;
        }
}

void Drop(std::vector<fs::path> files) {
    for (auto& f : files) g_dropped_next.push_back(std::move(f));
}

bool CanClose() {
    std::string pages;
    for (auto& [page, unsaved] : g_unsaved)
        if (unsaved) pages += (pages.empty() ? "" : ", ") + page;
    return pages.empty() || Ask("There is unsaved work in " + pages + ". Close anyway?");
}

void Stop() {
    ForgetPictures();
    SaveSettings();
}

void Frame() {
    g_dropped.swap(g_dropped_next);
    g_dropped_next.clear();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("RFD Switchblade", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    const float em = ImGui::GetFontSize();
    const float status_h = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;

    // sidebar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.006f, .022f, .006f, 1));
    ImGui::BeginChild("sidebar", ImVec2(em * 14, ImGui::GetContentRegionAvail().y - status_h), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushFont(g_heading);
    ImGui::TextColored(ImVec4(.62f, .86f, .44f, 1), "Tenchu RFD");
    ImGui::PopFont();
    ImGui::TextDisabled("RFD Switchblade");
    ImGui::Dummy(ImVec2(0, em));
    for (int i = 0; i < int(std::size(kPages)); i++) {
        const Page& p = kPages[i];
        ImGui::PushID(i);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float h = em * 3.2f;
        if (ImGui::Selectable("##page", g_page == i, 0, ImVec2(0, h))) {
            g_page = i;
            SetSetting("page", std::to_string(i));
        }
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (g_page == i) draw->AddRectFilled(at, ImVec2(at.x + em * .22f, at.y + h), IM_COL32(150, 220, 100, 255));
        draw->AddText(ImVec2(at.x + em * .7f, at.y + em * .35f), ImGui::GetColorU32(ImGuiCol_Text), (std::string(p.icon) + "  " + p.name).c_str());
        ImGui::PushClipRect(at, ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y + h), true);
        draw->AddText(nullptr, em * .8f, ImVec2(at.x + em * .7f, at.y + em * 1.75f), ImGui::GetColorU32(ImGuiCol_TextDisabled), p.about);
        ImGui::PopClipRect();
        ImGui::PopID();
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing() * 3 - em);
    ImGui::Separator();
    ImGui::TextDisabled("Game folder");
    ImGui::PushTextWrapPos(0);
    const std::string folder = g_game.empty() ? "not chosen" : U8(g_game.filename());
    if (ImGui::Selectable((std::string(ICON_FOLDER "  ") + folder).c_str())) ChooseGame();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nClick to choose another", g_game.empty() ? "No game folder" : U8(g_game).c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Selectable(ICON_HELP "  Help and keys")) ImGui::OpenPopup("Help");
    HelpPopup();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // the page
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("page", ImVec2(0, ImGui::GetContentRegionAvail().y - status_h), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (g_game.empty()) GameFolderMissing();
    else kPages[g_page].draw();
    ImGui::EndChild();

    // status bar: background work, then the last message
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.006f, .022f, .006f, 1));
    ImGui::BeginChild("status", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY(ImGui::GetStyle().ItemSpacing.y * .5f);
    Activity a;
    if (FilesBusy(a) || PacksBusy(a) || LevelsBusy(a)) {
        ImGui::TextUnformatted(a.what.c_str());
        ImGui::SameLine();
        if (a.progress >= 0) ImGui::ProgressBar(a.progress, ImVec2(em * 12, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0, em * 2);
    }
    const double age = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_message_time).count();
    if (!g_message.empty()) {
        const float fade = float(std::clamp(1.5 - (age - 8) / 4, .45, 1.0));
        ImGui::TextColored(g_message_warning ? ImVec4(1, .75f, .35f, fade) : ImVec4(.75f, .9f, .65f, fade), "%s  %s",
                           g_message_warning ? ICON_WARNING : ICON_INFO, g_message.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

}  // namespace wb
