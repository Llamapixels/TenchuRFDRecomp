#include "app/app.h"
#include "core/log.h"
#include "port/port_settings.h"
void TenchuMaybeRunGraphicsTest();
void TenchuStartHangWatchdog();
namespace tenchu::patches { void Start(); }
void TenchuStartAutomatedInput();
void TenchuAutomatedPad(int port,input::Pad& pad);
bool TenchuDiagnosticMessage(void*,uint32_t,uintptr_t,intptr_t,intptr_t*);
extern uint32_t g_tenchu_internal_percent;
extern bool g_tenchu_widescreen;
extern uint32_t g_tenchu_renderer;
void TenchuInitializeMenuWindow(void*);
void TenchuInitializeDisplayMenu();
void TenchuRenderDisplayMenu(double);

// Shown in the window title and the log, so every report names its build.
#define TENCHU_VERSION "0.1.4"
#define TENCHU_VERSION_W L"0.1.4"

namespace {
void Loaded() {
    LOGR("Tenchu: Return from Darkness native port, alpha %s", TENCHU_VERSION);
    TenchuMaybeRunGraphicsTest();
    TenchuStartHangWatchdog();
    TenchuStartAutomatedInput();
    tenchu::patches::Start();  // F6 > Patches: memory_patches\*.txt
    LOGR("Compatibility: Xbox Live startup/cleanup use SDK placeholders; online functionality is unverified.");
}
}

// No game logic is replaced; missing targets retain traps.
const app::GameInfo app::kGame = {
    .title = L"Tenchu: Return from Darkness (alpha " TENCHU_VERSION_W L")",
    .short_title = L"Tenchu: Return from Darkness",
    .name = L"TenchuRFD",
    .log = "tenchu_startup",
    .settings = "tenchu_startup.toml",
    .splash = L"splash",
    .missing = L"No game disc image was chosen. Start the game again and pick your Tenchu: Return from Darkness (USA) .iso.",
    .images = {{"default.xbe", "TenchuRFD"}},
    .options = {{"internal_resolution_percent",nullptr,&g_tenchu_internal_percent},{"widescreen",&g_tenchu_widescreen,nullptr},{"renderer",nullptr,&g_tenchu_renderer},
        {"master_volume_percent",nullptr,&g_tenchu_master_volume},{"mute",&g_tenchu_mute,nullptr},{"performance_overlay",&g_tenchu_performance_overlay,nullptr},{"remember_camera",&g_tenchu_remember_camera,nullptr},
        {"debug_capture",&g_tenchu_debug_capture,nullptr}},
    .load = TenchuLoadPortSettings,
    .save = TenchuSavePortSettings,
    .init = TenchuInitializeDisplayMenu,
    .loaded = Loaded,
    .window = TenchuInitializeMenuWindow,
    .message = TenchuDiagnosticMessage,
    .ui = TenchuRenderDisplayMenu,
    .pad = TenchuAutomatedPad,
    .rumble = nullptr,
};
