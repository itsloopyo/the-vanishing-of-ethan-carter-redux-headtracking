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

# The canonical config, the committed file and the toggles' saves. Also the
# renderer behind `pixi run render-config` (--render-config <path>).
add_executable(ecr_config_tests
    config_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.cpp
)
target_include_directories(ecr_config_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/include
)
target_compile_definitions(ecr_config_tests PRIVATE ECR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(ecr_config_tests PRIVATE cameraunlock)
add_test(NAME ecr_config_tests COMMAND ecr_config_tests)

# The canonical config conversion's differential test: the dev build's reader
# (byte copies in config_differential/oracle/src) and startup code against the
# frozen reader in src/legacy_config/, and that reader against the config
# owner's migration into CameraUnlock.ini.
set(ECR_DIFFERENTIAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/config_differential)
add_executable(ecr_config_differential
    ${ECR_DIFFERENTIAL_DIR}/differential_tests.cpp
    ${ECR_DIFFERENTIAL_DIR}/oracle/oracle_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.cpp
)
target_include_directories(ecr_config_differential PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${ECR_DIFFERENTIAL_DIR}
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/include
)
target_compile_definitions(ecr_config_differential PRIVATE ECR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(ecr_config_differential PRIVATE cameraunlock)
add_test(NAME ecr_config_differential COMMAND ecr_config_differential)

# Core's canonical config lint over the committed file and every file the
# differential test migrated, which it writes beside itself under migrated\.
add_test(NAME ecr_config_lint COMMAND node
    ${ECR_DIFFERENTIAL_DIR}/lint-migrated.mjs
    $<TARGET_FILE_DIR:ecr_config_differential>/migrated)
set_tests_properties(ecr_config_lint PROPERTIES DEPENDS ecr_config_differential)
set_tests_properties(ecr_config_differential ecr_config_lint PROPERTIES LABELS differential)

# What the differential test compiles is pinned by hash, line endings aside:
# the oracle's byte copies must stay the dev build's (git show dev:src/<file>),
# the core sources both readers compile must stay the ones the dev build was
# built with (core ee8cc72), and the frozen reader is never edited.
function(ecr_require_hash path expected)
    file(READ "${path}" text)
    string(REPLACE "\r" "" text "${text}")
    string(SHA256 actual "${text}")
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${path} has changed (SHA-256 of its text ${actual}, pinned "
                            "${expected}). The config differential test compares against "
                            "exactly this file; see tests/CMakeLists.txt.")
    endif()
endfunction()
set(ECR_CORE_DIR ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp)
foreach(pinned
        "${ECR_DIFFERENTIAL_DIR}/oracle/src/config.cpp|57fdeee3ecf1e1762d2616bd9cfdb2226f75f76b6be047d390d882288c672e85"
        "${ECR_DIFFERENTIAL_DIR}/oracle/src/config.h|7a713cffefcb43b88e66b8bd2b37096316577f267d7f292661fffb25d15bec03"
        "${ECR_DIFFERENTIAL_DIR}/oracle/src/logging.h|809eb2feb225482fb263f307ca699d5235a618a26ca06e73b6818499d774cfb9"
        "${ECR_DIFFERENTIAL_DIR}/oracle/src/fov_override.h|412b267118d913f620601c87b39ace813abc4a3dd305889832b678ac1374aa97"
        "${ECR_DIFFERENTIAL_DIR}/oracle/src/ue_abi.h|395bf0c55074b81d58b4ef3b3724ffb1b236e80d6333a57acbd09f93e69da501"
        "${ECR_CORE_DIR}/include/cameraunlock/config/ini_reader.h|a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"
        "${ECR_CORE_DIR}/src/config/ini_reader.cpp|e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"
        "${ECR_CORE_DIR}/include/cameraunlock/config/value_guards.h|6d3e3512bdfc9ac2a54bee750c5425191bdb4c24f3ae2ea3b9925612e01c78e5"
        "${ECR_CORE_DIR}/src/config/value_guards.cpp|a833ff7f2721f7974ce039a3597349a0974e6695ce3e75e598ce5bf4985a14b1"
        "${ECR_CORE_DIR}/include/cameraunlock/math/finite_utils.h|c59772d698d54ade3374ee1221b74f5563a86d76f0eb34a989ef7efab389c0ad"
        "${ECR_CORE_DIR}/include/cameraunlock/logging/file_log.h|43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"
        "${ECR_CORE_DIR}/src/logging/file_log.cpp|73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"
        "${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.h|b8206e98c33712fb02407ddeee505d6f3549234822049630f969a47033ac167f"
        "${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.cpp|6d473d04fb66edd6d5564062bc1799c646502f2cae3ec0f17c5452ac3ee84bcc")
    string(REPLACE "|" ";" pair "${pinned}")
    list(GET pair 0 pinned_path)
    list(GET pair 1 pinned_hash)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pinned_path}")
    ecr_require_hash("${pinned_path}" "${pinned_hash}")
endforeach()
