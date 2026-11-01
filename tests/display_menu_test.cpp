#include "cloth_solver.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "app/app.h"
#include "core/log.h"
#include "kernel/disc.h"
#include "graphics/internal_resolution.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <stdexcept>
#include "game_cheats.h"
#include "game_camera.h"
#include "graphics/widescreen.h"
#include "port/port_settings.h"
#include "port/memory_patches.h"
#include "audio/master_volume.h"
#include "graphics/screenshot.h"
#include "graphics/post_processing.h"
#include "kernel/kernel.h"
extern uint32_t g_tenchu_internal_percent;
void TenchuInitializeDisplayMenu();
void TenchuRenderDisplayMenu(double);
bool TenchuDisplayMenuMessage(uint32_t,uintptr_t,intptr_t,intptr_t*);
float TenchuInternalScale(uint32_t);
static unsigned saves=0;static uint32_t saved=0;
uint32_t g_tenchu_renderer=0;
static unsigned restarts=0;
void TenchuInitializeRenderer(){}
uint32_t TenchuActiveRenderer(){return 0;}
bool TenchuVulkanAvailable(){return true;}
void TenchuRequestRestart(){++restarts;}
static unsigned thread_dumps=0;
void TenchuDumpThreads(const char*){++thread_dumps;}
void app::Save(){++saves;saved=g_tenchu_internal_percent;}
app::Settings& app::Get(){static app::Settings settings;return settings;}
void app::ApplyDisplay(){}
std::filesystem::path app::ExeDir(){auto p=std::filesystem::current_path()/"test_artifacts"/"menu_test";std::filesystem::create_directories(p);return p;}
void* app::Window(){return nullptr;}
namespace logx {void Write(Level,const char*,...){} }
uint8_t* kernel::g_base=nullptr;  // memory patches: no game memory in this test
const kernel::Disc* kernel::MountedDisc(){return nullptr;}
const kernel::Disc::Entry* kernel::Disc::Find(const std::vector<std::string>&) const{return nullptr;}
uint32_t kernel::Disc::Read(const Entry&,uint64_t,void*,uint32_t) const{return 0;}
int main(){
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    try{
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={800,600};io.DeltaTime=1.f/60;io.IniFilename=nullptr;
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
        unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
        g_tenchu_internal_percent=100;TenchuInitializeDisplayMenu();
        intptr_t result=0;
        auto frame=[&]{ImGui::NewFrame();TenchuRenderDisplayMenu(60);ImGui::Render();};
        frame();check(ImGui::GetDrawData()->TotalVtxCount==0,"menu should start closed");
        check(!tenchu::patches::MenuOpen(),"closed menu must release one-shot queue");
        check(!(io.ConfigFlags&(ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad)),"F6 menu must disable keyboard/controller navigation");
        check(TenchuDisplayMenuMessage(WM_KEYDOWN,VK_F6,0,&result),"F6 must be handled");
        frame();frame();check(ImGui::GetDrawData()->TotalVtxCount>0,"F6 must show menu");
        check(tenchu::patches::MenuOpen(),"F6 must defer one-shot patches while open");
        ImGui::SetWindowSize(ImGui::FindWindowByName("Tenchu: Return from Darkness (F6)"),{780,570});frame();frame();
        auto* nav_window=ImGui::FindWindowByName("Tenchu: Return from Darkness (F6)");
        check((nav_window->Flags&ImGuiWindowFlags_NoNavInputs)&&(nav_window->Flags&ImGuiWindowFlags_NoNavFocus),"menu window must reject navigation input/focus");
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown,true);io.AddKeyEvent(ImGuiKey_GamepadDpadRight,true);frame();frame();
        check(g_tenchu_internal_percent==100&&g_tenchu_renderer==0&&restarts==0,"gamepad input must not activate menu controls");
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown,false);io.AddKeyEvent(ImGuiKey_GamepadDpadRight,false);frame();
        auto activate=[&](const char* label){
            auto* window=ImGui::FindWindowByName("Tenchu: Return from Darkness (F6)");
            check(window!=nullptr,"menu window missing");
            auto* tabs=GImGui->TabBars.GetByKey(window->GetID("Display settings"));
            check(tabs&&tabs->Tabs.Size==13,"menu categories missing");
            ImGui::ActivateItemByID(ImHashStr(label,0,tabs->Tabs[0].ID));frame();
        };
        activate("Restart game");check(restarts==1,"restart button must queue one game restart");
        saves=0;
        activate("2.0x");check(TenchuInternalScale(480)==1,"selection must wait for Apply");
        activate("Apply & save");
        check(TenchuInternalScale(480)==2&&saved==200&&saves==1,"2x button must apply live and save");
        check(tenchu::InternalDimension(640,200)==1280&&tenchu::InternalDimension(480,200)==960,"2x dimensions incorrect");
        activate("Reset to native");activate("Apply & save");
        check(TenchuInternalScale(480)==1&&saved==100&&saves==2,"native reset must apply and save");
        auto* display_window=ImGui::FindWindowByName("Tenchu: Return from Darkness (F6)");
        ImGui::SetScrollY(display_window,display_window->ScrollMax.y);frame();frame();
        activate("16:9 widescreen (experimental)");
        check(tenchu::wide::enabled&&app::Get().width==1280&&app::Get().height==720,"widescreen toggle must enable aspect correction and resize presentation");
        activate("16:9 widescreen (experimental)");
        check(!tenchu::wide::enabled&&app::Get().width==640&&app::Get().height==480,"disabling widescreen must restore native presentation");
        ImGui::SetScrollY(display_window,0);frame();frame();
        auto* window=ImGui::FindWindowByName("Tenchu: Return from Darkness (F6)");
        auto* tabs=GImGui->TabBars.GetByKey(window->GetID("Display settings"));
        tabs->NextSelectedTabId=tabs->Tabs[1].ID;frame();frame();
        ImGui::ActivateItemByID(ImHashStr("God mode",0,tabs->Tabs[1].ID));frame();
        check(tenchu::cheats::god_mode.load(),"Cheats checkbox must enable god mode");
        ImGui::ActivateItemByID(ImHashStr("God mode",0,tabs->Tabs[1].ID));frame();
        check(!tenchu::cheats::god_mode.load(),"Cheats checkbox must disable god mode");
        tabs->NextSelectedTabId=tabs->Tabs[2].ID;frame();frame();
        ImGui::ActivateItemByID(ImHashStr("Photo mode / free camera (F10)",0,tabs->Tabs[2].ID));frame();
        check(tenchu::camera::free_requested.load(),"Camera checkbox must enable free camera");
        TenchuDisplayMenuMessage(WM_SYSKEYDOWN,VK_F10,0,&result);
        check(!tenchu::camera::free_requested.load(),"F10 system key must toggle free camera");
        TenchuDisplayMenuMessage(WM_KEYDOWN,VK_F10,0,&result);
        TenchuDisplayMenuMessage(WM_KEYDOWN,VK_F10,intptr_t(1)<<30,&result);
        check(tenchu::camera::free_requested.load(),"held F10 must not repeatedly toggle");
        tenchu::camera::horizontal=2;tenchu::camera::running_horizontal=.5f;
        ImGui::ActivateItemByID(ImHashStr("Link standing and running sliders",0,tabs->Tabs[2].ID));frame();
        check(tenchu::camera::link_profiles&&tenchu::camera::running_horizontal==2,"link checkbox must synchronize profile sliders");
        tenchu::camera::distance=2;tenchu::camera::custom_fov=true;
        ImGui::SetScrollY(window,window->ScrollMax.y);frame();frame();
        ImGui::ActivateItemByID(ImHashStr("Reset camera",0,tabs->Tabs[2].ID));frame();
        check(!tenchu::camera::free_requested&&tenchu::camera::distance==1&&!tenchu::camera::custom_fov,"Reset camera button must restore camera defaults");
        auto tab=[&](int index){tabs->NextSelectedTabId=tabs->Tabs[index].ID;ImGui::SetScrollY(window,0);frame();frame();};
        auto click=[&](int index,const char* label){ImGui::ActivateItemByID(ImHashStr(label,0,tabs->Tabs[index].ID));frame();};
        tab(3);click(3,"Mute");check(g_tenchu_mute&&tenchu::audio::master_gain==0,"Audio mute must reach real mixer gain");
        click(3,"Mute");check(!g_tenchu_mute&&tenchu::audio::master_gain==1,"unmute must restore master gain");
        tab(4);TenchuSetInternalPercent(200);g_tenchu_renderer=1;
        click(4,"Save current graphics");TenchuSetInternalPercent(100);g_tenchu_renderer=0;
        click(4,"Load selected profile");check(g_tenchu_internal_percent==200&&g_tenchu_renderer==1,"profile UI must save/load actual graphics values");
        tab(5);click(5,"Show FPS and frame time");check(g_tenchu_performance_overlay,"performance checkbox must enable overlay");
        click(5,"Write thread dump (Ctrl+F3)");check(thread_dumps==1,"thread dump button must write a dump");
        click(5,"F3 render capture (debug)");check(g_tenchu_debug_capture,"debug capture checkbox must enable F3 captures");
        click(5,"Take screenshot (F12)");
        check(!tenchu::patches::MenuOpen(),"screenshot menu closure must release queued one-shots");
        check(tenchu::shots::delay==2,"screenshot button must queue delayed readback");
        check(!TenchuDisplayMenuMessage(WM_KEYDOWN,VK_ESCAPE,0,&result),"screenshot must hide F6 before capture");
        check(!tenchu::shots::NextFrame()&&!tenchu::shots::NextFrame()&&tenchu::shots::NextFrame(),"capture must wait until menu has disappeared");tenchu::shots::saving=false;
        frame();check(ImGui::GetDrawData()->TotalVtxCount>0,"performance overlay must remain visible with F6 closed");
        TenchuDisplayMenuMessage(WM_KEYDOWN,VK_F6,0,&result);frame();frame();tab(6);
        click(6,"Enable shader preset");check(tenchu::post::enabled,"shader toggle must enable rendering controls");
        click(6,"Save shader preset");check(g_tenchu_shader_preset=="My shader"&&!TenchuShaderPresets().empty(),"shader save button must write named preset");
        click(6,"Split-screen compare");check(tenchu::post::compare,"split compare checkbox must reach renderer");
        tenchu::post::Preset(4);check(tenchu::post::values[8]>0&&tenchu::post::values[9]>0,"Modern Look must configure expanded effects");
        click(6,"Reset shader");check(!tenchu::post::enabled&&!tenchu::post::compare&&tenchu::post::values[3]==1,"shader reset must restore original image");
        tab(7);app::Get().custom_textures=true;click(7,"Replace textures");check(!app::Get().custom_textures,"Textures tab must switch replacement off");
        click(7,"Replace textures");check(app::Get().custom_textures,"Textures tab must switch replacement back on");
        tab(8);click(8,"Write disc file list");
        tab(9);click(9,"Refresh levels");
        tab(10);click(10,"Reload");
        tab(12);click(12,"Enhanced cloth solver (experimental)");
        check(tenchu::cloth::enhanced,"Physics checkbox must enable the solver");
        click(12,"Save physics preset");check(g_tenchu_physics_preset=="My physics"&&!TenchuPhysicsPresets().empty(),"physics save button must write a named preset");
        tenchu::cloth::response_speed=.3f;click(12,"Load physics preset");
        check(tenchu::cloth::response_speed==.6f,"physics load button must restore saved controls");
        click(12,"Cloth body collision");check(!tenchu::cloth::body_collision,"body collision checkbox must disable blockers");
        click(12,"Cloth body collision");check(tenchu::cloth::body_collision,"body collision checkbox must enable blockers");
        click(12,"Cloth edge collision");check(!tenchu::cloth::edge_collision,"edge collision checkbox must disable edge contacts");
        click(12,"Cloth edge collision");check(tenchu::cloth::edge_collision,"edge collision checkbox must enable edge contacts");
        ImGui::SetScrollY(window,window->ScrollMax.y);frame();frame();
        click(12,"Cloth self-collision");check(tenchu::cloth::self_collision,"self-collision checkbox must enable separation");
        ImGui::SetScrollY(window,0);frame();frame();
        click(12,"Enhanced cloth solver (experimental)");
        check(!tenchu::cloth::enhanced,"Physics checkbox must restore original solver");
        click(12,"Enhanced cloth solver (experimental)");
        tab(11);
        tenchu::cheats::god_mode=true;click(11,"Restore default settings");
        check(!g_tenchu_performance_overlay&&!g_tenchu_debug_capture&&!g_tenchu_mute&&g_tenchu_master_volume==100&&!tenchu::cheats::god_mode&&g_tenchu_renderer==0&&g_tenchu_internal_percent==100,"System reset must restore port defaults and disable cheats");
        check(!TenchuGraphicsProfiles().empty(),"reset must retain saved graphics profiles");
        check(!tenchu::cloth::enhanced,"System reset must restore original cloth solver");
        TenchuDisplayMenuMessage(WM_KEYDOWN,VK_F6,intptr_t(1)<<30,&result);frame();
        check(ImGui::GetDrawData()->TotalVtxCount>0,"held F6 must not repeatedly toggle");
        TenchuDisplayMenuMessage(WM_KEYDOWN,VK_ESCAPE,0,&result);frame();
        check(ImGui::GetDrawData()->TotalVtxCount==0,"Escape must close menu");
        g_tenchu_internal_percent=200;TenchuInitializeDisplayMenu();check(TenchuInternalScale(480)==2,"saved scale must initialize renderer");
        g_tenchu_internal_percent=99999;TenchuInitializeDisplayMenu();check(TenchuInternalScale(480)==4,"loaded scale must clamp safely");
        ImGui::DestroyContext();std::puts("PASS: F6 menu, real ImGui button activation, live scale, save/reset, repeat guard and restore");
    }catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}
}
