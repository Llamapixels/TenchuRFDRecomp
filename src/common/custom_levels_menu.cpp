#include "port/custom_levels.h"
#include "port/open_folder.h"
#include <imgui.h>
#include <string>
#include <vector>

void TenchuRenderLevelsTab(){
    namespace levels=tenchu::levels;
    static std::vector<levels::LevelInfo> list;static bool scanned=false;static std::string status;
    if(!scanned){levels::Rescan();list=levels::List();scanned=true;}
    ImGui::SeparatorText("Custom levels");
    ImGui::TextWrapped("Each folder in levels next to the game is a custom level, made with RFD Switchblade's Levels page. "
        "Pick one here, then start the mission it is built on: that mission loads the custom level instead of its own map. "
        "Enemies, items and the sky stay the mission's own.");
    const std::string active=levels::Active();
    if(list.empty())ImGui::TextWrapped("No custom levels yet. Make one in RFD Switchblade (Levels, New level), or copy a level folder into the levels folder, then press Refresh.");
    else if(ImGui::BeginTable("levels",3,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("Level");
        ImGui::TableSetupColumn("Loads on");
        ImGui::TableSetupColumn("",ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for(const auto& l:list){
            ImGui::PushID(l.folder.c_str());ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(l.name.c_str());
            if(!l.author.empty()){ImGui::SameLine();ImGui::TextDisabled("by %s",l.author.c_str());}
            if(!l.description.empty()&&ImGui::IsItemHovered())ImGui::SetTooltip("%s",l.description.c_str());
            if(!l.problem.empty())ImGui::TextColored(ImVec4(1,.45f,.35f,1),"%s",l.problem.c_str());
            ImGui::TableNextColumn();
            if(l.stage>=0)ImGui::Text("Stage %d",l.stage);
            ImGui::TableNextColumn();
            if(l.folder==active){
                if(ImGui::Button("Stop using"))levels::Activate("",status);
            }else{
                ImGui::BeginDisabled(!l.problem.empty());
                if(ImGui::Button("Use"))levels::Activate(l.folder,status);
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if(ImGui::Button("Refresh levels")){levels::Rescan();list=levels::List();}
    ImGui::SameLine();
    if(ImGui::Button("Open levels folder")){
        std::error_code error;
        std::filesystem::create_directories(levels::LevelsDir(),error);
        TenchuOpenFolder(levels::LevelsDir());
    }
    if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
    ImGui::TextWrapped("Pick the level before the mission starts; a level picked during a mission applies the next time it loads. "
        "Only one custom level is used at a time, until you stop using it or exit the game.");
}
