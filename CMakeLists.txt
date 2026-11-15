cmake_minimum_required(VERSION 3.16)
project(OpenDF VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# SDL2: use a system/package-manager copy if present, otherwise download it.
option(OPENDF_FETCH_SDL2 "Download and build SDL2 if it is not installed" ON)
find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND AND OPENDF_FETCH_SDL2)
  include(FetchContent)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
  FetchContent_MakeAvailable(SDL2)
endif()
find_package(OpenGL REQUIRED)

add_executable(opendf
  src/main.cpp
  src/audio/Audio.cpp
  src/audio/Music.cpp
  src/formats/Image.cpp
  src/formats/Kdv.cpp
  src/formats/Bink.cpp
  src/formats/HudDef.cpp
  src/audio/Pwf.cpp
  src/core/ExeImage.cpp
  src/core/Import.cpp
  src/core/Pff.cpp
  src/formats/Anm.cpp
  src/formats/Chr.cpp
  src/formats/Dlg.cpp
  src/formats/Mission.cpp
  src/formats/Model3di.cpp
  src/formats/Pcx.cpp
  src/formats/Rtxt.cpp
  src/formats/Trn.cpp
  src/menu/Frontend.cpp
  src/menu/FrontendDf2.cpp
  src/menu/Launcher.cpp
  src/menu/Mnu.cpp
  src/game/Ai.cpp
  src/game/Config.cpp
  src/game/Animator.cpp
  src/game/Controls.cpp
  src/game/Game.cpp
  src/game/Player.cpp
  src/game/Moves.cpp
  src/game/Script.cpp
  src/game/Sound.cpp
  src/game/Vehicles.cpp
  src/game/Tracers.cpp
  src/game/Weapons.cpp
  src/game/TracerLines.cpp
  src/game/Weather.cpp
  src/game/World.cpp
  src/render/Gl.cpp
  src/render/Hud.cpp
  src/render/Models.cpp
  src/render/Terrain.cpp
  src/render/Window.cpp
)
target_include_directories(opendf PRIVATE src)

if(TARGET SDL2::SDL2main)
  target_link_libraries(opendf PRIVATE SDL2::SDL2main)
endif()
if(TARGET SDL2::SDL2)
  target_link_libraries(opendf PRIVATE SDL2::SDL2)
else()
  target_link_libraries(opendf PRIVATE SDL2::SDL2-static)
endif()
target_link_libraries(opendf PRIVATE OpenGL::GL)
# libjpeg (optional): the later games' JPEG terrain colour maps.
find_package(JPEG QUIET)
if(JPEG_FOUND)
  target_compile_definitions(opendf PRIVATE OPENDF_HAVE_JPEG)
  target_include_directories(opendf PRIVATE ${JPEG_INCLUDE_DIRS})
  target_link_libraries(opendf PRIVATE ${JPEG_LIBRARIES})
endif()

if(MSVC)
  target_compile_options(opendf PRIVATE /W3 /utf-8)
  set_target_properties(opendf PROPERTIES WIN32_EXECUTABLE OFF)
else()
  target_compile_options(opendf PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
