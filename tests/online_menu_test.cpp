#include "port/online.h"
#include "port/online_router.h"
#include "app/app.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
void TenchuRenderOnlineTab();
namespace {int instance=0,starts=0,stops=0;bool in_use=false;std::string clipboard;tenchu::online::router::Status status;}
std::filesystem::path app::ExeDir(){static auto path=std::filesystem::current_path()/"test_artifacts"/"online_menu"/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directories(path);return path;}
int app::Instance(){return instance;}
bool TenchuOnlineInUse(){return in_use;}
std::string TenchuOnlineStatus(){return "Fixture";}
namespace tenchu::online::router {
void Start(){++starts;status.state=State::Open;status.public_ip="203.0.113.7";status.ports_open=true;status.message="Mock router ready";}
void Stop(){++stops;status={};}
Status Snapshot(){return status;}
}
int main(){try{
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    using namespace tenchu::online;
    Settings settings;settings.name="Player";Configure(settings);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={1300,1800};io.DeltaTime=1.f/60;io.IniFilename=nullptr;
    io.GetClipboardTextFn=[](void*){return clipboard.c_str();};io.SetClipboardTextFn=[](void*,const char* text){clipboard=text;};
    unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    auto frame=[](){ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1250,1750});ImGui::Begin("Online test");if(ImGui::BeginTabBar("Tabs")){TenchuRenderOnlineTab();ImGui::EndTabBar();}ImGui::End();ImGui::Render();};
    frame();frame();auto* window=ImGui::FindWindowByName("Online test");auto* tabs=GImGui->TabBars.GetByKey(window->GetID("Tabs"));
    check(tabs&&tabs->Tabs.Size==1,"Online tab exists");const auto seed=tabs->Tabs[0].ID;
    auto activate=[&](const char* label){ImGui::ActivateItemByID(ImHashStr(label,0,seed));frame();};
    clipboard=JoinCode("127.0.0.1");activate("Paste");activate("Join");
    check(enabled&&Snapshot().join=="127.0.0.1"&&Snapshot().recent.size()==1,"Paste + Join updates actual network target and recent hosts");
    Configure({});Load(app::ExeDir()/"online.toml");check(Snapshot().join=="127.0.0.1"&&Snapshot().recent.size()==1,"Join persists");
    activate("Use this PC as host");check(Snapshot().join.empty(),"Host clears previous join target");
    clipboard="BAD-CODE";activate("Paste");activate("Join");check(Snapshot().join.empty(),"Invalid code cannot join");
    activate("Get my join code");check(starts==1&&Snapshot().router_setup,"Get code explicitly opts in and starts router setup");frame();
    const auto copy_id=ImHashStr("Copy",0,ImHashStr("Your join code",0,seed));ImGui::ActivateItemByID(copy_id);frame();
    check(clipboard==JoinCode("203.0.113.7"),"Copy returns public code");
    activate("Stop automatic router setup");check(stops==1&&!Snapshot().router_setup,"Stop disables saved router opt-in");
    in_use=true;activate("Enable experimental online play");check(enabled,"Active online profile cannot be disabled");in_use=false;
    instance=1;frame();activate("Get my join code");check(starts==1,"Same-PC instance cannot map real router");
    activate("LAN and advanced settings");frame();check(ImGui::GetDrawData()->TotalVtxCount>0,"Advanced UI renders");
    ImGui::DestroyContext();std::puts("PASS: real ImGui Paste/Join, persistence, host reset, invalid input, Get/Copy/Stop, in-use and loopback guards.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
