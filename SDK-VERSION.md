# SDK version record

The port builds on an unmodified checkout of XboxXBERecomp. Its own changes to the SDK are applied at configure time
by `tools/scripts/prepare_*_overlay.py`, which write patched copies of the SDK files into the build folder; the
checkout itself is never edited.

## Pinned SDK

- **Repository**: https://github.com/TGP482/XboxXBERecomp
- **Local clone**: `tools/XboxXBERecomp`
- **Commit**: `0fd00442660573dc342b41c11c6574318f0e8698`
- The SDK fetches its own dependencies while configuring: Zydis v4.1.1, toml++ v3.4.0, Dear ImGui v1.91.0 and
  SDL release-3.4.16.

## Compiler

- **LLVM 23.1.4** portable Windows x64 release:
  https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.4/clang%2Bllvm-23.1.4-x86_64-pc-windows-msvc.tar.zst
- SHA256 `1a3f2f998e230b5791cc4e2bfe11a1f7c07fe42c96ff44f227c9db0bedfaf091`, checked by
  `tools/scripts/extract_llvm.py`, which unpacks it into `tools/llvm`.
- Visual Studio 2022 or later with the C++ workload and a Windows SDK (for the linker libraries, CMake and Ninja).

## Vulkan renderer

The Vulkan option runs the Direct3D 12 renderer through vkd3d-proton and DXVK's DXGI.
`tools/scripts/install_vulkan_runtime.ps1` downloads the official releases, checks their SHA256 and puts the DLLs and
their licenses in `runtime/vulkan`:

- vkd3d-proton 3.0.1 (`d3d12.dll`, `d3d12core.dll`; LGPL 2.1 or later),
  SHA256 `3cf2315522af5e43605ef6d3c41dad91387040bf97199934f3f7ab76caaa2f0c`
- DXVK 3.1.1 (`dxgi.dll`; zlib), SHA256 `40565b4a724aadc4433fa4e010b4b23916d9b1f1baeee64e17186db94f54e608`

## Overlays

| Script | SDK files | Change |
| --- | --- | --- |
| `prepare_recompiler_overlay.py` | `tools/recomp/analysis.cpp` | Checked switch-table boundaries during function discovery |
| `prepare_audio_overlay.py` | `runtime/xdk/dsound.cpp` | Master volume and the DirectSound behaviour the game needs |
| `prepare_graphics_overlay.py` | `runtime/gpu/shaders.{h,cpp}`, `gpu/state.h`, `gpu/texturedumper.cpp`, `xdk/d3d8.cpp`, `core/diag.h`, `kernel/files.cpp`, `app/game_files.cpp` | Depth precision, render scale, texture packs and dumps, mods, custom levels, picking the disc image |
