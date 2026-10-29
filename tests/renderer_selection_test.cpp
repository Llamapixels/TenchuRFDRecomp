#include "graphics/renderer_selection.h"
#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include <filesystem>
#include <cstdio>
#include <stdexcept>
static HWND window=nullptr;static unsigned saves=0;
struct Restarted{};
void* app::Window(){return window;}
void app::Save(){++saves;}
std::recursive_mutex& app::UiLock(){static std::recursive_mutex lock;return lock;}
std::filesystem::path app::ExeDir(){wchar_t p[1024];GetModuleFileNameW(nullptr,p,1024);return std::filesystem::path(p).parent_path();}
namespace logx {void Write(Level,const char*,...){} }
[[noreturn]] void kernel::Relaunch(const char* exe){if(std::string(exe)!="TenchuRFD")throw std::runtime_error("wrong restart target");throw Restarted{};}
int main(){try{
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    SetEnvironmentVariableW(L"TENCHU_RENDERER",nullptr);
    g_tenchu_renderer=99;TenchuInitializeRenderer();check(g_tenchu_renderer==0&&TenchuActiveRenderer()==0,"invalid renderer must clamp");
    g_tenchu_renderer=1;TenchuInitializeRenderer();check(TenchuActiveRenderer()==1,"saved Vulkan renderer must initialize");
    SetEnvironmentVariableW(L"TENCHU_RENDERER",L"d3d12");TenchuInitializeRenderer();check(TenchuActiveRenderer()==0,"native test override must win without changing saved option");
    SetEnvironmentVariableW(L"TENCHU_RENDERER",L"vulkan");TenchuInitializeRenderer();check(TenchuActiveRenderer()==1,"Vulkan override must initialize");
    SetEnvironmentVariableW(L"TENCHU_RENDERER",nullptr);
    check(TenchuVulkanAvailable(),"packaged Vulkan runtime missing");
    window=CreateWindowExW(0,L"STATIC",L"Tenchu restart test",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(window!=nullptr,"message-only test window unavailable");
    TenchuRequestRestart();TenchuRequestRestart();
    MSG msg{};check(PeekMessageW(&msg,window,WM_APP,0xBFFF,PM_REMOVE)!=0,"restart must post to the app window");
    MSG duplicate{};check(!PeekMessageW(&duplicate,window,WM_APP,0xBFFF,PM_REMOVE),"restart must not queue duplicate launches");
    intptr_t result=0;check(!TenchuRendererMessage(WM_NULL,0,0,&result),"unrelated messages must pass through");
    TenchuPresentationStarted();
    check(!TenchuRendererMessage(WM_ENTERSIZEMOVE,0,0,&result)&&TenchuPresentationPaused(),"real message route must pause live resize");
    check(!TenchuRendererMessage(WM_EXITSIZEMOVE,0,0,&result)&&TenchuPresentationPaused(),"real message route must debounce drag completion");
    check(!TenchuRendererMessage(WM_SIZE,SIZE_MINIMIZED,0,&result)&&TenchuPresentationPaused(),"real message route must pause minimized presentation");
    bool restarted=false;try{TenchuRendererMessage(msg.message,msg.wParam,msg.lParam,&result);}catch(const Restarted&){restarted=true;}
    check(restarted&&saves==1,"restart message must save settings and relaunch actual game");
    DestroyWindow(window);puts("PASS: renderer preference/override/clamp, packaged runtime, queued restart, duplicate guard and real message routing");
}catch(const std::exception& e){puts(e.what());return 1;}}
