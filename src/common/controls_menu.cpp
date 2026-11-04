#include "game_controls.h"
#include "app/app.h"
#include <imgui.h>

namespace tenchu::controls {
void RenderMenu(){
    if(!ImGui::BeginTabItem("Controls"))return;
    input::Poll(0); // Keep binding capture alive while the game is paused by F6.
    auto s=Snapshot();bool changed=false;
    changed|=ImGui::Checkbox("Keyboard and mouse gameplay",&s.keyboard);
    changed|=ImGui::Checkbox("Mouse look (capture during gameplay)",&s.mouse);
    bool edited=false;
    auto slider=[&](const char* name,float& value,float lo,float hi,const char* fmt){bool c=ImGui::SliderFloat(name,&value,lo,hi,fmt);edited|=c;changed|=ImGui::IsItemDeactivatedAfterEdit();};
    slider("Mouse sensitivity",s.mouse_speed,.1f,4,"%.2fx");slider("Aiming sensitivity",s.aim_speed,.1f,4,"%.2fx");
    slider("Right-stick deadzone",s.deadzone,0,.5f,"%.2f");slider("Keyboard walking speed",s.walk_speed,.1f,1,"%.2fx");
    ImGui::TextWrapped("Esc releases the mouse; click the game to capture it again. F6 releases it for this menu. Bindings below send the listed Xbox input; alternate in-game control schemes can change its action.");
    static int listening=-1;static bool armed=false;static int device=0;
    auto extra=[&](int row)->int&{return row==Count?s.walk_key:(row==Count+1?s.confirm_key:s.cancel_key);};
    if(ImGui::RadioButton("Keyboard / mouse",&device,0))listening=-1;
    ImGui::SameLine();if(ImGui::RadioButton("Controller",&device,1))listening=-1;
    if(ImGui::Button("Reset all bindings")){s=Settings{};listening=-1;changed=true;}
    if(listening>=0){
        ImGui::TextUnformatted(armed?"Press a key or controller input. Esc cancels; Backspace clears.":"Release the controls, then press the new binding.");
        auto held=HeldKeys();auto pad=Values(PhysicalPad());bool any=false;
        for(int i=1;i<256;++i)any|=held[i];for(float v:pad)any|=v>.55f;
        if(!any)armed=true;
        if(armed){
            if(held[0x1B])listening=-1;
            else if(held[0x08]){if(listening>=Count)extra(listening)=0;else if(device==0)s.keyboard_bindings[listening]=0;else s.controller_bindings[listening]=-1;listening=-1;changed=true;}
            else if(device==0||listening>=Count){
                for(int i=1;i<256;++i)if(held[i]&&!ReservedKey(i)){
                    if(listening>=Count)extra(listening)=i;else s.keyboard_bindings[listening]=i;
                    listening=-1;changed=true;break;
                }
            }else for(int i=0;i<Count;++i)if(pad[i]>.55f){s.controller_bindings[listening]=i;listening=-1;changed=true;break;}
        }
    }
    if(ImGui::BeginTable("Bindings",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY|ImGuiTableFlags_BordersInnerV,{0,230})){
        ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthStretch);ImGui::TableSetupColumn("Binding",ImGuiTableColumnFlags_WidthFixed,155);ImGui::TableSetupColumn("",ImGuiTableColumnFlags_WidthFixed,55);
        ImGui::TableHeadersRow();
        for(int i=0;i<Count+3;++i){
            if(i>=Count&&device==1)continue;
            ImGui::PushID(i);ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(i==Count?"Walk modifier":(i==Count+1?"Menu confirm":(i==Count+2?"Menu back":labels[i])));ImGui::TableNextColumn();
            int binding=i>=Count?extra(i):(device==0?s.keyboard_bindings[i]:s.controller_bindings[i]);
            std::string name=device==0||i>=Count?KeyName(binding):(binding<0?"Unbound":sources[binding]);
            if(ImGui::Button(listening==i?"Waiting...":name.c_str(),{-1,0})){listening=i;armed=false;}
            ImGui::TableNextColumn();if(ImGui::SmallButton("Clear")){if(i>=Count)extra(i)=0;else if(device==0)s.keyboard_bindings[i]=0;else s.controller_bindings[i]=-1;changed=true;}
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if(edited||changed)Configure(s);
    if(changed)app::Save();
    ImGui::TextWrapped("Click a binding, release the mouse, then press its replacement. Shared bindings trigger every assigned action. F3, F6, F10 and F12 are reserved. Camera speed and inversion are in Camera.");
    ImGui::EndTabItem();
}
}
