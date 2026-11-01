#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include "port/port_settings.h"
#include "port/open_folder.h"
#include "app/app.h"
#include "graphics/renderer_selection.h"
#include "graphics/screenshot.h"
#include "graphics/post_processing.h"
#include "graphics/cutscene_motion_blur.h"
#include "cloth_solver.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

void TenchuRenderWindowOptions(){
    auto& s=app::Get();
    bool original_blur=tenchu::motion::original_blur.load();
    if(ImGui::Checkbox("Original cutscene motion blur",&original_blur)){tenchu::motion::original_blur=original_blur;app::Save();}
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Off removes the native cutscene frame-history trails. Enable to restore the original blur.");
    ImGui::SeparatorText("Window and frame rate");
    int mode=int(s.display_mode);
    if(ImGui::Combo("Display mode",&mode,"Fullscreen\0Borderless\0Windowed\0")){
        s.display_mode=app::DisplayMode(mode);app::ApplyDisplay();app::Save();
    }
    if(ImGui::Checkbox("VSync",&s.vsync))app::Save();
    const uint32_t values[]={30,60,90,120,144,165,240,0,app::kFpsCapRefresh};
    const char* labels[]={"30","60","90 (experimental)","120 (experimental)","144 (experimental)","165 (experimental)","240 (experimental)","Unlimited (experimental)","Display refresh"};
    const char* current=nullptr;for(unsigned i=0;i<9;++i)if(s.fps_cap==values[i])current=labels[i];
    char custom[32];std::snprintf(custom,sizeof(custom),"%u",s.fps_cap);
    if(ImGui::BeginCombo("FPS limit",current?current:custom)){
        for(unsigned i=0;i<9;++i)if(ImGui::Selectable(labels[i],s.fps_cap==values[i])){s.fps_cap=values[i];app::Save();}
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("60 is the default. Higher limits need gameplay testing and may affect game timing. A higher cap does not guarantee a higher frame rate.");
}
void TenchuRenderExtraTabs(){
    if(ImGui::BeginTabItem("Audio")){
        ImGui::SeparatorText("Game output");
        if(ImGui::Checkbox("Mute",&g_tenchu_mute)){TenchuUpdateMasterVolume();app::Save();}
        int volume=int(g_tenchu_master_volume);
        if(ImGui::SliderInt("Master volume",&volume,0,100,"%d%%",ImGuiSliderFlags_NoInput)){
            g_tenchu_master_volume=uint32_t(volume);TenchuUpdateMasterVolume();
        }
        if(ImGui::IsItemDeactivatedAfterEdit())app::Save();
        ImGui::TextWrapped("Applies immediately to the game's mixed audio, including streams. Muting keeps audio playback and game timing running.");
        ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("Profiles")){
        static char name[65]="My graphics";static int selected=-1;
        static auto names=TenchuGraphicsProfiles();static std::string status;
        ImGui::SeparatorText("Graphics profiles");
        ImGui::InputText("Profile name",name,sizeof(name));
        if(ImGui::Button("Save current graphics")){
            if(TenchuSaveGraphicsProfile(name,status)){names=TenchuGraphicsProfiles();for(size_t i=0;i<names.size();++i)if(names[i]==name)selected=int(i);}
        }
        const char* preview=selected>=0&&selected<int(names.size())?names[selected].c_str():"Choose a profile";
        if(ImGui::BeginCombo("Saved profile",preview)){
            for(size_t i=0;i<names.size();++i)if(ImGui::Selectable(names[i].c_str(),selected==int(i)))selected=int(i);
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(selected<0||selected>=int(names.size()));
        if(ImGui::Button("Load selected profile"))TenchuLoadGraphicsProfile(names[selected],status);
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button("Refresh profiles")){names=TenchuGraphicsProfiles();selected=-1;}
        if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
        ImGui::TextWrapped("Profiles save renderer, internal resolution, widescreen, window mode/size, VSync and FPS cap. Renderer changes require Restart game. Camera, cheats and audio stay as you set them.");
        ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("Tools")){
        ImGui::SeparatorText("Screenshots");
        if(ImGui::Button("Take screenshot (F12)"))TenchuTakeScreenshot();
        ImGui::TextWrapped("Captures the game after closing F6. Images are saved as BMP files in the screenshots folder next to the game.");
        auto status=tenchu::shots::Status();if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
        if(ImGui::Button("Open screenshot folder")){
            const auto folder=app::ExeDir()/"screenshots";std::error_code error;std::filesystem::create_directories(folder,error);
            if(error)tenchu::shots::Result("Cannot open screenshot folder: "+error.message());
            else TenchuOpenFolder(folder);
        }
        ImGui::SeparatorText("Performance");
        if(ImGui::Checkbox("Show FPS and frame time",&g_tenchu_performance_overlay))app::Save();
        ImGui::SeparatorText("Diagnostics");
        static std::string dump_status;
        if(ImGui::Button("Write thread dump (Ctrl+F3)")){TenchuDumpThreads("F6 > Tools");dump_status="Written to the game log (logs folder).";}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("If the game seems stuck, this records what every game thread is doing.");
        if(!dump_status.empty()){ImGui::SameLine();ImGui::TextUnformatted(dump_status.c_str());}
        if(ImGui::Checkbox("F3 render capture (debug)",&g_tenchu_debug_capture))app::Save();
        ImGui::TextWrapped("For bug reports only. F3 then saves three frames with every texture and draw call (about 20 MB) in the render_captures folder next to the game.");
        ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("Shader")){
        using namespace tenchu::post;
        bool on=enabled.load();if(ImGui::Checkbox("Enable shader preset",&on)){enabled=on;g_tenchu_shader_modified=!g_tenchu_shader_preset.empty();app::Save();}
        int preset=5;
        for(int p=0;p<5;++p){auto v=PresetValues(p);bool same=true;for(size_t i=0;i<count;++i)same &= values[i].load()==v[i];if(same)preset=p;}
        if(ImGui::Combo("Preset",&preset,"Neutral\0Vivid\0Cinematic\0Brighter shadows\0Modern Look\0Custom\0")){
            if(preset<5){const char* names[]={"Neutral","Vivid","Cinematic","Brighter shadows","Modern Look"};Preset(preset);enabled=true;g_tenchu_shader_preset=names[preset];g_tenchu_shader_modified=false;app::Save();}
        }
        static char shader_name[65]="My shader";
        static bool shader_name_initialized=false;
        if(!shader_name_initialized){if(!g_tenchu_shader_preset.empty())std::snprintf(shader_name,sizeof(shader_name),"%s",g_tenchu_shader_preset.c_str());shader_name_initialized=true;}
        static std::string shader_status;
        // Read the presets file when the tab is shown and after a save, not every frame.
        static std::vector<std::string> saved_shaders;static int shown_frame=-2;
        if(ImGui::GetFrameCount()!=shown_frame+1)saved_shaders=TenchuShaderPresets();
        shown_frame=ImGui::GetFrameCount();
        ImGui::InputText("Shader preset name",shader_name,sizeof(shader_name));
        if(ImGui::Button("Save shader preset")&&TenchuSaveShaderPreset(shader_name,shader_status))saved_shaders=TenchuShaderPresets();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Saves all shader sliders and enabled/compare settings. Reusing a name replaces that preset.");
        const char* active=g_tenchu_shader_preset.empty()?"Choose a saved shader preset":g_tenchu_shader_preset.c_str();
        if(ImGui::BeginCombo("Saved shader presets",active)){
            for(const auto& name:saved_shaders)if(ImGui::Selectable(name.c_str(),g_tenchu_shader_preset==name)){
                if(TenchuLoadShaderPreset(name,shader_status))std::snprintf(shader_name,sizeof(shader_name),"%s",name.c_str());
            }
            ImGui::EndCombo();
        }
        if(!g_tenchu_shader_preset.empty())ImGui::Text("Active: %s%s",g_tenchu_shader_preset.c_str(),g_tenchu_shader_modified?" (modified)":"");
        if(!shader_status.empty())ImGui::TextWrapped("%s",shader_status.c_str());
        ImGui::TextWrapped("The last preset and your current adjustments are restored when the game starts.");
        bool split=compare.load();if(ImGui::Checkbox("Split-screen compare",&split)){compare=split;g_tenchu_shader_modified=!g_tenchu_shader_preset.empty();app::Save();}
        if(split){
            float position=divider.load();if(ImGui::SliderFloat("Compare position",&position,.05f,.95f,"%.2f",ImGuiSliderFlags_AlwaysClamp)){divider=position;g_tenchu_shader_modified=!g_tenchu_shader_preset.empty();}
            if(ImGui::IsItemDeactivatedAfterEdit())app::Save();
            ImGui::TextUnformatted("Left: original | Right: shader");
        }
        const size_t order[]={6,8,9,10,11,12,13,0,1,2,3,14,5,15,4,7};
        for(size_t i:order){
            float value=values[i].load();
            if(ImGui::SliderFloat(labels[i],&value,minimum[i],maximum[i],"%.2f",ImGuiSliderFlags_AlwaysClamp)){values[i]=value;g_tenchu_shader_modified=!g_tenchu_shader_preset.empty();}
            if(ImGui::IsItemDeactivatedAfterEdit())app::Save();
            if(ImGui::IsItemHovered()){
                const char* tips[]={"Exposure in stops.","Adds a small brightness offset.","Contrast around middle gray.","Overall color intensity.","Midtone brightness.","Warm or cool color balance.","Fine edge detail.","Darkens image corners.","Local contrast in broader details.","Glow from pixels above the bloom threshold.","Higher values limit bloom to brighter pixels.","Glow radius in output pixels.","Spreads part of the glow over a wider area.","Softens bright highlights.","Adjusts muted colors more than saturated colors.","Brightens dark areas while preserving highlights."};
                ImGui::SetTooltip("%s Ctrl+click to enter a value.",tips[i]);
            }
        }
        if(ImGui::Button("Reset shader")){Reset();g_tenchu_shader_preset.clear();g_tenchu_shader_modified=false;app::Save();}
        ImGui::TextWrapped("Changes apply live on both renderers and are remembered. Disable to compare with the original image. Color adjustments affect the game and its HUD; the F6 menu stays unchanged.");
        ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("Textures")){TenchuRenderTexturesTab();ImGui::EndTabItem();}
    if(ImGui::BeginTabItem("Mods")){TenchuRenderModsTab();ImGui::EndTabItem();}
    if(ImGui::BeginTabItem("Levels")){TenchuRenderLevelsTab();ImGui::EndTabItem();}
    if(ImGui::BeginTabItem("Patches")){TenchuRenderPatchesTab();ImGui::EndTabItem();}
    if(ImGui::BeginTabItem("System")){
        ImGui::SeparatorText("Port settings");
        static bool reset=false;
        if(ImGui::Button("Restore default settings")){TenchuRestoreDefaults();reset=true;}
        ImGui::TextWrapped("Restores the F6 settings, resets the camera and disables cheats. Game saves and saved graphics profiles are kept.");
        if(reset)ImGui::TextWrapped("Defaults restored. Restart to apply a renderer change.");
        if(ImGui::Button("Restart game##system")){app::Save();TenchuRequestRestart();}
        ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("Physics")){
        auto modified=[](){g_tenchu_physics_modified=!g_tenchu_physics_preset.empty();};
        bool enhanced=tenchu::cloth::enhanced.load();
        if(ImGui::Checkbox("Enhanced cloth solver (experimental)",&enhanced)){tenchu::cloth::enhanced=enhanced;modified();app::Save();}
        ImGui::TextWrapped("Global settings apply to all loaded cloth, including supported memory-patched characters. Hair needs existing cloth data.");
        static char physics_name[65]="My physics";
        static std::vector<std::string> physics_presets;
        static std::string selected_physics,physics_status;
        static int physics_frame=-2;
        if(ImGui::GetFrameCount()!=physics_frame+1){
            physics_presets=TenchuPhysicsPresets();
            if(!g_tenchu_physics_preset.empty()){
                selected_physics=g_tenchu_physics_preset;
                std::snprintf(physics_name,sizeof(physics_name),"%s",selected_physics.c_str());
            }
        }
        physics_frame=ImGui::GetFrameCount();
        ImGui::InputText("Physics preset name",physics_name,sizeof(physics_name));
        if(ImGui::Button("Save physics preset")&&TenchuSavePhysicsPreset(physics_name,physics_status)){
            physics_presets=TenchuPhysicsPresets();selected_physics=physics_name;
        }
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Saves every physics control. Saving with an existing name replaces that preset.");
        if(ImGui::BeginCombo("Saved physics presets",selected_physics.empty()?"Choose a preset":selected_physics.c_str())){
            for(const auto& name:physics_presets)if(ImGui::Selectable(name.c_str(),selected_physics==name)){
                selected_physics=name;std::snprintf(physics_name,sizeof(physics_name),"%s",name.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(selected_physics.empty());
        if(ImGui::Button("Load physics preset"))TenchuLoadPhysicsPreset(selected_physics,physics_status);
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button("Reset physics settings")){TenchuResetPhysicsSettings();physics_status="Physics defaults restored. Saved presets are kept.";}
        if(!g_tenchu_physics_preset.empty())ImGui::Text("Active: %s%s",g_tenchu_physics_preset.c_str(),g_tenchu_physics_modified?" (modified)":"");
        if(!physics_status.empty())ImGui::TextWrapped("%s",physics_status.c_str());
        enhanced=tenchu::cloth::enhanced.load();
        ImGui::BeginDisabled(!enhanced);
        auto slider=[&](const char* label,std::atomic<float>& setting,float low,float high){
            float value=setting.load()*100;
            if(ImGui::SliderFloat(label,&value,low,high,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){setting=value/100;modified();}
            if(ImGui::IsItemDeactivatedAfterEdit())app::Save();
        };
        ImGui::SeparatorText("Body collision");
        bool collision=tenchu::cloth::body_collision.load();
        if(ImGui::Checkbox("Cloth body collision",&collision)){tenchu::cloth::body_collision=collision;modified();app::Save();}
        if(collision){
            slider("Body collision size",tenchu::cloth::body_size,50,150);
            bool edges=tenchu::cloth::edge_collision.load();
            if(ImGui::Checkbox("Cloth edge collision",&edges)){tenchu::cloth::edge_collision=edges;modified();app::Save();}
            if(edges)slider("Edge collision strength",tenchu::cloth::edge_strength,0,100);
        }
        ImGui::SeparatorText("Motion and shape");
        slider("Cloth response speed",tenchu::cloth::response_speed,25,100);
        slider("Motion retention",tenchu::cloth::settling_drag,50,100);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Lower retention adds drag and settles flutter sooner. This multiplies the model's own damping.");
        slider("Cloth gravity",tenchu::cloth::gravity_scale,0,200);
        slider("Stretch stiffness",tenchu::cloth::stretch_stiffness,25,100);
        slider("Bend resistance",tenchu::cloth::bend_resistance,0,100);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Resists folding away from the original loaded cloth shape. Higher values can look stiff.");
        ImGui::SeparatorText("Self-collision");
        bool self=tenchu::cloth::self_collision.load();
        if(ImGui::Checkbox("Cloth self-collision",&self)){tenchu::cloth::self_collision=self;modified();app::Save();}
        if(self){
            slider("Self-collision strength",tenchu::cloth::self_strength,0,100);
            slider("Self-collision thickness",tenchu::cloth::self_thickness,1,30);
        }
        ImGui::TextWrapped("Self-collision separates distant points and edges within each cloth panel. Start with a small thickness; it can cost performance or cause bunching on coarse models.");
        ImGui::EndDisabled();
        ImGui::TextWrapped("Body capsules approximate the animated skeleton. Reduce their size if cloth sits too far from the body. Turn off the enhanced solver for original behavior.");
        ImGui::EndTabItem();
    }
}
void TenchuRenderPerformanceOverlay(double fps){
    if(!g_tenchu_performance_overlay)return;
    const auto screen=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({screen.x-12,12},ImGuiCond_Always,{1,0});
    ImGui::SetNextWindowBgAlpha(.7f);
    ImGui::PushStyleColor(ImGuiCol_Text,{.8f,1,.7f,1});
    if(ImGui::Begin("Port performance",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoNav)){
        ImGui::Text("%.1f FPS | %.2f ms",fps,fps>0?1000/fps:0);
        ImGui::TextUnformatted(TenchuActiveRenderer()==1?"Vulkan":"Direct3D 12");
    }
    ImGui::End();ImGui::PopStyleColor();
}
