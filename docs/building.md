# Building from source

Windows 10 or 11, 64-bit. You need:

- Visual Studio 2022 or later (Build Tools are enough) with **Desktop development with C++** and a Windows SDK.
- Python 3.14 (the scripts use its `compression.zstd` module).
- Git.
- Your own `default.xbe` from the root of your *Tenchu: Return from Darkness* (USA) disc image.

## 1. Dependencies

From the repository folder:

```powershell
git clone https://github.com/TGP482/XboxXBERecomp tools/XboxXBERecomp
git -C tools/XboxXBERecomp checkout 0fd00442660573dc342b41c11c6574318f0e8698
```

Download the LLVM 23.1.4 archive named in [SDK-VERSION.md](../SDK-VERSION.md), save it as
`tools/llvm/archive.tar.zst`, then unpack it (the script checks its SHA256 first):

```powershell
python tools/scripts/extract_llvm.py
```

Fetch the Vulkan renderer's DLLs (official vkd3d-proton and DXVK releases, checked by SHA256):

```powershell
powershell -ExecutionPolicy Bypass -File tools/scripts/install_vulkan_runtime.ps1
```

## 2. The game executable

Copy `default.xbe` from your disc image to `assets/default.xbe`. It is only read; the game's code is generated from it
into `generated/` and `build/`, which are never committed.

## 3. Build

```powershell
cmd /c tools\scripts\build_native.cmd
```

This configures `build/native` with Ninja and the portable Clang, then builds the game (`TenchuRFD.exe` and the
recompiled `TenchuRFD.dll`), RFD Switchblade (`tools\RFD Switchblade.exe`) and the tests. The logs go to
`build/native_configure.log` and `build/native_build.log`. Afterwards, `cmake --build build/native` rebuilds what
changed.

Run `build\native\TenchuRFD.exe` and pick your `.iso` on the first start, as in a release.

## 4. Tests

```powershell
ctest --test-dir build/native -LE gpu
```

`-LE gpu` skips the tests that start the game on your graphics card.

## 5. A release zip

```powershell
python tools/scripts/package_release.py
```

This writes `dist/TenchuRFD-<version>-alpha-windows.zip`: the game, RFD Switchblade, the compiler folder players use to
prepare the game's code from their own disc, the Visual C++ runtime and the licenses. No game files or generated code
go in. The debug symbols are kept beside it in `dist/symbols`.
