#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include "app/app.h"
#include <filesystem>
#include <fstream>
#include <string>

// A release carries no game files: on the first start the player picks their disc
// image, and its location is remembered in game_location.txt beside the game. The
// SDK's own search of the assets folder still comes first (development builds).
namespace {
std::filesystem::path LocationFile(){return app::ExeDir()/"game_location.txt";}
}

std::filesystem::path TenchuRememberedIso(){
    std::ifstream in(LocationFile(),std::ios::binary);
    std::string line;
    if(!in||!std::getline(in,line))return {};
    while(!line.empty()&&(line.back()=='\r'||line.back()=='\n'))line.pop_back();
    return std::filesystem::path(std::u8string(line.begin(),line.end()));
}

void TenchuRememberIso(const std::filesystem::path& iso){
    auto text=iso.u8string();
    std::ofstream(LocationFile(),std::ios::binary)<<std::string(text.begin(),text.end())<<"\n";
}

// empty when the player cancels
std::filesystem::path TenchuPickIso(bool retry){
    const wchar_t* text=retry
        ?L"That disc image is not Tenchu: Return from Darkness (USA), the release this build was made for.\n\nPick another .iso?"
        :L"Choose your copy of Tenchu: Return from Darkness (USA) as an Xbox disc image (.iso).\n\n"
         L"The first start then prepares the game's code on this PC, which takes a few minutes. Later starts are quick.";
    if(MessageBoxW(nullptr,text,app::kGame.short_title,MB_OKCANCEL|(retry?MB_ICONWARNING:MB_ICONINFORMATION))!=IDOK)return {};
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    wchar_t file[MAX_PATH*4]={};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.lpstrFilter=L"Xbox disc image (*.iso)\0*.iso\0All files (*.*)\0*.*\0";
    ofn.lpstrFile=file;
    ofn.nMaxFile=DWORD(std::size(file));
    ofn.lpstrTitle=L"Select your Tenchu: Return from Darkness disc image";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_HIDEREADONLY|OFN_EXPLORER;
    if(!GetOpenFileNameW(&ofn))return {};
    return std::filesystem::path(file);
}
