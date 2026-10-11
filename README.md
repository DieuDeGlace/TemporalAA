# TemporalAA (prototype)

Native Steam Cube World temporal anti-aliasing, made by DieuDeGlace with AI assistance (OpenAI ChatGPT/Codex). MIT licensed; SDK attribution in THIRD_PARTY_NOTICES.md. Targets CubeModLoader API 7.3 and inspected Steam build timestamp 0x5d93ab0d/image size 0x5b7000. ReShade is untouched.

The supplied Omega videos guide the softer voxel-edge appearance, but do not establish Omega's implementation. This is a prototype, not an exact recreation. Eight-frame Halton subpixel projection jitter, linear reconstruction, neighborhood-clamped color history and change-based rejection resolve the world before the HUD. History resets on resolution changes, missed scene boundaries, large camera jumps, zoom changes and toggling. F9 toggles; Mods/TemporalAA.ini has Enabled=1 and HistoryPercent=65 (maximum85).

There is currently no depth reprojection or per-object motion-vector buffer. Fast camera/character/foliage motion may lose temporal smoothing or retain some trails. Color rejection limits this but cannot replace motion vectors. Live movement, HUD/minimap and performance verification are required before treating this as a stable release. Projection jitter only applies to the identified native perspective world pipeline; other custom rendering pipelines may not share it.

One full-size color copy/resolve, history texture and fullscreen resolve pass are added. Texture dimensions follow the actual scene, with no CPU GPU readback. GPU memory/work increase with resolution. World grading and MoonandShadow can coordinate via optional completion exports; update alpha-lookalike alongside this mod. No ReShade files, proprietary shaders, game assets or saves are bundled.

## Build

Windows x64, Visual Studio 2022 C++ Build Tools, Windows SDK, LLVM clang-cl, CMake 3.20+ and Ninja required. In an x64 Native Tools prompt:

```bat
git clone https://github.com/DieuDeGlace/TemporalAA.git
cd TemporalAA
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

## Install

Close Steam Cube World; copy build/TemporalAA.dll and optional config/TemporalAA.ini into Mods, then restart. Remove this DLL to uninstall. Existing MoonandShadow, alpha-lookalike and motion mods remain separate. WARP tests cover history accumulation, changed-pixel rejection, target resizing, state restoration and binding release at 720p,1080p and512×512. Tests do not establish Omega parity or frame rate.
