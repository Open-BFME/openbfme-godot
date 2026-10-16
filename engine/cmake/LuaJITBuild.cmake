# Build step of cmake/LuaJIT.cmake (cmake -P): copy the pristine LuaJIT tree to WORK and build its static library there.
#   PRISTINE, WORK, PREFIX   the upstream tree, the build copy, the prefix header (src/Libraries/LuaJIT/prefix.h)
#   MSVC_BUILD               true: msvcbuild.bat (run from a Visual Studio environment, as build.bat sets up)
#   TARGET_SYS, CC, TARGET_FLAGS, DEPLOYMENT_TARGET   the makefile's target (Darwin / iOS / Linux), compiler and flags
#   HOST_SYSROOT             iOS: the macOS SDK of the host tools
file(REMOVE_RECURSE ${WORK})
file(COPY ${PRISTINE}/ DESTINATION ${WORK})

if(MSVC_BUILD)
    # the prefix is forced into every compilation of the script (the host tools too: the renaming is consistent within each program)
    file(TO_NATIVE_PATH "${PREFIX}" prefix_native)
    file(READ ${WORK}/src/msvcbuild.bat bat)
    string(REPLACE "@set LJCOMPILE=cl /nologo" "@set LJCOMPILE=cl /nologo /FI\"${prefix_native}\"" bat "${bat}")
    file(WRITE ${WORK}/src/msvcbuild.bat "${bat}")
    execute_process(COMMAND cmd /c msvcbuild.bat lua52compat amalg static
                    WORKING_DIRECTORY ${WORK}/src RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
    set(lib ${WORK}/src/lua51.lib)
else()
    cmake_host_system_information(RESULT jobs QUERY NUMBER_OF_LOGICAL_CORES)
    set(args -C ${WORK}/src -j${jobs} libluajit.a BUILDMODE=static
             "TARGET_SYS=${TARGET_SYS}" "CC=${CC}" "TARGET_FLAGS=${TARGET_FLAGS}"
             "XCFLAGS=-DLUAJIT_ENABLE_LUA52COMPAT -include ${PREFIX}")
    if(TARGET_SYS STREQUAL "iOS")
        # the host tools (minilua, buildvm) run on this Mac: the host compiler with the macOS SDK (HOST_SYSROOT); the target compiles
        # name their iOS sysroot in TARGET_FLAGS, so SDKROOT (build_ios.sh exports the iOS one) is not needed and would mislead the host's
        list(APPEND args "HOST_CC=/usr/bin/clang -isysroot ${HOST_SYSROOT}")
        unset(ENV{SDKROOT})
    endif()
    if(DEPLOYMENT_TARGET)
        set(ENV{MACOSX_DEPLOYMENT_TARGET} ${DEPLOYMENT_TARGET})
    elseif(TARGET_SYS STREQUAL "Darwin")
        set(ENV{MACOSX_DEPLOYMENT_TARGET} 11.0)
    endif()
    execute_process(COMMAND make ${args} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
    set(lib ${WORK}/src/libluajit.a)
endif()
if(NOT rc EQUAL 0 OR NOT EXISTS ${lib})
    message(FATAL_ERROR "LuaJIT build failed (${rc}):\n${out}")
endif()
# the public headers under include/luajit/: the engine core also sees the retail Lua 4 headers, so LuaJIT's are included by that folder
file(MAKE_DIRECTORY ${WORK}/include/luajit)
foreach(h lua.h lauxlib.h lualib.h luaconf.h luajit.h lua.hpp)
    file(COPY ${WORK}/src/${h} DESTINATION ${WORK}/include/luajit)
endforeach()
