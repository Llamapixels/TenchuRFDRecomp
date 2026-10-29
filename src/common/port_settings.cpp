#include "port/port_settings.h"
#include "app/app.h"
#include "audio/master_volume.h"
#include "game_camera.h"
#include "game_cheats.h"
#include "graphics/widescreen.h"
#include "graphics/post_processing.h"
#include "graphics/cutscene_motion_blur.h"
#include "graphics/renderer_selection.h"
#include "graphics/texture_packs.h"
#include "port/mods.h"
#include "port/memory_patches.h"
#include <toml++/toml.hpp>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

uint32_t g_tenchu_master_volume=100;
std::string g_tenchu_shader_preset;
bool g_tenchu_shader_modified=false;
bool g_tenchu_mute=false,g_tenchu_performance_overlay=false,g_tenchu_remember_camera=true,g_tenchu_debug_capture=false;
extern uint32_t g_tenchu_internal_percent;
extern bool g_tenchu_widescreen;
namespace {
using namespace tenchu::camera;
struct CameraSettings {
    std::array<float,8> values;
    std::array<bool,7> flags;
    bool operator==(const CameraSettings&)const=default;
};
CameraSettings Snapshot(){
    return {{horizontal.load(),vertical.load(),fov.load(),distance.load(),running_horizontal.load(),running_vertical.load(),running_fov.load(),running_distance.load()},
        {invert_x.load(),invert_y.load(),custom_fov.load(),running_custom_fov.load(),link_profiles.load(),follow_control.load(),keyboard_mouse.load()}};
}
CameraSettings previous=Snapshot();bool previous_remember=true;
float Number(const toml::table& t,const char* key,float fallback,float low,float high){
    double value=t[key].value_or(double(fallback));
    return std::isfinite(value)?std::clamp(float(value),low,high):fallback;
}
toml::table ShaderSettings(){
    using namespace tenchu::post;
    toml::table shader{{"enabled",enabled.load()},{"split_compare",compare.load()},{"split_position",divider.load()}};
    for(size_t i=0;i<count;++i)shader.insert_or_assign(keys[i],values[i].load());
    return shader;
}
void ApplyShader(const toml::table& shader){
    using namespace tenchu::post;
    enabled=shader["enabled"].value_or(false);compare=shader["split_compare"].value_or(false);
    divider=Number(shader,"split_position",.5f,.05f,.95f);
    for(size_t i=0;i<count;++i)values[i]=Number(shader,keys[i],defaults[i],minimum[i],maximum[i]);
}
std::filesystem::path ShaderPresetsFile(){return app::ExeDir()/"shader_presets.toml";}
toml::table ReadShaderPresets(){
    return std::filesystem::exists(ShaderPresetsFile())?toml::parse_file(ShaderPresetsFile().string()):toml::table{};
}
std::filesystem::path ProfilesFile(){return app::ExeDir()/"graphics_profiles.toml";}
toml::table ReadProfiles(){
    if(!std::filesystem::exists(ProfilesFile()))return {};
    return toml::parse_file(ProfilesFile().string());
}
void ApplyGraphics(const toml::table& p){
    auto& s=app::Get();
    g_tenchu_renderer=std::min(p["renderer"].value_or(0u),1u);
    g_tenchu_widescreen=p["widescreen"].value_or(false);tenchu::wide::enabled=g_tenchu_widescreen;
    TenchuSetInternalPercent(p["internal_resolution_percent"].value_or(100u));
    s.display_mode=app::DisplayMode(std::min(p["display_mode"].value_or(2u),2u));
    auto width=p["width"].value_or(640u),height=p["height"].value_or(480u);
    s.width=width?std::clamp(width,320u,7680u):0;
    s.height=height?std::clamp(height,240u,4320u):0;
    s.vsync=p["vsync"].value_or(true);s.fps_cap=std::min(p["fps_cap"].value_or(60u),360u);
    app::ApplyDisplay();app::Save();
}
}
void TenchuUpdateMasterVolume(){
    g_tenchu_master_volume=std::min(g_tenchu_master_volume,100u);
    tenchu::audio::SetMaster(g_tenchu_master_volume,g_tenchu_mute);
}
void TenchuInitializePortSettings(){TenchuUpdateMasterVolume();tenchu::packs::Rescan();tenchu::mods::Rescan();previous=Snapshot();previous_remember=g_tenchu_remember_camera;}
void TenchuLoadPortSettings(const void* value){
    const auto& t=*static_cast<const toml::table*>(value);
    if(auto* shader=t["shader"].as_table()){
        ApplyShader(*shader);
        g_tenchu_shader_preset=(*shader)["last_preset"].value_or(std::string{});
        g_tenchu_shader_modified=(*shader)["preset_modified"].value_or(false);
    }
    tenchu::motion::original_blur=t["cutscene"]["original_motion_blur"].value_or(false);
    tenchu::packs::Load(&t);
    tenchu::mods::Load(&t);
    tenchu::patches::Load(&t);
    const auto* p=t["camera"].as_table();
    if(!g_tenchu_remember_camera||!p)return;
    horizontal=Number(*p,"horizontal_speed",1,.1f,4);vertical=Number(*p,"vertical_speed",1,.1f,4);
    fov=Number(*p,"fov",48,20,120);distance=Number(*p,"distance",1,.25f,4);
    running_horizontal=Number(*p,"running_horizontal_speed",1,.1f,4);running_vertical=Number(*p,"running_vertical_speed",1,.1f,4);
    running_fov=Number(*p,"running_fov",48,20,120);running_distance=Number(*p,"running_distance",1,.25f,4);
    invert_x=(*p)["invert_horizontal"].value_or(false);invert_y=(*p)["invert_vertical"].value_or(false);
    custom_fov=(*p)["custom_fov"].value_or(false);running_custom_fov=(*p)["running_custom_fov"].value_or(false);
    link_profiles=(*p)["linked"].value_or(false);follow_control=(*p)["custom_follow"].value_or(false);keyboard_mouse=(*p)["keyboard_mouse"].value_or(true);
    SyncLinked();
}
void TenchuSavePortSettings(void* value){
    auto& t=*static_cast<toml::table*>(value);
    t.insert_or_assign("cutscene",toml::table{{"original_motion_blur",tenchu::motion::original_blur.load()}});
    tenchu::packs::Save(&t);
    tenchu::mods::Save(&t);
    tenchu::patches::Save(&t);
    auto shader=ShaderSettings();
    shader.insert_or_assign("last_preset",g_tenchu_shader_preset);
    shader.insert_or_assign("preset_modified",g_tenchu_shader_modified);
    t.insert_or_assign("shader",std::move(shader));
    if(!g_tenchu_remember_camera)return;
    auto c=Snapshot();
    t.insert_or_assign("camera",toml::table{
        {"horizontal_speed",c.values[0]},{"vertical_speed",c.values[1]},{"fov",c.values[2]},{"distance",c.values[3]},
        {"running_horizontal_speed",c.values[4]},{"running_vertical_speed",c.values[5]},{"running_fov",c.values[6]},{"running_distance",c.values[7]},
        {"invert_horizontal",c.flags[0]},{"invert_vertical",c.flags[1]},{"custom_fov",c.flags[2]},{"running_custom_fov",c.flags[3]},
        {"linked",c.flags[4]},{"custom_follow",c.flags[5]},{"keyboard_mouse",c.flags[6]}});
}
void TenchuSaveCameraIfChanged(bool editing){
    if(editing)return;
    auto current=Snapshot();
    bool dirty=previous_remember!=g_tenchu_remember_camera||(g_tenchu_remember_camera&&current!=previous);
    previous=current;previous_remember=g_tenchu_remember_camera;
    if(dirty)app::Save();
}
std::vector<std::string> TenchuGraphicsProfiles(){
    std::vector<std::string> names;
    try{auto file=ReadProfiles();if(auto* profiles=file["profiles"].as_table())for(auto&& [key,value]:*profiles)if(value.is_table())names.emplace_back(key.str());}catch(...){}
    return names;
}
bool TenchuSaveGraphicsProfile(const std::string& name,std::string& error){
    if(name.empty()||name.size()>64||name.find_first_of("\r\n\t")!=std::string::npos){error="Enter a profile name (1-64 characters).";return false;}
    try{
        auto file=ReadProfiles();if(!file["profiles"].is_table())file.insert_or_assign("profiles",toml::table{});
        const auto& s=app::Get();
        uint32_t width=s.width,height=s.height;
        if(s.display_mode==app::DisplayMode::Windowed){
            RECT rect{};auto window=static_cast<HWND>(app::Window());
            if(window&&!IsIconic(window)&&GetClientRect(window,&rect)&&rect.right>rect.left&&rect.bottom>rect.top){width=uint32_t(rect.right-rect.left);height=uint32_t(rect.bottom-rect.top);}
        }
        file["profiles"].as_table()->insert_or_assign(name,toml::table{
            {"renderer",int64_t(g_tenchu_renderer)},{"widescreen",g_tenchu_widescreen},{"internal_resolution_percent",int64_t(g_tenchu_internal_percent)},
            {"display_mode",int64_t(s.display_mode)},{"width",int64_t(width)},{"height",int64_t(height)},
            {"vsync",s.vsync},{"fps_cap",int64_t(s.fps_cap)}});
        auto temp=ProfilesFile();temp+=L".tmp";
        {std::ofstream out(temp);out<<file;out.close();if(!out)throw std::runtime_error("Cannot write profiles file.");}
        if(!MoveFileExW(temp.c_str(),ProfilesFile().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace profiles file.");
        error="Profile saved.";return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool TenchuLoadGraphicsProfile(const std::string& name,std::string& error){
    try{auto file=ReadProfiles();auto* profiles=file["profiles"].as_table();auto* p=profiles?profiles->get_as<toml::table>(name):nullptr;
        if(!p){error="Choose a saved profile.";return false;}ApplyGraphics(*p);error="Profile loaded. Restart if you changed renderer.";return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
std::vector<std::string> TenchuShaderPresets(){
    std::vector<std::string> names;
    try{auto file=ReadShaderPresets();if(auto* presets=file["presets"].as_table())for(auto&& [key,value]:*presets)if(value.is_table())names.emplace_back(key.str());}catch(...){}
    return names;
}
bool TenchuSaveShaderPreset(const std::string& name,std::string& status){
    if(name.empty()||name.size()>64||name.find_first_not_of(' ')==std::string::npos||name.find_first_of("\r\n\t")!=std::string::npos){status="Enter a preset name (1-64 characters).";return false;}
    try{
        auto file=ReadShaderPresets();if(!file["presets"].is_table())file.insert_or_assign("presets",toml::table{});
        file["presets"].as_table()->insert_or_assign(name,ShaderSettings());
        auto temp=ShaderPresetsFile();temp+=L".tmp";
        {std::ofstream out(temp);out<<file;out.close();if(!out)throw std::runtime_error("Cannot write shader presets.");}
        if(!MoveFileExW(temp.c_str(),ShaderPresetsFile().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace shader presets file.");
        g_tenchu_shader_preset=name;g_tenchu_shader_modified=false;app::Save();status="Shader preset saved.";return true;
    }catch(const std::exception& e){status=e.what();return false;}
}
bool TenchuLoadShaderPreset(const std::string& name,std::string& status){
    try{
        auto file=ReadShaderPresets();auto* presets=file["presets"].as_table();auto* preset=presets?presets->get_as<toml::table>(name):nullptr;
        if(!preset){status="Choose a saved shader preset.";return false;}
        ApplyShader(*preset);g_tenchu_shader_preset=name;g_tenchu_shader_modified=false;app::Save();status="Shader preset loaded.";return true;
    }catch(const std::exception& e){status=e.what();return false;}
}
void TenchuRestoreDefaults(){
    auto& s=app::Get();s.display_mode=app::DisplayMode::Windowed;s.width=640;s.height=480;s.vsync=true;s.fps_cap=60;
    g_tenchu_renderer=0;g_tenchu_widescreen=false;tenchu::wide::enabled=false;TenchuSetInternalPercent(100);
    g_tenchu_master_volume=100;g_tenchu_mute=false;g_tenchu_performance_overlay=false;g_tenchu_remember_camera=true;g_tenchu_debug_capture=false;TenchuUpdateMasterVolume();
    tenchu::motion::original_blur=false;
    tenchu::post::Reset();g_tenchu_shader_preset.clear();g_tenchu_shader_modified=false;
    tenchu::camera::Reset();link_profiles=false;keyboard_mouse=true;tenchu::cheats::god_mode=false;
    tenchu::cheats::second_player=false;tenchu::cheats::control_second=false;tenchu::cheats::single_screen=false;tenchu::cheats::story_start=false;tenchu::cheats::two_players_next=false;
    for(const auto& p:tenchu::patches::List())if(p.enabled)tenchu::patches::SetEnabled(p.id,false);
    previous=Snapshot();previous_remember=true;app::ApplyDisplay();app::Save();
}
