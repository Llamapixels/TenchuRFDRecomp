#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include "app/app.h"
#include "port/mods.h"
#include "port/open_folder.h"
#include "graphics/renderer_selection.h"
#include <imgui.h>
#include <string>
#include <vector>

void TenchuRenderModsTab(){
    namespace mods=tenchu::mods;
    static std::vector<mods::ModStatus> list;static bool scanned=false;static std::string status;
    if(!scanned){list=mods::List();scanned=true;}
    ImGui::SeparatorText("Mods");
    ImGui::TextWrapped("Each folder in mods next to the game is a mod. A file inside it replaces the disc file at the same path, "
        "for example mods\\My mod\\<folder>\\<file>. Mods higher in the list win when two replace the same file.");
    ImGui::TextWrapped("Files packed in an archive are replaced one at a time: mods\\My mod\\Media\\m.con.files\\<file> replaces only "
        "that file inside Media\\m.con, so mods changing different files of one archive work together. Files edited in the "
        "extracted folder apply the same way.");
    if(list.empty())ImGui::TextWrapped("No mods installed. Open the mods folder, add a mod folder, then press Refresh mods.");
    else if(ImGui::BeginTable("mods",4,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("On",ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Mod");
        ImGui::TableSetupColumn("Files");
        ImGui::TableSetupColumn("",ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        bool changed=false;
        for(size_t i=0;i<list.size();++i){
            const auto& m=list[i];
            ImGui::PushID(m.name.c_str());ImGui::TableNextRow();
            ImGui::TableNextColumn();bool on=m.enabled;
            if(ImGui::Checkbox("##on",&on)){mods::SetEnabled(m.name,on);changed=true;}
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.title.empty()?m.name.c_str():m.title.c_str());
            if(ImGui::IsItemHovered()&&(!m.author.empty()||!m.description.empty()||!m.version.empty()))
                ImGui::SetTooltip("%s%s%s%s%s",m.version.empty()?"":("Version "+m.version+"\n").c_str(),m.author.empty()?"":"By ",m.author.c_str(),m.author.empty()?"":"\n",m.description.c_str());
            ImGui::TableNextColumn();
            if(m.hidden&&m.enabled)ImGui::Text("%zu (%zu replaced by a mod above)",m.files,m.hidden);
            else ImGui::Text("%zu",m.files);
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(i==0);if(ImGui::ArrowButton("##up",ImGuiDir_Up)){mods::Move(m.name,-1);changed=true;}ImGui::EndDisabled();
            ImGui::SameLine();ImGui::BeginDisabled(i+1==list.size());if(ImGui::ArrowButton("##down",ImGuiDir_Down)){mods::Move(m.name,1);changed=true;}ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
        if(changed){list=mods::List();app::Save();}
    }
    if(ImGui::Button("Refresh mods")){mods::Rescan();list=mods::List();}
    ImGui::SameLine();
    if(ImGui::Button("Open mods folder")){
        TenchuOpenFolder(mods::ModsDir());
    }
    ImGui::SameLine();
    if(ImGui::Button("Open extracted folder")){
        const auto extracted=app::ExeDir()/"extracted";
        std::error_code error;
        if(std::filesystem::exists(extracted,error))TenchuOpenFolder(extracted);
        else status="The game files are unpacked into the extracted folder once you pick your disc image.";
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Every file of the disc, each archive unpacked into <name>.con.files beside it.\nDelete the folder to unpack it again.");
    ImGui::SameLine();if(ImGui::Button("Restart game##mods")){app::Save();TenchuRequestRestart();}
    ImGui::TextWrapped("Changes apply to files the game opens next; archives are rebuilt when the game restarts. "
        "The game's code (default.xbe) cannot be modded this way.");
    ImGui::Text("Disc files replaced this session: %zu",mods::Replaced());
    if(ImGui::TreeNode("Recently replaced")){
        auto recent=mods::Recent(20);
        if(recent.empty())ImGui::TextDisabled("None yet.");
        for(const auto& r:recent)ImGui::TextUnformatted(r.c_str());
        ImGui::TreePop();
    }
    ImGui::SeparatorText("For mod makers");
    if(ImGui::Button("Write disc file list"))mods::WriteDiscList(status);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Saves every disc path and size to mods\\disc_files.txt.");
    if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
    ImGui::TextWrapped("A mod can describe itself in mod.toml at its top level: title, author, version and description.");
}
