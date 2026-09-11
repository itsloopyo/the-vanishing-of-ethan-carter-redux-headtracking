# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

cmake_minimum_required(VERSION 3.20)
project(EthanCarterReduxHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    # /W4 rather than MSBuild's /W1 default. This code writes floats through
    # pointers into the engine's own stack frames and composes rotations from
    # them, so C4244 (conversion, possible loss of data) and C4018 (signed /
    # unsigned mismatch) are the shape of defect that produces a subtly wrong
    # camera instead of a crash. The tree is clean at this level; the third-party
    # minhook target keeps its own suppressions below.
    add_compile_options(/permissive- /Zc:__cplusplus /MP /W4)
endif()

# MinHook (for the GetPlayerViewPoint trampoline). Fetched + built locally;
# cameraunlock-core's hook manager links against this `minhook` target.
include(FetchContent)
# Pinned to the commit, not the tag. A tag is a mutable ref: whoever controls
# the upstream repo can move v1.3.3 onto different code, and this one is
# COMPILED INTO the .asi we sign and ship, so it is a stronger supply-chain
# position than an action pin. Same rule AGENTS.md applies to GitHub Actions.
FetchContent_Declare(
    minhook_src
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        9fbd087432700d73fc571118d6a9697a36443d88  # v1.3.3
)
FetchContent_MakeAvailable(minhook_src)
add_library(minhook STATIC
    ${minhook_src_SOURCE_DIR}/src/hook.c
    ${minhook_src_SOURCE_DIR}/src/buffer.c
    ${minhook_src_SOURCE_DIR}/src/trampoline.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde64.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC ${minhook_src_SOURCE_DIR}/include)
if(MSVC)
    # Third-party sources, kept at upstream's own warning expectations rather
    # than this project's /W4. The extra four are what /W4 adds on top: an empty
    # translation unit under the x64 build, nameless unions, a deliberate
    # constant truncation and a false-positive uninitialised read in the
    # disassembler tables.
    target_compile_options(minhook PRIVATE
        /wd4267 /wd4244 /wd4127 /wd4131 /wd4206 /wd4201 /wd4310 /wd4701)
endif()

# Cycling the inject mode live moves head tracking off the render caller, which
# looks exactly like a broken mod to a player who trips the chord by accident.
# The diagnostic is only wanted while re-deriving the render caller after a game
# patch, so it is compiled out of anything a player runs.
option(ECR_DEV_HOTKEYS "Build the inject-mode diagnostic hotkeys (developer aid, not for release)" OFF)

# Shared cameraunlock-core (C++ side) with the hook manager and UE runtime
# helpers enabled.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_UNREAL ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# Sources shared with the test executable: the pure-logic units the camera
# behaviour is pinned by, with no DllMain and no hook installation.
set(MOD_CORE_SOURCES
    src/aim_projection.cpp
    src/camera_pose.cpp
    src/caller_gate.cpp
    src/config.cpp
    src/fov_override.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
)

set(MOD_SOURCES
    ${MOD_CORE_SOURCES}
    src/dev_probe.cpp
    src/dllmain.cpp
    src/engine_trace.cpp
    src/headtracking_mod.cpp
    src/hook_diagnostics.cpp
    src/hud_reticle.cpp
    src/window_centering.cpp
)

add_library(EthanCarterReduxHeadTracking SHARED ${MOD_SOURCES})

target_include_directories(EthanCarterReduxHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(EthanCarterReduxHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    cameraunlock_unreal
    minhook
    ws2_32
    psapi
)

# Always defined, so no translation unit depends on a header having been
# included before its own `#if ECR_DEV_HOTKEYS`.
target_compile_definitions(EthanCarterReduxHeadTracking PRIVATE
    ECR_DEV_HOTKEYS=$<IF:$<BOOL:${ECR_DEV_HOTKEYS}>,1,0>)

if(ECR_DEV_HOTKEYS)
    message(WARNING "ECR_DEV_HOTKEYS is ON. This build registers Ctrl+Shift+U "
                    "and Ctrl+Shift+J, which cycle which GetPlayerViewPoint caller is "
                    "injected and can leave the view not following the head. "
                    "Do not ship it.")
endif()

# Output as .asi for Ultimate ASI Loader.
set_target_properties(EthanCarterReduxHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Behaviour-locking tests over the pure units: the camera pose composition and
# axis signs, the per-caller inject gate, the FOV offset bounds, and what the
# INI parses to. They build the same sources the .asi does - nothing is stubbed.
option(ECR_BUILD_TESTS "Build the behaviour-locking tests" ON)
if(ECR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
