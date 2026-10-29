cmake_minimum_required(VERSION 3.20)
project(CastleSiege CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(COMMON_SRC
    src/engine/Application.cpp
    src/game/TitleScene.cpp
    src/game/LoadingScene.cpp
    src/game/VillageScene.cpp)

if(APPLE OR (UNIX AND NOT APPLE))
    # ---------------- macOS / Linux dev build: SDL2 + OpenGL + silent audio ----------------
    find_package(SDL2 REQUIRED)
    add_executable(CastleSiege ${COMMON_SRC}
        src/platform/main_sdl.cpp
        src/engine/RenderDevice_GL.cpp
        src/engine/Audio_Null.cpp)
    target_compile_definitions(CastleSiege PRIVATE CS_USE_GL CS_NULL_AUDIO)
    target_include_directories(CastleSiege PRIVATE src/engine third_party/stb ${SDL2_INCLUDE_DIRS})
    target_link_libraries(CastleSiege PRIVATE ${SDL2_LIBRARIES} "-framework OpenGL")
else()
    # ---------------- Windows: D3D11 + FMOD ----------------
    set(FMOD_ROOT "C:/Program Files (x86)/FMOD SoundSystem/FMOD Studio API Windows/api" CACHE PATH "")
    add_executable(CastleSiege WIN32 ${COMMON_SRC}
        src/platform/main_win32.cpp
        src/engine/RenderDevice_D3D11.cpp
        src/engine/Audio_FMOD.cpp)
    target_include_directories(CastleSiege PRIVATE
        src/engine third_party/stb
        "${FMOD_ROOT}/core/inc" "${FMOD_ROOT}/studio/inc")
    target_link_directories(CastleSiege PRIVATE
        "${FMOD_ROOT}/core/lib/x64" "${FMOD_ROOT}/studio/lib/x64")
    target_link_libraries(CastleSiege PRIVATE fmodstudio_vc fmod_vc)
endif()

# Run from the project root so "assets/" resolves. Assets are optional (placeholders render if missing).
