cmake_minimum_required(VERSION 3.16)

project(OpenRCCars LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(OPENRCCARS_TOOLS_ONLY "Build only the data tools (no Tempest / GPU)" OFF)

if(MSVC)
  add_compile_options(/W3 /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# --- data formats: pack, csi, sb (plain C++, no dependencies) ---
add_library(rcformats STATIC
  formats/chunk.h
  formats/math3d.h
  formats/pack.h   formats/pack.cpp
  formats/csi.h    formats/csi.cpp
  formats/scene.h  formats/scene.cpp
  formats/collision.h formats/collision.cpp
  formats/settings.h  formats/settings.cpp
  formats/objects.h   formats/objects.cpp
  formats/water.h     formats/water.cpp
  formats/carprofile.h formats/carprofile.cpp
  formats/soundbank.h  formats/soundbank.cpp
  )
target_include_directories(rcformats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- Bullet (only LinearMath, BulletCollision and BulletDynamics) ---
include(FetchContent)
FetchContent_Declare(bullet
  GIT_REPOSITORY https://github.com/bulletphysics/bullet3.git
  GIT_TAG        3.25
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  do-not-add-subdirectory
  )
FetchContent_MakeAvailable(bullet)
# Not Bullet's bt*All.cpp unity files: some of its sources define BT_USE_SSE_IN_API before their includes,
# which breaks the other sources of a unity build with MSVC (SSE is on by default there).
file(GLOB_RECURSE BULLET_SOURCES
  ${bullet_SOURCE_DIR}/src/LinearMath/*.cpp
  ${bullet_SOURCE_DIR}/src/BulletCollision/*.cpp
  ${bullet_SOURCE_DIR}/src/BulletDynamics/*.cpp
  )
add_library(bullet STATIC ${BULLET_SOURCES})
target_include_directories(bullet SYSTEM PUBLIC ${bullet_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(bullet PRIVATE /W0)
else()
  target_compile_options(bullet PRIVATE -w)
endif()

# --- driving physics: car geometry and parameters, btRaycastVehicle on the COLL track (no GPU) ---
add_library(rcphysics STATIC
  physics/cargeometry.h physics/cargeometry.cpp
  physics/carparams.h   physics/carparams.cpp
  physics/vehicle.h     physics/vehicle.cpp
  physics/checkpoints.h physics/checkpoints.cpp
  physics/race.h        physics/race.cpp
  physics/props.h       physics/props.cpp
  physics/opponents.h   physics/opponents.cpp
  physics/btconvert.h
  )
target_link_libraries(rcphysics PUBLIC rcformats PRIVATE bullet)

# --- sound: software mixer, the original's engine and channel logic, MP3 music (no audio device needed) ---
add_library(rcaudio STATIC
  audio/mixer.h    audio/mixer.cpp
  audio/soundlib.h audio/soundlib.cpp
  audio/carsound.h audio/carsound.cpp
  audio/music.h    audio/music.cpp
  )
target_include_directories(rcaudio PRIVATE lib/dr_mp3)
target_link_libraries(rcaudio PUBLIC rcformats)

add_executable(rcinfo tools/rcinfo.cpp)
target_link_libraries(rcinfo rcformats rcphysics rcaudio)

if(OPENRCCARS_TOOLS_ONLY)
  return()
endif()

# --- Tempest ---
set(TEMPEST_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(TEMPEST_BUILD_AUDIO  ON  CACHE BOOL "" FORCE) # OpenAL Soft, bundled with Tempest
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/Tempest/Engine/CMakeLists.txt")
  message(FATAL_ERROR "Tempest not found - run: git submodule update --init --recursive")
endif()
add_subdirectory(lib/Tempest/Engine)

# --- shaders (GLSL -> SPIR-V, embedded into the binary) ---
add_subdirectory(shader)

# --- renderer shared by the game and tools ---
add_library(rcengine STATIC
  game/camera.h            game/camera.cpp
  game/resources.h         game/resources.cpp
  game/hud.h               game/hud.cpp
  game/arrow.h             game/arrow.cpp
  game/sounds.h            game/sounds.cpp
  game/world/levelmesh.h   game/world/levelmesh.cpp
  game/world/modelmesh.h     game/world/modelmesh.cpp
  game/world/level.h       game/world/level.cpp
  game/world/collisionview.h game/world/collisionview.cpp
  game/world/objects.h     game/world/objects.cpp
  )
target_include_directories(rcengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} lib/Tempest/Engine/include)
target_link_libraries(rcengine PUBLIC rcformats rcphysics rcaudio Tempest Shaders)
# Tempest is static here, so its private link directory for vulkan-1.lib does not reach our executables.
target_link_directories(rcengine PUBLIC "$ENV{VULKAN_SDK}/lib")

# --- game ---
add_executable(OpenRCCars game/main.cpp game/mainwindow.h game/mainwindow.cpp)
target_link_libraries(OpenRCCars rcengine)
# Console subsystem on purpose: logs (missing files, GPU info) stay visible while the project is young.
# For a release build add /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup (MSVC) or -mwindows (MinGW).

# --- offscreen renderer ---
add_executable(rcshot tools/rcshot.cpp)
target_link_libraries(rcshot rcengine)
