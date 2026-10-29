#include "kernel/kernel.h"
#include "port/port_settings.h"
#include "app/app.h"
#include "audio/master_volume.h"
#include "game_camera.h"
#include "game_cheats.h"
#include "graphics/widescreen.h"
#include "graphics/post_processing.h"
#include "graphics/cutscene_motion_blur.h"
#include "graphics/screenshot.h"
#include "graphics/internal_resolution.h"
#include "core/log.h"
#include "kernel/disc.h"
#include <toml++/toml.hpp>
#include <fstream>
#include <limits>
#include <cstdio>
#include <stdexcept>
uint32_t g_tenchu_internal_percent=100,g_tenchu_renderer=0;
bool g_tenchu_widescreen=false;
static unsigned saves=0,display_changes=0;
void app::Save(){++saves;}
void app::ApplyDisplay(){++display_changes;}
app::Settings& app::Get(){static app::Settings s;return s;}
void* app::Window(){return nullptr;}
std::filesystem::path app::ExeDir(){auto p=std::filesystem::current_path()/"test_artifacts"/"port_settings_test";std::filesystem::create_directories(p);return p;}
namespace logx {void Write(Level,const char*,...){} }
uint8_t* kernel::g_base=nullptr;  // memory patches: no game memory in this test
const kernel::Disc* kernel::MountedDisc(){return nullptr;}
const kernel::Disc::Entry* kernel::Disc::Find(const std::vector<std::string>&) const{return nullptr;}
uint32_t kernel::Disc::Read(const Entry&,uint64_t,void*,uint32_t) const{return 0;}
void TenchuSetInternalPercent(uint32_t value){g_tenchu_internal_percent=tenchu::ClampInternalPercent(value);}
int main(){try{
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    std::filesystem::remove(app::ExeDir()/"graphics_profiles.toml");
    std::filesystem::remove(app::ExeDir()/"shader_presets.toml");
    g_tenchu_master_volume=150;TenchuUpdateMasterVolume();check(g_tenchu_master_volume==100&&tenchu::audio::master_gain==1,"volume must clamp");
    g_tenchu_master_volume=50;TenchuUpdateMasterVolume();float samples[]={.8f,-.8f,.2f,-.2f};tenchu::audio::ApplyMaster(samples,4);
    check(samples[0]==.4f&&samples[1]==-.4f&&samples[2]==.1f,"master gain must scale both stereo channels");
    g_tenchu_mute=true;TenchuUpdateMasterVolume();tenchu::audio::ApplyMaster(samples,4);
    check(samples[0]==0&&samples[1]==0&&g_tenchu_master_volume==50,"mute must keep selected volume");
    g_tenchu_mute=false;TenchuUpdateMasterVolume();check(tenchu::audio::master_gain==.5f,"unmute restores selected volume");
    tenchu::post::Preset(2);tenchu::post::enabled=true;
    tenchu::post::values[8]=.42f;tenchu::post::values[9]=.3f;tenchu::post::values[10]=.65f;tenchu::post::values[11]=12;
    tenchu::post::values[12]=.7f;tenchu::post::values[13]=.4f;tenchu::post::values[14]=.2f;tenchu::post::values[15]=.05f;
    tenchu::post::compare=true;tenchu::post::divider=.35f;
    tenchu::motion::original_blur=true;
    toml::table shaderSaved;TenchuSavePortSettings(&shaderSaved);tenchu::motion::original_blur=false;
    tenchu::post::Preset(0);tenchu::post::enabled=false;TenchuLoadPortSettings(&shaderSaved);
    check(tenchu::motion::original_blur,"cutscene blur preference must restore");
    check(tenchu::post::enabled&&tenchu::post::values[3]==.85f,"shader settings must roundtrip independently of camera");
    check(tenchu::post::values[8]==.42f&&tenchu::post::values[9]==.3f&&tenchu::post::values[11]==12&&tenchu::post::values[15]==.05f&&tenchu::post::compare&&tenchu::post::divider==.35f,"expanded shader settings and split position must roundtrip");
    toml::table invalidShader{{"shader",toml::table{{"gamma",std::numeric_limits<double>::infinity()},{"sharpness",99.0}}}};
    TenchuLoadPortSettings(&invalidShader);check(tenchu::post::values[4]==1&&tenchu::post::values[6]==1,"shader load must clamp and reject nonfinite values");
    using namespace tenchu::camera;
    g_tenchu_remember_camera=true;
    toml::table input{{"camera",toml::table{{"horizontal_speed",2.0},{"vertical_speed",std::numeric_limits<double>::infinity()},
        {"fov",200.0},{"distance",.01},{"running_horizontal_speed",.25},{"linked",true},{"invert_horizontal",true},{"free_requested",true}}}};
    free_requested=false;TenchuLoadPortSettings(&input);
    check(horizontal==2&&vertical==1&&fov==120&&distance==.25f&&running_horizontal==2&&invert_x&&!free_requested,"camera restore must validate values and exclude live freecam state");
    toml::table output;TenchuSavePortSettings(&output);
    check(output["camera"]["horizontal_speed"].value_or(0.)==2&&!output["camera"]["free_requested"],"camera serializer must omit active modes");
    g_tenchu_remember_camera=false;toml::table omitted;TenchuSavePortSettings(&omitted);check(!omitted["camera"],"remember-off must omit camera data");
    g_tenchu_remember_camera=true;TenchuInitializePortSettings();auto before=saves;horizontal=3;
    TenchuSaveCameraIfChanged(true);check(saves==before,"slider drag must not save every frame");
    TenchuSaveCameraIfChanged(false);check(saves==before+1,"finished camera edit must save");
    TenchuSaveCameraIfChanged(false);check(saves==before+1,"unchanged camera must not save repeatedly");
    auto& settings=app::Get();settings.display_mode=app::DisplayMode::Borderless;settings.width=0;settings.height=0;settings.vsync=false;settings.fps_cap=144;
    g_tenchu_renderer=1;g_tenchu_widescreen=true;g_tenchu_internal_percent=300;std::string status;
    check(TenchuSaveGraphicsProfile("Wide Vulkan",status),"profile save failed");
    g_tenchu_renderer=0;g_tenchu_widescreen=false;g_tenchu_internal_percent=100;settings.fps_cap=60;
    check(TenchuLoadGraphicsProfile("Wide Vulkan",status),"profile load failed");
    check(g_tenchu_renderer==1&&g_tenchu_widescreen&&g_tenchu_internal_percent==300&&settings.fps_cap==144&&!settings.vsync&&settings.width==0&&settings.height==0,"profile roundtrip must preserve desktop sentinel and graphics values");
    check(g_tenchu_master_volume==50&&horizontal==3,"graphics profiles must preserve audio/camera");
    check(!TenchuSaveGraphicsProfile("",status)&&!TenchuLoadGraphicsProfile("missing",status),"invalid/missing profile must fail without applying");
    tenchu::shots::Request();tenchu::shots::Request();
    check(!tenchu::shots::NextFrame()&&!tenchu::shots::NextFrame()&&tenchu::shots::NextFrame()&&!tenchu::shots::NextFrame(),"screenshot must queue once and wait two frames");tenchu::shots::saving=false;
    tenchu::shots::Result("saved");check(tenchu::shots::Status()=="saved","screenshot status must be readable");
    tenchu::post::Preset(4);tenchu::post::enabled=true;tenchu::post::compare=true;tenchu::post::divider=.3f;
    check(TenchuSaveShaderPreset("My night look",status),"named shader save failed");
    tenchu::post::Reset();g_tenchu_shader_preset.clear();
    check(TenchuLoadShaderPreset("My night look",status),"named shader load failed");
    check(tenchu::post::enabled&&tenchu::post::values[8]==.25f&&tenchu::post::values[9]==.15f&&tenchu::post::compare&&tenchu::post::divider==.3f&&g_tenchu_shader_preset=="My night look"&&!g_tenchu_shader_modified,"preset must restore all shader controls and identity");
    tenchu::post::values[9]=.6f;g_tenchu_shader_modified=true;
    toml::table restart;TenchuSavePortSettings(&restart);
    tenchu::post::Reset();g_tenchu_shader_preset.clear();g_tenchu_shader_modified=false;
    TenchuLoadPortSettings(&restart);
    check(tenchu::post::values[9]==.6f&&g_tenchu_shader_preset=="My night look"&&g_tenchu_shader_modified,"startup must restore last used preset and unsaved modifications");
    check(TenchuSaveShaderPreset("My night look",status)&&TenchuShaderPresets().size()==1,"resaving preset must replace existing name");
    tenchu::post::values[9]=0;check(TenchuLoadShaderPreset("My night look",status)&&tenchu::post::values[9]==.6f,"overwrite must persist updated values");
    check(!TenchuSaveShaderPreset("",status)&&!TenchuSaveShaderPreset("  ",status)&&!TenchuLoadShaderPreset("missing",status)&&tenchu::post::values[9]==.6f,"invalid preset operations must not change current shader");
    auto sentinel=app::ExeDir()/"game_save_sentinel.txt";{std::ofstream f(sentinel);f<<"keep";}
    g_tenchu_performance_overlay=true;g_tenchu_mute=true;free_requested=true;tenchu::cheats::god_mode=true;
    TenchuRestoreDefaults();
    check(settings.display_mode==app::DisplayMode::Windowed&&settings.width==640&&settings.height==480&&settings.vsync&&settings.fps_cap==60,"reset must restore display defaults");
    check(g_tenchu_renderer==0&&!g_tenchu_widescreen&&g_tenchu_internal_percent==100&&g_tenchu_master_volume==100&&!g_tenchu_mute&&!g_tenchu_performance_overlay&&!free_requested&&!tenchu::cheats::god_mode,"reset must restore F6 preferences");
    check(TenchuGraphicsProfiles().size()==1&&TenchuShaderPresets().size()==1&&g_tenchu_shader_preset.empty()&&std::filesystem::exists(sentinel),"reset must preserve profiles and saves");
    puts("PASS: stereo volume/mute, safe camera persistence, autosave timing, profile disk roundtrip, screenshots and non-destructive defaults");
}catch(const std::exception& e){puts(e.what());return 1;}}
