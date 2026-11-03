# fifa07 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fifa07 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(FIFA07_SOURCES
    src/main.cpp
    src/memory_hooks.cpp
    src/cube_texture_cache.cpp
    src/skinning_diagnostics.cpp
    src/audio_diagnostics.cpp
    src/graphics_defaults.cpp
    src/xma_fix/factory.cpp
    src/xma_fix/xma_context.cpp
    src/xma_fix/xma_decoder.cpp
)

option(FIFA07_BUILD_ANIMATION_AUDIT "Build the opt-in animation instruction recorder" OFF)
if(FIFA07_BUILD_ANIMATION_AUDIT)
    list(APPEND FIFA07_SOURCES src/animation_audit.cpp)
endif()

option(FIFA07_BUILD_CINEMATIC_DIAGNOSTICS "Build the opt-in cinematic call/frame recorder" OFF)
if(FIFA07_BUILD_CINEMATIC_DIAGNOSTICS)
    list(APPEND FIFA07_SOURCES src/cinematic_diagnostics.cpp)
endif()

if(WIN32)
    add_executable(fifa07 WIN32 ${FIFA07_SOURCES})
else()
    add_executable(fifa07 ${FIFA07_SOURCES})
endif()

rexglue_setup_target(fifa07 GPU_PLUGINS xenos)
target_link_libraries(fifa07 PRIVATE rex::libavcodec rex::libavutil)
target_include_directories(fifa07 SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../tools/rexglue-source/thirdparty/FFmpeg")

# The pinned nightly emits an unsafe in-place float-to-half pack. Run this
# targeted correction after every regeneration, before compiling its output.
find_program(FIFA07_PATCH_PYTHON NAMES python.exe python3 python
    REQUIRED)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp"
    APPEND
    COMMAND "${FIFA07_PATCH_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/fix-vpkd3d-alias.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/generated/default"
)
if(WIN32)
    target_link_options(fifa07 PRIVATE "LINKER:/MAP:${CMAKE_CURRENT_BINARY_DIR}/fifa07.map")
endif()
