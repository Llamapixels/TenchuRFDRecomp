#pragma once
#include <imgui.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

// RFD Switchblade: a separate program beside the game (tools\RFD Switchblade.exe).
namespace wb {

// an image on the GPU for ImGui::Image
struct Picture {
    uint32_t w = 0, h = 0;
    ImTextureID id{};
    ~Picture();
};
std::shared_ptr<Picture> Upload(const uint8_t* bgra, uint32_t w, uint32_t h);
void* MainWindow();  // HWND, owner of dialogs

// --capture runs: a page shown without user input, for screenshots of the layout
struct Automation {
    int page = -1;        // sidebar page
    int packs_mode = -1;  // texture packs: 0 review, 1 browse; levels: 1 a new level
    bool match = false;   // match the last pack on start
    int select = -1;      // texture or file to select, by position in the list
};
Automation& Automate();
bool Settled();  // nothing loading or running

// called by the window (main.cpp)
void Start(ImFont* heading);
void Frame();
void Drop(std::vector<std::filesystem::path> files);
bool CanClose();  // asks about unsaved work
void Stop();      // saves settings and stops background work

}  // namespace wb
