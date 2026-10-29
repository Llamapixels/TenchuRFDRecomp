#include "graphics/renderer_selection.h"
#include "graphics/presentation_resize.h"
#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

uint32_t g_tenchu_renderer=0; // 0 native D3D12, 1 D3D12 -> Vulkan
namespace {
std::atomic<uint32_t> active{0};
std::atomic<bool> restarting{false};
constexpr UINT restart_message=WM_APP+0x326;
HMODULE dxgi_module=nullptr,d3d12_module=nullptr,core_module=nullptr;
std::once_flag load_once;
bool loaded=false;
bool Load(){
    std::call_once(load_once,[]{
        const bool vulkan=active.load()==1;
        const auto folder=app::ExeDir()/"renderers"/"vulkan";
        if(vulkan){
            auto flags=LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
            dxgi_module=LoadLibraryExW((folder/L"dxgi.dll").c_str(),nullptr,flags);
            core_module=LoadLibraryExW((folder/L"d3d12core.dll").c_str(),nullptr,flags);
            d3d12_module=LoadLibraryExW((folder/L"d3d12.dll").c_str(),nullptr,flags);
        }else{
            dxgi_module=LoadLibraryExW(L"dxgi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            d3d12_module=LoadLibraryExW(L"d3d12.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
        loaded=dxgi_module&&d3d12_module&&(!vulkan||core_module);
        if(!loaded){LOGE("Renderer runtime load failed (%lu)",GetLastError());return;}
        wchar_t dx[1024]{},d3[1024]{};
        GetModuleFileNameW(dxgi_module,dx,1024);GetModuleFileNameW(d3d12_module,d3,1024);
        LOGR("Renderer active: %s",vulkan?"Vulkan (vkd3d-proton / DXVK)":"Direct3D 12");
        LOGR("Renderer libraries: %ls | %ls",dx,d3);
    });
    return loaded;
}
template<class T>T Proc(HMODULE module,const char* name){return reinterpret_cast<T>(module?GetProcAddress(module,name):nullptr);}
}
bool TenchuVulkanAvailable(){
    const auto path=app::ExeDir()/"renderers"/"vulkan";
    return std::filesystem::is_regular_file(path/"dxgi.dll")&&std::filesystem::is_regular_file(path/"d3d12.dll")&&std::filesystem::is_regular_file(path/"d3d12core.dll");
}
void TenchuInitializeRenderer(){
    if(g_tenchu_renderer>1)g_tenchu_renderer=0;
    uint32_t choice=g_tenchu_renderer;
    wchar_t override_name[32]{};
    if(GetEnvironmentVariableW(L"TENCHU_RENDERER",override_name,32))choice=std::wstring(override_name)==L"vulkan"?1:0;
    active=choice;
    if(choice==1){
        const auto log=app::ExeDir()/"logs"/"vulkan.log";
        SetEnvironmentVariableW(L"VKD3D_LOG_FILE",log.c_str());
        SetEnvironmentVariableW(L"DXVK_LOG_PATH",(app::ExeDir()/"logs").c_str());
        // vkd3d-proton's pipeline cache otherwise lands in the working directory, which
        // a shortcut can point anywhere: keep it beside the game.
        SetEnvironmentVariableW(L"VKD3D_SHADER_CACHE_PATH",app::ExeDir().c_str());
    }
}
uint32_t TenchuActiveRenderer(){return active.load();}
bool TenchuPresentationPaused(){return tenchu::resize::window.Paused(GetTickCount64());}
std::atomic<uint64_t> g_tenchu_presents{0};  // successful presents, read by the hang watchdog
void TenchuPresentationStarted(){tenchu::resize::window.presented=true;g_tenchu_presents.fetch_add(1,std::memory_order_relaxed);}
void TenchuRequestRestart(){
    if(restarting.exchange(true))return;
    if(!PostMessageW(static_cast<HWND>(app::Window()),restart_message,g_tenchu_renderer+1,0)){
        restarting=false;LOGE("Cannot queue game restart (%lu)",GetLastError());
    }
}
bool TenchuRendererMessage(uint32_t msg,uintptr_t requested,intptr_t,intptr_t* result){
    if(msg==WM_ENTERSIZEMOVE||msg==WM_EXITSIZEMOVE||msg==WM_SIZE){
        tenchu::resize::window.Message(msg,requested,GetTickCount64());
        if(msg==WM_ENTERSIZEMOVE)LOGR("Window resize: presentation paused during drag");
        if(msg==WM_EXITSIZEMOVE)LOGR("Window resize: waiting for stable final dimensions");
        return false;
    }
    if(msg!=restart_message)return false;
    {
        std::lock_guard lock(app::UiLock());
        if(requested==1||(requested==2&&TenchuVulkanAvailable()))g_tenchu_renderer=uint32_t(requested-1);
        LOGR("Restart requested: saved renderer %u",g_tenchu_renderer);app::Save();
    }
    kernel::Relaunch("TenchuRFD");*result=0;return true;
}
HRESULT WINAPI TenchuCreateDXGIFactory2(UINT flags,REFIID iid,void** out){
    if(!Load())return E_FAIL;
    auto fn=Proc<decltype(&CreateDXGIFactory2)>(dxgi_module,"CreateDXGIFactory2");return fn?fn(flags,iid,out):E_NOINTERFACE;
}
HRESULT WINAPI TenchuD3D12CreateDevice(IUnknown* adapter,D3D_FEATURE_LEVEL level,REFIID iid,void** out){
    if(!Load())return E_FAIL;
    auto fn=Proc<PFN_D3D12_CREATE_DEVICE>(d3d12_module,"D3D12CreateDevice");return fn?fn(adapter,level,iid,out):E_NOINTERFACE;
}
HRESULT WINAPI TenchuD3D12GetDebugInterface(REFIID iid,void** out){
    if(!Load())return E_FAIL;
    auto fn=Proc<PFN_D3D12_GET_DEBUG_INTERFACE>(d3d12_module,"D3D12GetDebugInterface");return fn?fn(iid,out):E_NOINTERFACE;
}
HRESULT WINAPI TenchuD3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* desc,D3D_ROOT_SIGNATURE_VERSION version,ID3DBlob** blob,ID3DBlob** error){
    if(!Load())return E_FAIL;
    auto fn=Proc<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(d3d12_module,"D3D12SerializeRootSignature");return fn?fn(desc,version,blob,error):E_NOINTERFACE;
}
