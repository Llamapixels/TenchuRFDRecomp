#include "app/app.h"
#include "port/memory_patches.h"
#include "port/open_folder.h"
#include <imgui.h>
#include <string>
#include <vector>

void TenchuRenderPatchesTab(){
    namespace patches=tenchu::patches;
    // the list reads the writer's state: refresh it a few times a second, not every frame
    static std::vector<patches::Status> list;static int refreshed=-100;
    if(ImGui::GetFrameCount()-refreshed>=15){list=patches::List();refreshed=ImGui::GetFrameCount();}
    ImGui::SeparatorText("Memory patches");
    ImGui::TextWrapped("Patches shared as text files, like Cheat Engine patches. Each .txt in the memory_patches folder next to the game can hold several; "
        "paste a whole post in and only the [name], Author= and Address,<address>,value,<value> lines are read. "
        "A patch that is on is written many times a second and its original bytes come back when you turn it off.");
    if(list.empty())ImGui::TextWrapped("No patches yet. Open the folder, add a .txt with a patch, then press Reload.");
    else if(ImGui::BeginTable("patches",3,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("On",ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Patch");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();
        for(const auto& p:list){
            ImGui::PushID(p.id.c_str());ImGui::TableNextRow();
            ImGui::TableNextColumn();bool on=p.enabled;
            if(ImGui::Checkbox("##on",&on)){patches::SetEnabled(p.id,on);app::Save();refreshed=-100;}
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p.name.c_str());
            if(!p.author.empty()){ImGui::SameLine();ImGui::TextDisabled("by %s",p.author.c_str());}
            if(ImGui::IsItemHovered()||ImGui::IsItemHovered(ImGuiHoveredFlags_None))
                ImGui::SetTooltip("%s%s%s",p.file.c_str(),p.notes.empty()?"":"\n",p.notes.c_str());
            ImGui::TableNextColumn();
            if(!p.problems.empty()&&p.state==patches::State::Outside)ImGui::TextColored({1,.45f,.35f,1},"Cannot be read");
            else if(!p.code.empty())ImGui::TextColored({1,.8f,.3f,1},"Changes game code: no effect");
            else if(!p.enabled)ImGui::TextDisabled("Off");
            else if(p.state==patches::State::Waiting)ImGui::TextDisabled("Waiting for the game");
            else ImGui::TextColored({.55f,1,.45f,1},"On");
            if(ImGui::IsItemHovered()){
                std::string tip;
                if(!p.code.empty())tip+="It writes into the game's code ("+p.code+"). This port runs the game recompiled to PC code, so the original Xbox code bytes are never run and editing them does nothing. Patches that change data (values, tables) work.";
                for(const auto& problem:p.problems)tip+=(tip.empty()?"":"\n")+problem;
                if(p.enabled&&p.state==patches::State::Waiting)tip+=(tip.empty()?"":"\n")+std::string("Part of it is not in memory the game has set up yet; it is written once it is.");
                if(!tip.empty())ImGui::SetTooltip("%s",tip.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if(ImGui::Button("Reload")){patches::Rescan();refreshed=-100;}
    ImGui::SameLine();if(ImGui::Button("Open patches folder"))TenchuOpenFolder(patches::Folder());
    ImGui::TextWrapped("Addresses can be the game's own or Cheat Engine's (game memory starts at 100000000). The value's digits set its size: "
        "2 for a byte, 4 for 2 bytes, 8 for 4 bytes; a value with a point is a float. A patch that points the game at a routine it was not built with stops the game "
        "with a \"has no recompiled function\" error: the log names the patches that were on.");
}
