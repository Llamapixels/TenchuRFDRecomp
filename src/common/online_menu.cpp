#include "port/online.h"
#include "port/online_router.h"
#include "app/app.h"
#include <imgui.h>
#include <cstdio>
#include <string>

std::string TenchuOnlineStatus();
bool TenchuOnlineInUse();
namespace {
void SaveOnline(){tenchu::online::Save(app::ExeDir()/"online.toml");}
void JoinHost(const std::string& text){
    using namespace tenchu::online;
    std::string ip;if(!ResolveHost(text,ip))return;
    auto s=Snapshot();s.join=ip;Configure(s);enabled=true;RememberHost(text);SaveOnline();
}
void CodeRow(const char* label,const std::string& ip){
    const auto code=tenchu::online::JoinCode(ip);
    ImGui::PushID(label);
    ImGui::Text("%s: %s",label,code.c_str());ImGui::SameLine();
    if(ImGui::SmallButton("Copy"))ImGui::SetClipboardText(code.c_str());
    ImGui::TextDisabled("%s",ip.c_str());
    ImGui::PopID();
}
}
void TenchuRenderOnlineTab(){
    using namespace tenchu::online;
    if(!ImGui::BeginTabItem("Online"))return;
    const bool in_use=TenchuOnlineInUse();
    bool on=enabled.load();
    ImGui::BeginDisabled(in_use);
    if(ImGui::Checkbox("Enable experimental online play",&on)&&!in_use){
        enabled=on;
        if(!on){router::Stop();auto s=Snapshot();s.router_setup=false;Configure(s);}
        SaveOnline();
    }
    ImGui::EndDisabled();
    static char name[32]{},host[128]{},bind[64]{};
    static int port=47626;static bool loaded=false;
    if(!loaded){const auto s=Snapshot();std::snprintf(name,sizeof(name),"%s",s.name.c_str());std::snprintf(host,sizeof(host),"%s",s.join.c_str());std::snprintf(bind,sizeof(bind),"%s",s.bind_ip.c_str());port=s.port;loaded=true;}
    ImGui::BeginDisabled(in_use);
    if(ImGui::InputText("Player name",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue)&&ValidName(name)&&!in_use){
        auto s=Snapshot();s.name=name;Configure(s);SaveOnline();
    }
    ImGui::SameLine();ImGui::BeginDisabled(!ValidName(name));
    if(ImGui::Button("Save name")&&ValidName(name)&&!in_use){auto s=Snapshot();s.name=name;Configure(s);SaveOnline();}
    ImGui::EndDisabled();ImGui::EndDisabled();
    ImGui::TextWrapped("Share a code with a friend, then use the game's Xbox Live menus. No Xbox Live account is required.");

    ImGui::SeparatorText("Host a game");
    auto settings=Snapshot();const auto status=router::Snapshot();
    const int instance=app::Instance();
    if(instance){
        CodeRow("Same-PC join code","127.0.0."+std::to_string(instance));
        ImGui::TextWrapped("This test copy connects only to another instance on this PC. Use the normal game executable for other PCs.");
    }else if(!settings.bind_ip.empty()){
        CodeRow("Network join code",settings.bind_ip);
    }else{
        if(!status.public_ip.empty())CodeRow("Your join code",status.public_ip);
        ImGui::BeginDisabled(status.state==router::State::Working);
        if(ImGui::Button(status.state==router::State::Off?"Get my join code":"Refresh my join code")){
            enabled=true;auto s=Snapshot();s.router_setup=true;Configure(s);SaveOnline();router::Start();
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("Finds your internet address and asks your router to open the game's ports (UPnP).");
        if(!status.message.empty())ImGui::TextWrapped("%s",status.message.c_str());
        if(settings.router_setup){
            if(ImGui::Button("Stop automatic router setup")){router::Stop();auto s=Snapshot();s.router_setup=false;Configure(s);SaveOnline();}
        }
    }
    if(ImGui::Button("Use this PC as host")){
        enabled=true;auto s=Snapshot();s.join.clear();Configure(s);SaveOnline();host[0]=0;
    }
    ImGui::TextWrapped("Create a match in Xbox Live, share your code, then accept your friend's join request.");

    ImGui::SeparatorText("Join a friend");
    const bool enter=ImGui::InputTextWithHint("Host code","XXXX-XXXX or IPv4 address",host,sizeof(host),ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if(ImGui::Button("Paste")){const char* text=ImGui::GetClipboardText();if(text)std::snprintf(host,sizeof(host),"%s",text);}
    std::string address;const bool valid=ResolveHost(host,address);
    ImGui::BeginDisabled(!valid);
    if(ImGui::Button("Join")|| (enter&&valid))JoinHost(host);
    ImGui::EndDisabled();
    if(host[0]&&!valid)ImGui::TextWrapped("Enter a valid eight-character code or IPv4 address.");
    settings=Snapshot();
    if(!settings.join.empty()){
        std::string target;ResolveHost(settings.join,target);
        ImGui::Text("Selected host: %s (%s)",JoinCode(target).c_str(),target.c_str());
        ImGui::SameLine();if(ImGui::SmallButton("Clear")){settings.join.clear();Configure(settings);SaveOnline();}
    }
    ImGui::TextWrapped("After selecting a host, use Quick Match or OptiMatch in Xbox Live to find their match.");
    if(!settings.recent.empty()&&ImGui::TreeNode("Recent hosts")){
        for(const auto& recent:settings.recent){
            ImGui::PushID(recent.host.c_str());
            ImGui::Text("%s  %s",recent.host.c_str(),recent.date.c_str());ImGui::SameLine();
            if(ImGui::SmallButton("Join")){JoinHost(recent.host);std::snprintf(host,sizeof(host),"%s",recent.host.c_str());}
            ImGui::SameLine();if(ImGui::SmallButton("Copy"))ImGui::SetClipboardText(recent.host.c_str());
            ImGui::SameLine();if(ImGui::SmallButton("Remove")){ForgetHost(recent.host);SaveOnline();}
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if(ImGui::CollapsingHeader("LAN and advanced settings")){
        static auto addresses=LocalAddresses();
        const auto runtime=RuntimeAddress();
        if(!runtime.empty())CodeRow("Current network code",runtime);
        if(ImGui::Button("Refresh local addresses"))addresses=LocalAddresses();
        for(const auto& ip:addresses){ImGui::PushID(ip.c_str());CodeRow("LAN code",ip);ImGui::PopID();}
        ImGui::TextWrapped("Use a LAN code on the same network, or a virtual LAN address with your VPN. Public codes need reachable ports; CGNAT may require a virtual LAN.");
        const bool locked=in_use||status.state!=router::State::Off;
        ImGui::BeginDisabled(locked);
        ImGui::InputInt("Session directory port",&port);
        ImGui::InputTextWithHint("Network address","Automatic (restart after changing)",bind,sizeof(bind));
        const bool valid_settings=port>0&&port<65536&&port!=1000&&port!=1001&&ValidBindAddress(bind);
        ImGui::BeginDisabled(!valid_settings);
        if(ImGui::Button("Save advanced settings")&&valid_settings&&!locked){auto s=Snapshot();s.port=uint16_t(port);s.bind_ip=bind;Configure(s);SaveOnline();}
        ImGui::EndDisabled();ImGui::EndDisabled();
        if(!valid_settings)ImGui::TextWrapped("Choose a valid IPv4 address (or leave blank), and a directory port from 1-65535 other than 1000/1001.");
        ImGui::TextWrapped("Both players must use the same directory port. Restart after changing the network address or port. Stop automatic router setup before editing these settings.");
        ImGui::TextWrapped("Manual forwarding: UDP 1000, 1001 and %u; TCP 1002. Allow TenchuRFD through Windows Firewall. There is no relay service.",unsigned(settings.port));
    }
    if(in_use)ImGui::TextWrapped("Restart to change your player name or disable online play.");
    ImGui::TextWrapped("Status: %s",TenchuOnlineStatus().c_str());
    ImGui::EndTabItem();
}
