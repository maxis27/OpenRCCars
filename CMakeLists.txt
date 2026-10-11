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
  )
target_include_directories(rcformats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(rcinfo tools/rcinfo.cpp)
target_link_libraries(rcinfo rcformats)

if(OPENRCCARS_TOOLS_ONLY)
  return()
endif()

# --- Tempest ---
set(TEMPEST_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(TEMPEST_BUILD_AUDIO  OFF CACHE BOOL "" FORCE)
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
  game/world/levelmesh.h   game/world/levelmesh.cpp
  game/world/level.h       game/world/level.cpp
  )
target_include_directories(rcengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} lib/Tempest/Engine/include)
target_link_libraries(rcengine PUBLIC rcformats Tempest Shaders)
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
