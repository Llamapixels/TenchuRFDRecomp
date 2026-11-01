#pragma once
#include <cstdint>
#include <string>
#include <vector>
extern uint32_t g_tenchu_master_volume;
extern bool g_tenchu_mute,g_tenchu_performance_overlay,g_tenchu_remember_camera,g_tenchu_debug_capture;
void TenchuInitializePortSettings();
void TenchuLoadPortSettings(const void*);
void TenchuSavePortSettings(void*);
void TenchuUpdateMasterVolume();
void TenchuSaveCameraIfChanged(bool editing);
void TenchuRestoreDefaults();
bool TenchuSaveGraphicsProfile(const std::string&,std::string& error);
bool TenchuLoadGraphicsProfile(const std::string&,std::string& error);
std::vector<std::string> TenchuGraphicsProfiles();
void TenchuRenderExtraTabs();
void TenchuRenderTexturesTab();
void TenchuRenderModsTab();
void TenchuRenderLevelsTab();
void TenchuRenderPatchesTab();
void TenchuRenderWindowOptions();
void TenchuRenderPerformanceOverlay(double fps);
void TenchuSetInternalPercent(uint32_t);
void TenchuTakeScreenshot();
void TenchuDumpThreads(const char* why);

extern std::string g_tenchu_shader_preset;
extern bool g_tenchu_shader_modified;
bool TenchuSaveShaderPreset(const std::string&,std::string&);
bool TenchuLoadShaderPreset(const std::string&,std::string&);
std::vector<std::string> TenchuShaderPresets();
extern std::string g_tenchu_physics_preset;
extern bool g_tenchu_physics_modified;
bool TenchuSavePhysicsPreset(const std::string&,std::string&);
bool TenchuLoadPhysicsPreset(const std::string&,std::string&);
std::vector<std::string> TenchuPhysicsPresets();
void TenchuResetPhysicsSettings();
