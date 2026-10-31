# Luau 0.744 (lane SDK-2: the modding SDK's scripting language, owner decision 2026-10-20): the library `openbfme_luau`.
#
#   thirdparty/luau-0.744/   the pristine luau-lang/luau 0.744 release (Common, Ast, Bytecode, Compiler and VM only; never edited;
#                            thirdparty/luau-0.744.manifest pins every file)
#   src/Libraries/Luau/      the patch layer: altered files at their pristine relative paths (marked ALTERED SOURCE VERSION), the new
#                            files, README.md (what changed and why) and openbfme.patch (checked by tools/luau_patch_check.py)
#
# The library is built from a staging directory in the build tree: the pristine files, then the patch layer copied over them by
# relative path (configure_file, so CMake re-runs when either side changes). Included after Dep2.cmake (the VM's ^ and the compiler's
# constant folding call the deterministic libm).
set(OPENBFME_LUAU_PRISTINE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/luau-0.744)
set(OPENBFME_LUAU_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/src/Libraries/Luau)
set(OPENBFME_LUAU_STAGE ${CMAKE_CURRENT_BINARY_DIR}/luau-openbfme)
set(OPENBFME_LUAU_PARTS Common Ast Bytecode Compiler VM)

foreach(root ${OPENBFME_LUAU_PRISTINE} ${OPENBFME_LUAU_PATCH})
    foreach(part ${OPENBFME_LUAU_PARTS})
        file(GLOB_RECURSE _luau_files RELATIVE ${root} ${root}/${part}/*.h ${root}/${part}/*.cpp)
        foreach(f ${_luau_files})
            configure_file(${root}/${f} ${OPENBFME_LUAU_STAGE}/${f} COPYONLY)
        endforeach()
    endforeach()
endforeach()

set(OPENBFME_LUAU_SOURCES)
foreach(part ${OPENBFME_LUAU_PARTS})
    file(GLOB _luau_part_sources ${OPENBFME_LUAU_STAGE}/${part}/src/*.cpp)
    list(APPEND OPENBFME_LUAU_SOURCES ${_luau_part_sources})
endforeach()

add_library(openbfme_luau STATIC ${OPENBFME_LUAU_SOURCES})
target_compile_features(openbfme_luau PUBLIC cxx_std_17)
foreach(part ${OPENBFME_LUAU_PARTS})
    target_include_directories(openbfme_luau PRIVATE ${OPENBFME_LUAU_STAGE}/${part}/include)
endforeach()
target_include_directories(openbfme_luau PRIVATE ${OPENBFME_LUAU_STAGE}/VM/src ${OPENBFME_LUAU_STAGE}/Compiler/src)
# the engine includes the embedding API as "luau/lua.h" etc.: Lua 4's own lua.h (openbfme_lua) keeps the plain name
foreach(h VM/include/lua.h VM/include/luaconf.h VM/include/lualib.h VM/include/lua_openbfme.h Compiler/include/luacode.h)
    get_filename_component(_name ${h} NAME)
    configure_file(${OPENBFME_LUAU_STAGE}/${h} ${OPENBFME_LUAU_STAGE}/public/luau/${_name} COPYONLY)
endforeach()
target_include_directories(openbfme_luau SYSTEM PUBLIC ${OPENBFME_LUAU_STAGE}/public)
target_link_libraries(openbfme_luau PUBLIC openbfme_detlibm)
# two of Luau's global variables share Lua 4's names (C++ does not mangle them): renamed in this library, nothing outside uses them
target_compile_definitions(openbfme_luau PRIVATE lua_ident=luau_openbfme_ident luaT_eventname=luau_openbfme_eventname)
set_target_properties(openbfme_luau PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(openbfme_luau PRIVATE /D_CRT_SECURE_NO_WARNINGS /w)
else()
    target_compile_options(openbfme_luau PRIVATE -w)
endif()
# sim role scripts run through the VM in lockstep: the simulation floating-point contract (cmake/SimFp.cmake)
openbfme_sim_register_target(openbfme_luau)
