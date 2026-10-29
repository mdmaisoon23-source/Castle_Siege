# Castle Siege - native title screen (dev build)

## Folder structure
```
CastleSiege/
├── CMakeLists.txt
├── README.md
├── docs/ARCHITECTURE.md
├── assets/
│   ├── title/        (optional PNGs: bg_battlefield, castle_wall, catapult, troops, king, logo, btn_play)
│   └── audio/        (optional FMOD banks, Windows only)
├── third_party/stb/  (put stb_image.h here)
└── src/
    ├── engine/   Engine.h, Application.cpp, RenderDevice_D3D11.cpp, RenderDevice_GL.cpp, Audio_FMOD.cpp, Audio_Null.cpp
    ├── game/     TitleScene.cpp
    └── platform/ main_win32.cpp, main_sdl.cpp
```

## Step 0 (both systems)
Download stb_image.h into third_party/stb/:
https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

## macOS
```bash
xcode-select --install
brew install cmake sdl2
cd CastleSiege
cmake -B build
cmake --build build
./build/CastleSiege
```
Run from the CastleSiege folder (not from inside build).

## Windows
1. Install Visual Studio 2022 with "Desktop development with C++".
2. Install the FMOD Studio API (fmod.com) and set FMOD_ROOT in CMakeLists.txt.
3. Open the folder in Visual Studio, set CastleSiege.exe as startup item, press F5.
4. Copy fmod.dll and fmodstudio.dll next to the exe.

Note: without Master.bank in assets/audio, FMOD init fails. To skip audio on Windows, in CMakeLists.txt replace
src/engine/Audio_FMOD.cpp with src/engine/Audio_Null.cpp and add
target_compile_definitions(CastleSiege PRIVATE CS_NULL_AUDIO), then remove the FMOD include/link lines.

## What you should see
1280x720 window: coloured placeholder layers, parallax on mouse move, smoke, logo scale-in, PLAY button hover.
Art is placeholder; supply your own original assets.

## Flow (v2)
Title (PLAY) -> Loading (spinning swords, 3s) -> Village (HUD + isometric castle).
In the village: red X returns to the title screen, the >> button toggles a 3x countdown speed.
New files: src/engine/Gfx.h, src/game/LoadingScene.cpp, src/game/VillageScene.cpp.
