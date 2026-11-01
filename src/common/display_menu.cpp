#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "app/app.h"
#include "core/log.h"
#include "graphics/internal_resolution.h"
#include <imgui.h>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include "game_cheats.h"
#include "game_camera.h"
#include "graphics/widescreen.h"
#include "graphics/renderer_selection.h"
#include "graphics/screenshot.h"
#include "port/port_settings.h"
#include "port/memory_patches.h"

uint32_t g_tenchu_internal_percent=100;
bool g_tenchu_widescreen=false;
namespace {
std::atomic<uint32_t> active_percent{100};
std::atomic<bool> visible{false};
void SetVisible(bool open){visible=open;tenchu::patches::SetMenuOpen(open);}
ImVec4 Color(float r,float g,float b){return {r,g,b,1};}
}
void TenchuInitializeDisplayMenu(){
    g_tenchu_internal_percent=tenchu::ClampInternalPercent(g_tenchu_internal_percent);
    active_percent=g_tenchu_internal_percent;
    tenchu::wide::enabled=g_tenchu_widescreen;
    SetVisible(std::getenv("TENCHU_MENU_OPEN")!=nullptr);
    TenchuInitializeRenderer();
    TenchuInitializePortSettings();
}
void TenchuSetInternalPercent(uint32_t value){g_tenchu_internal_percent=tenchu::ClampInternalPercent(value);active_percent=g_tenchu_internal_percent;}
void TenchuTakeScreenshot(){SetVisible(false);tenchu::shots::Request();}
void TenchuInitializeMenuWindow(void*){
    ImGui::GetIO().ConfigFlags&=~(ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad);
}
float TenchuInternalScale(uint32_t){
    // GPU regression runs can select an allocation scale without saving it.
    if(std::getenv("TENCHU_GPU_SELFTEST"))
        if(const char* p=std::getenv("TENCHU_TEST_INTERNAL_PERCENT"))return tenchu::ClampInternalPercent(uint32_t(std::strtoul(p,nullptr,10)))/100.f;
    return active_percent.load(std::memory_order_relaxed)/100.f;
}
bool TenchuDisplayMenuMessage(uint32_t msg,uintptr_t key,intptr_t flags,intptr_t* result){
    if(msg!=WM_KEYDOWN&&!(key==VK_F10&&msg==WM_SYSKEYDOWN))return false;
    if(key==VK_F12){if(!(flags&(intptr_t(1)<<30)))TenchuTakeScreenshot();*result=0;return true;}
    if(key==VK_F10){
        if(!(flags&(intptr_t(1)<<30)))tenchu::camera::free_requested=!tenchu::camera::free_requested.load();
        *result=0;return true;
    }
    if(key==VK_F6){
        if(!(flags&(intptr_t(1)<<30)))SetVisible(!visible.load());
        *result=0;return true;
    }
    if(key==VK_ESCAPE&&visible){SetVisible(false);*result=0;return true;}
    return false;
}
void TenchuRenderDisplayMenu(double fps){
    TenchuInitializeMenuWindow(nullptr);
    TenchuRenderPerformanceOverlay(fps);
    static bool was_visible=false;
    static int draft=100;
    tenchu::camera::menu_open=visible.load();
    if(!visible){if(was_visible)TenchuSaveCameraIfChanged(false);was_visible=false;return;}
    if(!was_visible){draft=int(active_percent.load());was_visible=true;}
    const ImVec2 screen=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({16,24},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({std::min(760.f,std::max(280.f,screen.x-32)),std::min(560.f,std::max(250.f,screen.y-48))},ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,0);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,0);
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding,0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,Color(.005f,.035f,.005f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,Color(.04f,.25f,0));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,Color(.10f,.53f,0));
    ImGui::PushStyleColor(ImGuiCol_Text,Color(.87f,.93f,.84f));
    ImGui::PushStyleColor(ImGuiCol_Border,Color(.08f,.38f,.02f));
    ImGui::PushStyleColor(ImGuiCol_Separator,Color(.08f,.38f,.02f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,Color(.19f,.25f,.19f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,Color(.24f,.33f,.23f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,Color(.28f,.40f,.25f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,Color(.44f,.56f,.40f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,Color(.57f,.80f,.40f));
    ImGui::PushStyleColor(ImGuiCol_Button,Color(.06f,.26f,.02f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Color(.10f,.42f,.02f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,Color(.14f,.56f,.03f));
    ImGui::PushStyleColor(ImGuiCol_Tab,Color(.04f,.19f,.02f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected,Color(.08f,.35f,.02f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered,Color(.12f,.48f,.03f));
    bool open=true;
    if(ImGui::Begin("Tenchu: Return from Darkness (F6)",&open,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNavInputs|ImGuiWindowFlags_NoNavFocus)){
        if(ImGui::BeginTabBar("Display settings")){
            if(ImGui::BeginTabItem("Display")){
                ImGui::SeparatorText("Renderer");
                int renderer=int(g_tenchu_renderer);
                if(ImGui::Combo("Renderer (restart)",&renderer,"Direct3D 12\0Vulkan (vkd3d-proton)\0")){
                    if(renderer==0||TenchuVulkanAvailable()){g_tenchu_renderer=uint32_t(renderer);app::Save();}
                }
                ImGui::Text("Active: %s",TenchuActiveRenderer()==1?"Vulkan":"Direct3D 12");
                if(g_tenchu_renderer!=TenchuActiveRenderer())ImGui::TextUnformatted("Restart to apply the selected renderer.");
                if(ImGui::Button("Restart game")){app::Save();TenchuRequestRestart();}
                ImGui::TextWrapped("Restart returns to the title screen. Vulkan uses vkd3d-proton / DXVK.");
                TenchuRenderWindowOptions();
                ImGui::SeparatorText("Internal resolution");
                ImGui::TextUnformatted("Native frame: 640 x 480");
                const uint32_t current=active_percent.load();
                ImGui::Text("Current: %u x %u  (%.2fx)",tenchu::InternalDimension(640,current),tenchu::InternalDimension(480,current),current/100.f);
                float scale=draft/100.f;
                ImGui::SetNextItemWidth(std::max(150.f,ImGui::GetContentRegionAvail().x-115));
                if(ImGui::SliderFloat("Render scale",&scale,.5f,4.f,"%.2fx"))draft=int(scale*100+.5f);
                for(int p:{100,150,200,300,400}){
                    if(p!=100)ImGui::SameLine();
                    char label[16];std::snprintf(label,sizeof(label),"%.1fx",p/100.f);
                    if(ImGui::Button(label,{64,0}))draft=p;
                }
                ImGui::Text("Selected: %u x %u",tenchu::InternalDimension(640,draft),tenchu::InternalDimension(480,draft));
                if(ImGui::Button("Apply & save")){
                    TenchuSetInternalPercent(uint32_t(draft));
                    app::Save();
                    LOGR("Internal resolution: %u x %u (%.2fx), saved",tenchu::InternalDimension(640,g_tenchu_internal_percent),tenchu::InternalDimension(480,g_tenchu_internal_percent),g_tenchu_internal_percent/100.f);
                }
                ImGui::SameLine();if(ImGui::Button("Reset to native"))draft=100;
                ImGui::TextWrapped("Applies live. The game may reduce scene resolution during some effects.");
                ImGui::SeparatorText("Aspect ratio");
                if(ImGui::Checkbox("16:9 widescreen (experimental)",&g_tenchu_widescreen)){
                    tenchu::wide::enabled=g_tenchu_widescreen;
                    auto& settings=app::Get();
                    settings.width=g_tenchu_widescreen?1280:640;
                    settings.height=g_tenchu_widescreen?720:480;
                    app::ApplyDisplay();app::Save();
                    LOGR("Widescreen: %s, presentation %ux%u",g_tenchu_widescreen?"16:9":"4:3",settings.width,settings.height);
                }
                ImGui::TextWrapped("Expands the 3D view horizontally. Keeps screen-space HUD and movie images at their original proportions. Changes the window to 1280 x 720; disabling returns to 640 x 480. Gameplay and effects need testing.");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Cheats")){
                ImGui::SeparatorText("Player cheats");
                bool god=tenchu::cheats::god_mode.load(std::memory_order_relaxed);
                if(ImGui::Checkbox("God mode",&god)){
                    tenchu::cheats::god_mode.store(god,std::memory_order_relaxed);
                    LOGR("Cheats: god mode %s",god?"on":"off");
                }
                ImGui::TextWrapped("Keeps player health full. Toggle during gameplay; takes effect on the next player update. Resets to off when you exit the game.");
                ImGui::BeginDisabled();bool ignore=false;ImGui::Checkbox("Enemies ignore players (unavailable)",&ignore);ImGui::EndDisabled();
                ImGui::TextWrapped("Enemy detection is still being investigated.");
                ImGui::SeparatorText("Multiplayer alone (experimental)");
                bool second=tenchu::cheats::second_player.load();
                if(ImGui::Checkbox("Virtual second controller",&second)){
                    tenchu::cheats::second_player=second;
                    if(!second)tenchu::cheats::control_second=false;
                    LOGR("Cheats: virtual second controller %s",second?"on":"off");
                }
                ImGui::TextWrapped("Controller port 2 reports a pad, so Multiplayer can be chosen with one controller. It sits idle unless you hand it your controller below.");
                ImGui::BeginDisabled(!second);
                if(ImGui::Button(tenchu::cheats::second_start.load()?"Start queued: close F6":"Press Start on controller 2")){
                    tenchu::cheats::second_start=true;
                    LOGR("Cheats: Start queued for controller 2");
                }
                ImGui::TextWrapped("Answers \"press Start on the controller for Player 2\": the press is sent as soon as you close this menu.");
                bool handing=tenchu::cheats::control_second.load();
                if(ImGui::Checkbox("Your controller drives player 2",&handing)){
                    tenchu::cheats::control_second=handing;
                    LOGR("Cheats: controller drives player %d",handing?2:1);
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped("For screens where player 2 has to choose or confirm. Player 1 stands idle meanwhile; untick to take player 1 back.");
                bool single=tenchu::cheats::single_screen.load();
                if(ImGui::Checkbox("Player 1 full screen (not working)",&single)){
                    tenchu::cheats::single_screen=single;
                    LOGR("Cheats: single screen %s",single?"on":"off");
                }
                ImGui::TextWrapped("Not working yet: the game stutters and glitches with it on. Meant to draw only player 1's view over the whole screen.");
                bool story=tenchu::cheats::story_start.load();
                if(ImGui::Checkbox("Player 1 starts at the story start",&story)){
                    tenchu::cheats::story_start=story;
                    LOGR("Cheats: player 1 story start %s",story?"on":"off");
                }
                ImGui::TextWrapped("In multiplayer, player 1 is placed where single player starts that stage (for its character), so the opening cutscene triggers; player 2 keeps its multiplayer start and is brought over after the cutscene. Set before the level loads.");
                bool next=tenchu::cheats::two_players_next.load();
                if(ImGui::Checkbox("Two players in the next mission",&next)){
                    tenchu::cheats::two_players_next=next;
                    // a story mission played alone: player 1 at the story start, seen full screen
                    if(next){tenchu::cheats::second_player=true;tenchu::cheats::story_start=true;}
                    LOGR("Cheats: two players in the next mission %s",next?"armed":"off");
                }
                ImGui::TextWrapped("The next mission that loads creates player 2 as Multiplayer does; unticks itself once it has. Tick it before starting a story mission: it also turns on the virtual second controller and the story start. The mission plays in split screen.");                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Camera")){
                using namespace tenchu::camera;
                ImGui::Checkbox("Remember camera settings",&g_tenchu_remember_camera);
                auto toggle=[](const char* label,std::atomic<bool>& setting){bool v=setting.load();if(ImGui::Checkbox(label,&v)){setting=v;LOGR("Camera setting: %s = %d",label,int(v));}};
                auto slider=[](const char* label,std::atomic<float>& setting,float lo,float hi,const char* format){
                    float v=setting.load();ImGui::SetNextItemWidth(std::max(140.f,ImGui::GetContentRegionAvail().x-165));
                    if(ImGui::SliderFloat(label,&v,lo,hi,format)){setting=v;LOGR("Camera setting: %s = %.3f",label,v);}
                };
                toggle("Photo mode / free camera (F10)",free_requested);
                ImGui::TextWrapped("Left stick move, right stick look. RT/LT up/down, RB/LB fast/slow. Your character stands still; the game keeps running.");
                ImGui::TextUnformatted(free_active.load()?"Free camera active":(available.load()?"Gameplay camera connected":"Waiting for normal gameplay camera"));
                if(ImGui::TreeNode("Stick diagnostics")){
                    input::Pad pad;{std::lock_guard lock(pad_lock);pad=raw_pad;}
                    ImGui::Text("Controller: %s | camera mode: %d | scene frames: %u",pad.connected?"connected":"disconnected",mode.load(),scene_frames.load());
                    ImGui::Text("Left: %d, %d | Right: %d, %d",pad.lx,pad.ly,pad.rx,pad.ry);
                    ImGui::Text("LT: %u | RT: %u",unsigned(pad.analog[6]),unsigned(pad.analog[7]));
                    ImGui::TreePop();
                }
                ImGui::Separator();
                toggle("Custom right-stick camera (experimental)",follow_control);
                ImGui::TextWrapped("Off (default): the game's original right-stick camera. On: the speed sliders drive right-stick look instead; this does not work reliably yet.");
                bool linked=link_profiles.load();
                if(ImGui::Checkbox("Link standing and running sliders",&linked)){
                    link_profiles=linked;SyncLinked();
                }
                ImGui::TextUnformatted(running.load()?"Active profile: moving / running":"Active profile: standing");
                toggle("Invert horizontal",invert_x);toggle("Invert vertical",invert_y);
                slider("Horizontal speed",horizontal,.1f,4.f,"%.2fx");
                slider("Vertical speed",vertical,.1f,4.f,"%.2fx");
                float degrees=custom_fov.load()?fov.load():native_fov.load();
                ImGui::SetNextItemWidth(std::max(140.f,ImGui::GetContentRegionAvail().x-165));
                if(ImGui::SliderFloat("Field of view",&degrees,20,120,"%.0f deg")){fov=degrees;custom_fov=true;}
                slider("Distance",distance,.25f,4.f,"%.2fx");
                SyncLinked();
                ImGui::TextWrapped("Standing camera settings above. Right stick orbits the follow camera; click the right stick to recenter. Close F6 to look around.");
                if(ImGui::TreeNode("Moving / running camera")){
                    ImGui::TextWrapped("Separate controls while the left stick is pushed past 65%%. Includes the game's moving camera modes.");
                    slider("Running horizontal speed",running_horizontal,.1f,4.f,"%.2fx");
                    slider("Running vertical speed",running_vertical,.1f,4.f,"%.2fx");
                    float rf=running_custom_fov.load()?running_fov.load():native_fov.load();
                    ImGui::SetNextItemWidth(std::max(140.f,ImGui::GetContentRegionAvail().x-165));
                    if(ImGui::SliderFloat("Running field of view",&rf,20,120,"%.0f deg")){running_fov=rf;running_custom_fov=true;}
                    slider("Running distance",running_distance,.25f,4.f,"%.2fx");
                    SyncLinked(true);
                    ImGui::TreePop();
                }
                ImGui::TextWrapped("FOV/distance apply live to gameplay. Photo mode keeps the active FOV profile and uses the main speed sliders. Scripted cameras keep their original framing.");
                if(ImGui::Button("Reset camera"))Reset();
                if(ImGui::CollapsingHeader("Keyboard & mouse")){
                    toggle("Enable keyboard & mouse",keyboard_mouse);
                    ImGui::TextWrapped("Free camera: WASD move, Q/E down/up, arrow keys look, hold right mouse button and drag to look. Shift fast, Ctrl slow. Close F6 to move the camera.");
                }
                TenchuSaveCameraIfChanged(ImGui::IsAnyItemActive());
                ImGui::EndTabItem();
            }
            TenchuRenderExtraTabs();
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        if(ImGui::Button("Close (F6)"))open=false;
    }
    ImGui::End();
    if(!open)SetVisible(false);
    ImGui::PopStyleColor(17);ImGui::PopStyleVar(4);
}
