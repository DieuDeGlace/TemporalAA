# TemporalAA — camera-reprojection experiment

Native Steam Cube World temporal anti-aliasing, made by DieuDeGlace with AI assistance (OpenAI ChatGPT/Codex). MIT licensed; SDK attribution in THIRD_PARTY_NOTICES.md. Targets CubeModLoader API 7.3 and inspected Steam build timestamp 0x5d93ab0d/image size 0x5b7000. ReShade is untouched.

The supplied Omega videos guide the softer voxel-edge appearance. This independent implementation does not contain Omega shader code or assets. This is a prototype, not an exact recreation. Eight-frame Halton subpixel projection jitter, linear reconstruction, neighborhood-clamped color history and change-based rejection resolve the world before the HUD. History resets on resolution changes, missed scene boundaries, large camera jumps, zoom changes and toggling. F9 toggles; Mods/TemporalAA.ini has Enabled=1 and HistoryPercent=65 (maximum85).

This `test/motion-vector-taa` branch derives camera motion from the native scene depth and current/previous camera transforms inside the existing fullscreen resolve. History is sampled at the reprojected coordinate, with previous-jitter correction and previous-depth rejection for newly exposed surfaces. No per-object motion vectors are available: characters, fish and waving foliage still rely on neighborhood clamping and color-change rejection. Live movement, HUD/minimap and performance verification remain required. Projection jitter only applies to the identified native perspective world pipeline; other rendering pipelines may not share it.

The existing color copy, history and fullscreen resolve are retained. This experiment adds two depth textures and GPU-only depth copies; it adds no geometry replay or extra fullscreen pass. D24/S8, D32, D32/S8 and D16 depth targets are supported. Multisampled depth is copied and resolved to the nearest sample by a GPU compute shader before reprojection. Unsupported formats, mismatched dimensions or unavailable camera data use color-history fallback. Texture dimensions follow the actual scene, with no CPU GPU readback. GPU memory/work increase with resolution. World grading and MoonandShadow can coordinate via optional completion exports; update alpha-lookalike alongside this mod. No ReShade files, proprietary shaders, game assets or saves are bundled.

## Build

Windows x64, Visual Studio 2022 C++ Build Tools, Windows SDK, LLVM clang-cl, CMake 3.20+ and Ninja required. In an x64 Native Tools prompt:

```bat
git clone https://github.com/DieuDeGlace/TemporalAA.git
cd TemporalAA
git switch test/motion-vector-taa
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

## Install

Close Steam Cube World; copy build/TemporalAA.dll and optional config/TemporalAA.ini into Mods, then restart. Remove this DLL to uninstall. Existing MoonandShadow, alpha-lookalike and motion mods remain separate. WARP tests cover history accumulation, changed-pixel rejection, target resizing, state restoration and binding release at 720p,1080p and512×512. Tests do not establish Omega parity or frame rate.

Fine-detail stabilization: opposing-neighbor peak/trough detection retains more bounded history on thin, alternating pixel detail (such as distant voxel steps). Broad edges and flat areas retain the original history settings. This is a screen-space feature-size adjustment, not a depth-based distance mask; nearby subpixel grass can also benefit. Stale history is still neighborhood-clamped and strongly changed pixels are rejected. Live confirmation is needed to establish whether this resolves the reported distant shimmer without undesirable trails.

## Experimental settings and rollback

`CameraReprojection=1` enables the experiment. Set it to zero and restart for the color-history path, or reinstall the stable DLL built from `main` for complete rollback. F9 toggles all TAA. Keep `main` separate until live stability and timings have been assessed. The log reports whether camera reprojection actually activates.

GPU tests verify actual camera-translated history lookup, previous-depth rejection, three depth formats, resizing, matrix inversion, singular-matrix rejection, original accumulation/fallback behavior, jitter-upload reuse, blur and state restoration. They do not establish in-game frame rate or full moving-object correctness.

## SMAA + TAA test

`SMAA=1` enables upstream SMAA 1x at the medium preset on the world color before HUD drawing. Edge detection and blending-weight calculation add two fullscreen passes; neighborhood blending is fused into the TAA resolve. This treats visible shadow edges as color edges and does not increase shadow resolution, add cascades, or change grass/model shadow placement. Set `SMAA=0` and restart for the reprojection-only comparison. SMAA is licensed independently; see THIRD_PARTY_NOTICES.md.

The capture now waits for a depth-backed world draw instead of allowing an early sky/preview draw to lock out scene capture. D32/S8 depth targets are also supported. Logs identify camera-reprojection activation or a fallback reason, plus SMAA resource initialization. Live confirmation is required before claiming the fallback has been fixed on a user's installation.

Multisample depth testing covers 2x and 4x MSAA with different sample depths, including nearest-sample selection and reallocation between sample counts. The log must report active reprojection in the live Steam game before judging its visual benefit.
