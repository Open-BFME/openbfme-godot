# Lua 4.0.1 with EA's fork (lane LUA-1): the library `openbfme_lua`.
#
#   thirdparty/lua-4.0.1/   the pristine lua.org tarball content (never edited; MANIFEST.md5 pins every file)
#   src/Libraries/Lua/      the patch layer: the altered files (marked ALTERED SOURCE VERSION), the new files, README.md
#                           (what changed and the evidence) and ea-fork.patch (the diff of the altered files against the
#                           pristine ones, checked by tools/lua_patch_check.py)
#
# The library is built from a staging directory in the build tree: the pristine files, then the patch layer copied over
# them by name. Staging at configure time with configure_file also makes CMake re-run when either side changes.
set(OPENBFME_LUA_PRISTINE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lua-4.0.1)
set(OPENBFME_LUA_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/src/Libraries/Lua)
set(OPENBFME_LUA_STAGE ${CMAKE_CURRENT_BINARY_DIR}/lua-ea)

file(GLOB OPENBFME_LUA_PRISTINE_FILES
    ${OPENBFME_LUA_PRISTINE}/src/*.c ${OPENBFME_LUA_PRISTINE}/src/*.h
    ${OPENBFME_LUA_PRISTINE}/src/lib/*.c ${OPENBFME_LUA_PRISTINE}/include/*.h)
foreach(f ${OPENBFME_LUA_PRISTINE_FILES})
    get_filename_component(name ${f} NAME)
    configure_file(${f} ${OPENBFME_LUA_STAGE}/${name} COPYONLY)
endforeach()
file(GLOB OPENBFME_LUA_PATCH_FILES ${OPENBFME_LUA_PATCH}/*.c ${OPENBFME_LUA_PATCH}/*.h ${OPENBFME_LUA_PATCH}/*.cpp)
foreach(f ${OPENBFME_LUA_PATCH_FILES})
    get_filename_component(name ${f} NAME)
    configure_file(${f} ${OPENBFME_LUA_STAGE}/${name} COPYONLY)
endforeach()
# the headers the engine includes (the rest of the library's headers stay private to it)
foreach(h lua.h lauxlib.h lualib.h luadebug.h lua_ea.h)
    configure_file(${OPENBFME_LUA_STAGE}/${h} ${OPENBFME_LUA_STAGE}/include/${h} COPYONLY)
endforeach()

# lundump.c is needed by lua_dobuffer (binary chunk check); luac, lua.c and ltests.c are not part of the library
set(OPENBFME_LUA_C_SOURCES
    lapi lcode ldebug ldo lfunc lgc llex lmem lobject lparser lstate lstring ltable ltm lundump lvm lzio
    lauxlib lbaselib ldblib liolib lmathlib lstrlib leahost)
set(OPENBFME_LUA_LIB_SOURCES)
foreach(n ${OPENBFME_LUA_C_SOURCES})
    list(APPEND OPENBFME_LUA_LIB_SOURCES ${OPENBFME_LUA_STAGE}/${n}.c)
endforeach()
list(APPEND OPENBFME_LUA_LIB_SOURCES ${OPENBFME_LUA_STAGE}/leanumeric.cpp)

add_library(openbfme_lua STATIC ${OPENBFME_LUA_LIB_SOURCES})
target_include_directories(openbfme_lua PRIVATE ${OPENBFME_LUA_STAGE})
target_include_directories(openbfme_lua SYSTEM PUBLIC ${OPENBFME_LUA_STAGE}/include)
set_target_properties(openbfme_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
# the force-included configuration (C locale ctype, strcoll) applies to the C files only
set(OPENBFME_LUA_CONFIG ${OPENBFME_LUA_STAGE}/lua_ea_config.h)
foreach(n ${OPENBFME_LUA_C_SOURCES})
    if(MSVC)
        set_source_files_properties(${OPENBFME_LUA_STAGE}/${n}.c PROPERTIES COMPILE_OPTIONS "/FI${OPENBFME_LUA_CONFIG}")
    else()
        set_source_files_properties(${OPENBFME_LUA_STAGE}/${n}.c PROPERTIES COMPILE_OPTIONS "-include;${OPENBFME_LUA_CONFIG}")
    endif()
endforeach()
# the VM's arithmetic is rounded one operation at a time (lua_ea.h: PC24): the simulation floating-point contract (cmake/SimFp.cmake)
openbfme_sim_register_target(openbfme_lua)
