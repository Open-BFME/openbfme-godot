# Lane DEP-2 (our own maths: the simulation needs no libm transcendental): the deterministic double-precision libm.
# Included from engine/CMakeLists.txt after SimFp.cmake and before Lua.cmake (the Lua library's math functions call it).
#
# openbfme_detlibm is its own static library because the Lua library (below the core) needs it; it carries the simulation floating-point
# contract like the core. Its source is a simulation source (the audit manifest, OPENBFME_SIM_SOURCES_LIBS), registered as a facade
# implementation in tools/sim/sim_policy.json, but it is not compiled into openbfme_core.
add_library(openbfme_detlibm STATIC src/Common/System/DeterministicLibm.cpp)
target_include_directories(openbfme_detlibm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(openbfme_detlibm PROPERTIES POSITION_INDEPENDENT_CODE ON)
openbfme_sim_register_target(openbfme_detlibm)
set(OPENBFME_SIM_SOURCES_LIBS src/Common/System/DeterministicLibm.cpp)

set(OPENBFME_DEP2_TESTS
    tests/test_deterministic_libm.cpp
)
if(UNIX AND NOT APPLE)
    # the GDExtension's own powf / getentropy (linked into the extension with --wrap; the tests call the __wrap_ functions directly)
    list(APPEND OPENBFME_DEP2_TESTS tests/test_linux_runtime_shims.cpp src/GodotDevice/LinuxRuntimeShims.cpp)
endif()
