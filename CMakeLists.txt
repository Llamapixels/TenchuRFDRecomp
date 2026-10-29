cmake_minimum_required(VERSION 3.25)
project(TenchuAnalysisTools LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(TENCHU_BUILD_RUNTIME "Build the experimental native startup executable" OFF)
if(TENCHU_BUILD_RUNTIME)
  get_filename_component(TENCHU_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_program(TENCHU_AR llvm-ar HINTS "${TENCHU_COMPILER_BIN}" REQUIRED NO_DEFAULT_PATH)
  find_program(TENCHU_RANLIB llvm-ranlib HINTS "${TENCHU_COMPILER_BIN}" REQUIRED NO_DEFAULT_PATH)
  set(CMAKE_AR "${TENCHU_AR}")
  set(CMAKE_RANLIB "${TENCHU_RANLIB}")
  include("${CMAKE_CURRENT_SOURCE_DIR}/tools/XboxXBERecomp/cmake/XbeRecomp.cmake")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(TENCHU_RECOMP_OVERLAY "${CMAKE_CURRENT_BINARY_DIR}/tool_overlay/analysis.cpp")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tools/scripts/prepare_recompiler_overlay.py)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/prepare_recompiler_overlay.py"
    "${TENCHU_RECOMP_OVERLAY}" COMMAND_ERROR_IS_FATAL ANY)
  set(TENCHU_EXPERIMENTAL_SOURCES ${XBE_RECOMP_SOURCES})
  list(REMOVE_ITEM TENCHU_EXPERIMENTAL_SOURCES "${XBE_SDK_DIR}/tools/recomp/analysis.cpp")
  add_executable(xbe_recomp_fixedpoint EXCLUDE_FROM_ALL ${TENCHU_EXPERIMENTAL_SOURCES} "${TENCHU_RECOMP_OVERLAY}")
  target_include_directories(xbe_recomp_fixedpoint PRIVATE "${XBE_SDK_DIR}/runtime" "${XBE_SDK_DIR}/tools/recomp")
  target_compile_definitions(xbe_recomp_fixedpoint PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(xbe_recomp_fixedpoint PRIVATE Zydis tomlplusplus::tomlplusplus)
  set(TENCHU_DIAGNOSTIC_OVERLAY "${CMAKE_CURRENT_BINARY_DIR}/tool_overlay/diagnostic_analysis.cpp")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/prepare_recompiler_overlay.py" "${TENCHU_DIAGNOSTIC_OVERLAY}" --diagnostic COMMAND_ERROR_IS_FATAL ANY)
  add_executable(xbe_recomp_diagnostics EXCLUDE_FROM_ALL ${TENCHU_EXPERIMENTAL_SOURCES} "${TENCHU_DIAGNOSTIC_OVERLAY}")
  target_include_directories(xbe_recomp_diagnostics PRIVATE "${XBE_SDK_DIR}/runtime" "${XBE_SDK_DIR}/tools/recomp")
  target_compile_definitions(xbe_recomp_diagnostics PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(xbe_recomp_diagnostics PRIVATE Zydis tomlplusplus::tomlplusplus)
  # Production keeps upstream discovery behavior, with checked switch-table
  # boundary handling. The separate fixed-point experiment is still optional.
  set_property(TARGET xbe_recomp PROPERTY SOURCES ${TENCHU_EXPERIMENTAL_SOURCES} "${TENCHU_DIAGNOSTIC_OVERLAY}")
  set(TENCHU_AUDIO_OVERLAY "${CMAKE_CURRENT_BINARY_DIR}/runtime_overlay/dsound.cpp")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    tools/scripts/prepare_audio_overlay.py tools/XboxXBERecomp/runtime/xdk/dsound.cpp)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/prepare_audio_overlay.py"
    "${TENCHU_AUDIO_OVERLAY}" COMMAND_ERROR_IS_FATAL ANY)
  list(REMOVE_ITEM XBE_RUNTIME_SOURCES "${XBE_SDK_DIR}/runtime/xdk/dsound.cpp")
  list(APPEND XBE_RUNTIME_SOURCES "${TENCHU_AUDIO_OVERLAY}")
  set(TENCHU_GRAPHICS_OVERLAY "${CMAKE_CURRENT_BINARY_DIR}/graphics_overlay")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    tools/scripts/prepare_graphics_overlay.py runtime/graphics/post_processing.hlsl tools/XboxXBERecomp/runtime/core/diag.h tools/scripts/resize_swap_overlay.inc tools/XboxXBERecomp/runtime/gpu/state.h
    tools/XboxXBERecomp/runtime/gpu/shaders.h tools/XboxXBERecomp/runtime/gpu/shaders.cpp
    tools/XboxXBERecomp/runtime/gpu/d3d12.cpp tools/XboxXBERecomp/runtime/xdk/d3d8.cpp tools/XboxXBERecomp/runtime/gpu/texturedumper.cpp tools/XboxXBERecomp/runtime/kernel/files.cpp tools/XboxXBERecomp/runtime/app/game_files.cpp)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/prepare_graphics_overlay.py"
    "${TENCHU_GRAPHICS_OVERLAY}" COMMAND_ERROR_IS_FATAL ANY)
  foreach(source gpu/shaders.cpp gpu/d3d12.cpp xdk/d3d8.cpp gpu/texturedumper.cpp kernel/files.cpp app/game_files.cpp)
    list(REMOVE_ITEM XBE_RUNTIME_SOURCES "${XBE_SDK_DIR}/runtime/${source}")
    list(APPEND XBE_RUNTIME_SOURCES "${TENCHU_GRAPHICS_OVERLAY}/${source}")
  endforeach()
  set(XBE_HOST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/common/game_info.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/math_exports.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/automated_test_input.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/render_diagnostics.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/display_menu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/renderer_selection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/port_settings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/menu_options.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/texture_packs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/texture_match.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/textures_menu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/mods.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/mods_menu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/custom_levels.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/custom_levels_menu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/open_folder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/hang_watchdog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/game_location.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/connect_file.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/disc_extract.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/memory_patches.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/memory_patches_menu.cpp")
  add_xbe_image(TenchuRFD default.xbe startup.toml 8 startup)
  # also here, so building the executable alone never leaves the hookless configs
  # for the release-time fallback (it then built its own DLL without the hooks)
  add_custom_command(TARGET TenchuRFD POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/runtime/vulkan" "${CMAKE_CURRENT_BINARY_DIR}/renderers/vulkan"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${XBE_COMPILER_DIR}/config/game.toml" "${XBE_COMPILER_DIR}/config/pointers.toml")
  # Only the checked profile belongs beside the local executable. The SDK's
  # release-time fallback otherwise picks the first config for default.xbe.
  add_custom_target(tenchu_local_ready ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -f
      "${XBE_COMPILER_DIR}/config/game.toml" "${XBE_COMPILER_DIR}/config/pointers.toml"
      "${XBE_COMPILER_DIR}/work/TenchuRFD/built.txt"
    COMMAND "${CMAKE_COMMAND}" -E touch "$<TARGET_FILE:TenchuRFD_code>"
    DEPENDS TenchuRFD_code compiler_files VERBATIM)
  target_include_directories(TenchuRFD BEFORE PRIVATE "${TENCHU_GRAPHICS_OVERLAY}")
  target_include_directories(TenchuRFD PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
  target_link_libraries(TenchuRFD PRIVATE comdlg32)
  # RFD Switchblade (the mod and level tool): a separate program, shipped in tools\ beside the game
  add_executable(RFDSwitchblade WIN32 src/workbench/main.cpp src/workbench/workbench.cpp src/workbench/files_page.cpp src/workbench/pictures.cpp src/workbench/pack_model.cpp
    src/workbench/packs_page.cpp src/workbench/images.cpp src/workbench/level_model.cpp src/workbench/viewport.cpp src/workbench/levels_page.cpp
    src/common/connect_file.cpp src/common/texture_match.cpp
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp" "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp" "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp")
  target_include_directories(RFDSwitchblade PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
  target_link_libraries(RFDSwitchblade PRIVATE tomlplusplus::tomlplusplus d3d11 dxgi d3dcompiler windowscodecs ole32 comdlg32 shell32 dwmapi)
  set_target_properties(RFDSwitchblade PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tools" OUTPUT_NAME "RFD Switchblade")
  add_executable(audio_rolloff_test tests/audio_rolloff_test.cpp)
  target_include_directories(audio_rolloff_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
  enable_testing()
  add_test(NAME audio_rolloff COMMAND audio_rolloff_test)
  add_executable(audio_stream_adapter_test tests/audio_stream_adapter_test.cpp src/startup/audio_stream_adapter.cpp src/startup/audio_pause_adapter.cpp)
  target_include_directories(audio_stream_adapter_test PRIVATE "${XBE_SDK_DIR}/runtime" "${CMAKE_CURRENT_SOURCE_DIR}/generated/startup")
  add_dependencies(audio_stream_adapter_test recompile_TenchuRFD)
  add_test(NAME audio_stream_adapter COMMAND audio_stream_adapter_test)
  add_executable(object_initialization_test tests/object_initialization_test.cpp)
  target_include_directories(object_initialization_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
  add_test(NAME object_initialization COMMAND object_initialization_test)
  add_executable(io_apc_dispatcher_test tests/io_apc_dispatcher_test.cpp src/startup/io_apc_dispatcher.cpp)
  target_include_directories(io_apc_dispatcher_test PRIVATE "${XBE_SDK_DIR}/runtime")
  add_test(NAME io_apc_dispatcher COMMAND io_apc_dispatcher_test)
  # coverage checks against the analysis of your own default.xbe (analysis/*.json, not shipped)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/analysis/xbe.json")
    add_test(NAME menu_callback_coverage COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/menu_callback_test.py")
    add_test(NAME task_transition_coverage COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/task_transition_test.py")
    add_test(NAME story_table_coverage COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/story_table_test.py")
    add_test(NAME loader_vtable_coverage COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/loader_vtable_test.py")
    add_test(NAME math_dispatch_coverage COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/math_dispatch_test.py")
    add_test(NAME referenced_vtable_coverage COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/referenced_vtable_test.py")
    add_test(NAME switch_table_boundary COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/switch_table_boundary_test.py")
    add_test(NAME static_callback_coverage COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/static_callback_coverage_test.py")
  endif()
  add_executable(autoplay_input_test tests/autoplay_input_test.cpp)
  target_include_directories(autoplay_input_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${XBE_SDK_DIR}/runtime")
  add_test(NAME autoplay_input COMMAND autoplay_input_test)
  add_executable(display_menu_test tests/display_menu_test.cpp src/common/display_menu.cpp src/common/port_settings.cpp src/common/menu_options.cpp
    src/common/texture_packs.cpp src/common/texture_match.cpp src/common/textures_menu.cpp src/common/mods.cpp src/common/mods_menu.cpp src/common/custom_levels.cpp src/common/custom_levels_menu.cpp src/common/open_folder.cpp src/common/connect_file.cpp
    src/common/memory_patches.cpp src/common/memory_patches_menu.cpp
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp" "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
  target_include_directories(display_menu_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${XBE_SDK_DIR}/runtime" "${imgui_SOURCE_DIR}")
  target_link_libraries(display_menu_test PRIVATE tomlplusplus::tomlplusplus shell32 windowscodecs ole32)
  add_test(NAME display_menu COMMAND display_menu_test)
  add_executable(game_cheats_test tests/game_cheats_test.cpp)
  target_include_directories(game_cheats_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
  add_test(NAME game_cheats COMMAND game_cheats_test)
  add_executable(camera_hooks_test tests/camera_hooks_test.cpp src/startup/camera_hooks.cpp)
  target_include_directories(camera_hooks_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${XBE_SDK_DIR}/runtime")
  add_test(NAME camera_hooks COMMAND camera_hooks_test)
  add_executable(widescreen_test tests/widescreen_test.cpp src/startup/widescreen_projection.cpp)
  target_include_directories(widescreen_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${XBE_SDK_DIR}/runtime")
  add_test(NAME widescreen COMMAND widescreen_test)
  add_executable(renderer_selection_test tests/renderer_selection_test.cpp src/common/renderer_selection.cpp)
  target_include_directories(renderer_selection_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${XBE_SDK_DIR}/runtime")
  target_compile_definitions(renderer_selection_test PRIVATE _CRT_SECURE_NO_WARNINGS)
  add_test(NAME renderer_selection COMMAND renderer_selection_test)
  add_executable(presentation_resize_test tests/presentation_resize_test.cpp)
  target_include_directories(presentation_resize_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
  add_test(NAME presentation_resize COMMAND presentation_resize_test)
  add_executable(port_settings_test tests/port_settings_test.cpp src/common/port_settings.cpp src/common/texture_packs.cpp src/common/texture_match.cpp src/common/mods.cpp src/common/connect_file.cpp src/common/memory_patches.cpp)
  target_include_directories(port_settings_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${XBE_SDK_DIR}/runtime")
  target_link_libraries(port_settings_test PRIVATE tomlplusplus::tomlplusplus windowscodecs ole32)
  add_executable(texture_packs_test tests/texture_packs_test.cpp src/common/texture_packs.cpp src/common/texture_match.cpp)
  target_include_directories(texture_packs_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${XBE_SDK_DIR}/runtime")
  target_link_libraries(texture_packs_test PRIVATE tomlplusplus::tomlplusplus windowscodecs ole32)
  add_test(NAME texture_packs COMMAND texture_packs_test)
  add_executable(custom_levels_test tests/custom_levels_test.cpp src/common/custom_levels.cpp src/common/mods.cpp src/common/connect_file.cpp)
  target_include_directories(custom_levels_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${XBE_SDK_DIR}/runtime")
  target_link_libraries(custom_levels_test PRIVATE tomlplusplus::tomlplusplus)
  add_test(NAME custom_levels COMMAND custom_levels_test)
  add_executable(mods_test tests/mods_test.cpp src/common/mods.cpp src/common/connect_file.cpp)
  target_include_directories(mods_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${XBE_SDK_DIR}/runtime")
  target_link_libraries(mods_test PRIVATE tomlplusplus::tomlplusplus)
  add_test(NAME mods COMMAND mods_test)
  add_executable(workbench_test tests/workbench_test.cpp src/workbench/images.cpp src/workbench/pack_model.cpp src/common/texture_match.cpp src/common/connect_file.cpp)
  target_include_directories(workbench_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${CMAKE_CURRENT_SOURCE_DIR}/src/workbench")
  target_link_libraries(workbench_test PRIVATE tomlplusplus::tomlplusplus windowscodecs ole32)
  add_test(NAME workbench COMMAND workbench_test)
  add_executable(workbench_probe EXCLUDE_FROM_ALL tests/workbench_probe.cpp src/workbench/images.cpp src/common/texture_match.cpp src/common/connect_file.cpp)
  target_include_directories(workbench_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${CMAKE_CURRENT_SOURCE_DIR}/src/workbench")
  target_link_libraries(workbench_probe PRIVATE tomlplusplus::tomlplusplus windowscodecs ole32)
  add_executable(connect_file_test tests/connect_file_test.cpp src/common/connect_file.cpp)
  target_include_directories(connect_file_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
  add_test(NAME connect_file COMMAND connect_file_test)
  add_executable(texture_pack_probe EXCLUDE_FROM_ALL tests/texture_pack_probe.cpp src/common/texture_packs.cpp src/common/texture_match.cpp)
  target_include_directories(texture_pack_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${XBE_SDK_DIR}/runtime")
  target_link_libraries(texture_pack_probe PRIVATE tomlplusplus::tomlplusplus windowscodecs ole32)
  add_test(NAME port_settings COMMAND port_settings_test)
  add_executable(memory_patches_test tests/memory_patches_test.cpp src/common/memory_patches.cpp)
  target_include_directories(memory_patches_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime" "${XBE_SDK_DIR}/runtime")
  target_link_libraries(memory_patches_test PRIVATE tomlplusplus::tomlplusplus)
  target_compile_definitions(memory_patches_test PRIVATE TENCHU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME memory_patches COMMAND memory_patches_test)
  add_test(NAME camera_path COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/camera_path_test.py")
  add_executable(backbuffer_scale_adapter_test tests/backbuffer_scale_adapter_test.cpp src/startup/backbuffer_scale_adapter.cpp)
  target_include_directories(backbuffer_scale_adapter_test PRIVATE "${XBE_SDK_DIR}/runtime" "${CMAKE_CURRENT_SOURCE_DIR}/generated/startup")
  add_dependencies(backbuffer_scale_adapter_test recompile_TenchuRFD)
  add_test(NAME backbuffer_scale_adapter COMMAND backbuffer_scale_adapter_test)
  add_executable(indexed_draw_adapter_test tests/indexed_draw_adapter_test.cpp src/startup/indexed_draw_adapter.cpp)
  target_include_directories(indexed_draw_adapter_test PRIVATE "${XBE_SDK_DIR}/runtime" "${CMAKE_CURRENT_SOURCE_DIR}/generated/startup")
  add_dependencies(indexed_draw_adapter_test recompile_TenchuRFD)
  add_test(NAME indexed_draw_adapter COMMAND indexed_draw_adapter_test)
  find_program(TENCHU_POWERSHELL NAMES pwsh powershell REQUIRED)
  add_test(NAME backbuffer_gpu COMMAND "${TENCHU_POWERSHELL}" -NoProfile -File
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/smoke_startup.ps1" -GraphicsTest)
  set_tests_properties(backbuffer_gpu PROPERTIES TIMEOUT 25 LABELS "gpu" ENVIRONMENT "TENCHU_RENDERER=d3d12")
  add_test(NAME backbuffer_gpu_three_quarter COMMAND "${TENCHU_POWERSHELL}" -NoProfile -File
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/smoke_startup.ps1" -GraphicsTest -ThreeQuarterScale)
  set_tests_properties(backbuffer_gpu_three_quarter PROPERTIES TIMEOUT 25 LABELS "gpu" ENVIRONMENT "TENCHU_RENDERER=d3d12")
  add_test(NAME backbuffer_gpu_internal_2x COMMAND "${TENCHU_POWERSHELL}" -NoProfile -File
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/smoke_startup.ps1" -GraphicsTest -ThreeQuarterScale -InternalScalePercent 200)
  set_tests_properties(backbuffer_gpu_internal_2x PROPERTIES TIMEOUT 25 LABELS "gpu" ENVIRONMENT "TENCHU_RENDERER=d3d12")
  add_test(NAME vulkan_gpu COMMAND "${TENCHU_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/smoke_startup.ps1" -GraphicsTest)
  add_test(NAME vulkan_gpu_three_quarter COMMAND "${TENCHU_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/smoke_startup.ps1" -GraphicsTest -ThreeQuarterScale)
  add_test(NAME vulkan_gpu_internal_2x COMMAND "${TENCHU_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/smoke_startup.ps1" -GraphicsTest -ThreeQuarterScale -InternalScalePercent 200)
  set_tests_properties(vulkan_gpu vulkan_gpu_three_quarter vulkan_gpu_internal_2x PROPERTIES TIMEOUT 30 LABELS "gpu;vulkan" ENVIRONMENT "TENCHU_RENDERER=vulkan")
  # Upstream render_thread.cpp uses the SSE4.1 _mm_testz_si128 intrinsic.
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/tools/XboxXBERecomp/runtime/gpu/render_thread.cpp"
    PROPERTIES COMPILE_OPTIONS "-msse4.1")
  return()
endif()
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
add_subdirectory(tools/zydis)
add_subdirectory(tools/tomlplusplus)
set(SDK "${CMAKE_CURRENT_SOURCE_DIR}/tools/XboxXBERecomp")
file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS "${SDK}/tools/recomp/*.cpp")
add_executable(xbe_recomp ${RECOMP_SOURCES})
add_executable(xbe_symbols "${SDK}/tools/symbols/main.cpp")
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(xbe_recomp PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/msvc_tool_annotations.h")
endif()
foreach(target xbe_recomp xbe_symbols)
  target_include_directories(${target} PRIVATE "${SDK}/runtime" "${SDK}/tools/recomp")
  target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(${target} PRIVATE Zydis tomlplusplus::tomlplusplus)
endforeach()
