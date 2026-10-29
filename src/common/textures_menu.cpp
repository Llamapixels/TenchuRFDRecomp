#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include "app/app.h"
#include "graphics/texture_packs.h"
#include "port/open_folder.h"
#include <imgui.h>
#include <filesystem>
#include <string>
#include <vector>

namespace {
void Open(const std::filesystem::path& path){TenchuOpenFolder(path);}
std::string Name(const std::string& file){auto slash=file.find_last_of("/\\");return slash==std::string::npos?file:file.substr(slash+1);}
}

void TenchuRenderTexturesTab(){
    namespace packs=tenchu::packs;
    auto& s=app::Get();
    // The lists walk every texture seen; refresh them a few times a second, not every frame.
    static uint32_t revision=0;static int refreshed=-100;
    static packs::Stats stats;static std::vector<packs::Entry> review,replaced;static std::vector<packs::PackStatus> list;
    if(revision!=packs::Revision()||ImGui::GetFrameCount()-refreshed>=20){
        revision=packs::Revision();refreshed=ImGui::GetFrameCount();
        stats=packs::Counts();review=packs::ReviewList(200);replaced=packs::Replaced(500);list=packs::Packs();
    }
    ImGui::SeparatorText("Texture packs");
    if(ImGui::Checkbox("Replace textures",&s.custom_textures))app::Save();
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Off shows the original Xbox textures. Changes apply live.");
    ImGui::Text("Textures seen: %zu | replaced: %zu | to review: %zu | no match: %zu",stats.seen,stats.matched,stats.review,stats.unmatched);
    if(list.empty())ImGui::TextWrapped("No packs yet. Put a pack folder (or a link to one) in textures\\packs next to the game, then press Refresh packs.");
    else if(ImGui::BeginTable("packs",4,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("On",ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Pack");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("",ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for(size_t i=0;i<list.size();++i){
            const auto& p=list[i];
            ImGui::PushID(p.name.c_str());ImGui::TableNextRow();
            ImGui::TableNextColumn();bool on=p.enabled;
            if(ImGui::Checkbox("##on",&on)){packs::SetEnabled(p.name,on);app::Save();}
            ImGui::TableNextColumn();ImGui::TextUnformatted(p.name.c_str());
            ImGui::TableNextColumn();
            if(p.indexing)ImGui::Text("Indexing %zu / %zu",p.indexed,p.images);
            else if(!p.enabled)ImGui::TextDisabled("Off");
            else if(p.errors)ImGui::Text("Ready, %zu images (%zu unreadable)",p.images,p.errors);
            else ImGui::Text("Ready, %zu images",p.images);
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(i==0);if(ImGui::ArrowButton("##up",ImGuiDir_Up)){packs::Move(p.name,-1);app::Save();}ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Higher packs win when two packs match the same texture.");
            ImGui::SameLine();ImGui::BeginDisabled(i+1==list.size());if(ImGui::ArrowButton("##down",ImGuiDir_Down)){packs::Move(p.name,1);app::Save();}ImGui::EndDisabled();
            ImGui::SameLine();ImGui::BeginDisabled(p.indexing);if(ImGui::SmallButton("Reindex"))packs::Reindex(p.name);ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if(ImGui::Button("Refresh packs"))packs::Rescan();
    ImGui::SameLine();if(ImGui::Button("Open packs folder"))Open(packs::PacksDir());
    ImGui::TextWrapped("Works with packs made for the PS2 version (Tenchu: Wrath of Heaven). Each texture is matched to the pack by how it looks, so file names do not matter. "
        "Original textures show until a texture is matched, and coverage grows as you visit more of the game. The first index of a new pack takes a minute; it is cached afterwards.");

    if(ImGui::CollapsingHeader(("Needs review ("+std::to_string(review.size())+")###review").c_str())){
        ImGui::TextWrapped("Close matches that were not applied automatically, often textures this version changed. Compare shows the Xbox texture on the left and the pack image on the right.");
        if(review.empty())ImGui::TextDisabled("Nothing to review.");
        for(const auto& e:review){
            ImGui::PushID(int(e.key));ImGui::PushID(int(e.key>>32));
            ImGui::Text("%s  (%.2f)",Name(e.file).c_str(),e.ncc);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s / %s\nShape match %.3f, colour difference %.3f",e.pack.c_str(),e.file.c_str(),e.ncc,e.color);
            ImGui::SameLine();ImGui::BeginDisabled(e.compare.empty());if(ImGui::SmallButton("Compare"))Open(e.compare);ImGui::EndDisabled();
            ImGui::SameLine();if(ImGui::SmallButton("Use it"))packs::Approve(e.key);
            ImGui::SameLine();if(ImGui::SmallButton("Never"))packs::Block(e.key);
            ImGui::PopID();ImGui::PopID();
        }
    }
    if(ImGui::CollapsingHeader(("Replaced ("+std::to_string(stats.matched)+")###replaced").c_str())){
        ImGui::TextWrapped("If a replacement looks wrong in game, find it here and choose Block; the original texture returns at once.");
        if(ImGui::BeginChild("replaced",{0,220},ImGuiChildFlags_Border)){
            for(const auto& e:replaced){
                ImGui::PushID(int(e.key));ImGui::PushID(int(e.key>>32));
                ImGui::Text("%s",Name(e.file).c_str());
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s / %s\nShape match %.3f, colour difference %.3f",e.pack.c_str(),e.file.c_str(),e.ncc,e.color);
                ImGui::SameLine();if(ImGui::SmallButton("Open image"))Open(e.image);
                ImGui::SameLine();if(ImGui::SmallButton("Block"))packs::Block(e.key);
                ImGui::PopID();ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    if(ImGui::Button("Forget review choices"))packs::ClearChoices();
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Clears every Use it and Never/Block choice.");

    ImGui::SeparatorText("Xbox texture dumps");
    if(ImGui::Checkbox("Dump textures",&s.dump_textures))app::Save();
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Saves each texture the game uses once, for making Xbox-keyed packs.");
    ImGui::SameLine();if(ImGui::Button("Open dumps folder"))Open(app::ExeDir()/"textures"/"dumps");
    int format=s.texture_dump_format==1?1:0;
    ImGui::SetNextItemWidth(160);
    if(ImGui::Combo("Dump format",&format,"PNG (easiest to edit)\0DDS (Xbox compression)\0")){s.texture_dump_format=uint32_t(format);app::Save();}
    ImGui::TextWrapped("Edited dumps go in textures\\replacement, or in a pack folder, keeping the 16-character key in the file name (256x256_<key>_DXT5.dds). "
        "Those replace exactly that texture, count as replaced above, and take priority over matched packs. Dumps hold only the full size image; "
        "smaller versions (mipmaps) are made from your edit when it loads. PNG dumps are the easiest to edit.");
}
