# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

# Builds the mod's own pure units (MOD_CORE_SOURCES) rather than a copy, so a
# passing suite is a statement about the code that ships in the .asi.
add_executable(ecr_headtracking_tests
    test_main.cpp
    aim_projection_tests.cpp
    camera_pose_tests.cpp
    caller_gate_tests.cpp
    fov_tests.cpp
    log_throttle_tests.cpp
    config_tests.cpp
)

foreach(source ${MOD_CORE_SOURCES})
    target_sources(ecr_headtracking_tests PRIVATE ${CMAKE_SOURCE_DIR}/${source})
endforeach()

target_include_directories(ecr_headtracking_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/include
)

target_link_libraries(ecr_headtracking_tests PRIVATE
    cameraunlock
    cameraunlock_unreal
    ws2_32
)

# The mod target defines this for every TU; the tests exercise the shipping
# (non-developer) configuration.
target_compile_definitions(ecr_headtracking_tests PRIVATE ECR_DEV_HOTKEYS=0)

add_test(NAME ecr_headtracking_tests COMMAND ecr_headtracking_tests)
