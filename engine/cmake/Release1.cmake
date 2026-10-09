# Lane RELEASE-1 (what a public tester needs: finding the game, the version and the session log, packaging): client-only sources (excluded from the
# simulation audit in tools/sim/sim_policy.json) and tests. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
set(OPENBFME_RELEASE1_VERSION_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/BuildVersion.gen.cpp)
set(OPENBFME_VERSION_OVERRIDE "" CACHE STRING "Release override of the version string (empty = git describe); see cmake/BuildVersion.cmake")
find_package(Git QUIET)
# regenerated on every build (a new commit or tag changes the version without a reconfigure); the file is only rewritten when its text changes
add_custom_target(openbfme_build_version
    COMMAND ${CMAKE_COMMAND} -DOUT=${OPENBFME_RELEASE1_VERSION_CPP} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/.. "-DGIT=${GIT_EXECUTABLE}"
            "-DVERSION_OVERRIDE=${OPENBFME_VERSION_OVERRIDE}" "-DENGINE_ID=${OPENBFME_ENGINE_ID_VALUE}" "-DID_X86_32=${OPENBFME_SIM_X86_32}"
            "-DID_OPTIONS=${OPENBFME_ENGINE_ID_OPTIONS}" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildVersion.cmake
    BYPRODUCTS ${OPENBFME_RELEASE1_VERSION_CPP}
    COMMENT "Build version"
    VERBATIM)
set(OPENBFME_RELEASE1_SOURCES
    src/Common/System/InstallLocator.cpp
    src/Common/System/LogPrivacy.cpp
    src/Common/System/ConsoleFilter.cpp
    src/Common/System/BuildVersion.cpp
    ${OPENBFME_RELEASE1_VERSION_CPP}
)
set(OPENBFME_RELEASE1_GODOT_SOURCES
    src/GodotDevice/GodotRelease.cpp
)
set(OPENBFME_RELEASE1_TESTS
    tests/test_release1_install.cpp
    tests/test_release1_log.cpp
    tests/test_release1_console.cpp
)
