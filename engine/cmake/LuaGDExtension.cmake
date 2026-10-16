# lua-gdextension 0.8.2 (lane LUAGD-1, https://github.com/gilzoide/lua-gdextension, MIT): Lua in Godot (LuaState, LuaTable, ...,
# Lua scripts as a ScriptLanguage), on LuaJIT (cmake/LuaJIT.cmake). Built from source into the openbfme GDExtension itself, not as
# a second extension: one godot-cpp (iOS links every extension statically into the app, two copies of godot-cpp would clash), and
# BfmeLuaRuntime runs on the lua_State of the addon's LuaState (GodotBfmeLua.cpp).
#
#   thirdparty/lua-gdextension-0.8.2/   the pristine upstream files of the tag (src, doc_classes, tools/code_generation, LICENSE ...)
#   thirdparty/sol2/                    sol2 (include/ + LICENSE.txt) at the commit 0.8.2 pins (github.com/Assumeru/sol2)
#   thirdparty/tree-sitter/             tree-sitter (lib/ + LICENSE) at the commit 0.8.2 pins
#   thirdparty/tree-sitter-lua/         the Lua grammar (src/ + LICENSE.md) at the commit 0.8.2 pins
#   src/Libraries/LuaGDExtension/       the patch layer: the altered upstream files (README.md: what changed and why)
#   src/GodotDevice/LuaGDExtensionRegister.cpp   the addon's class registration (upstream src/main.cpp without its entry point)
#
# The build follows upstream's SConstruct: C++20, exceptions, the LuaJIT and sol2 defines; the generated headers come from upstream's
# own generator (tools/luagd_generate.py runs it against this build's godot-cpp API). Not built: the editor documentation data
# (upstream's doc_data.gen.cpp, editor help only).
set(OPENBFME_LUAGD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lua-gdextension-0.8.2)
set(OPENBFME_LUAGD_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/src/Libraries/LuaGDExtension)
set(OPENBFME_LUAGD_STAGE ${CMAKE_CURRENT_BINARY_DIR}/luagd)
# the sources are built from a staging folder: the pristine src/, then the patch layer (src/Libraries/LuaGDExtension/src/, the files
# marked ALTERED SOURCE VERSION, README.md says why) copied over them by path (configure_file: CMake re-runs when either side changes)
file(GLOB_RECURSE OPENBFME_LUAGD_PRISTINE_FILES RELATIVE ${OPENBFME_LUAGD_DIR}/src CONFIGURE_DEPENDS ${OPENBFME_LUAGD_DIR}/src/*)
foreach(f ${OPENBFME_LUAGD_PRISTINE_FILES})
    if(EXISTS ${OPENBFME_LUAGD_PATCH}/src/${f})
        configure_file(${OPENBFME_LUAGD_PATCH}/src/${f} ${OPENBFME_LUAGD_STAGE}/src/${f} COPYONLY)
    else()
        configure_file(${OPENBFME_LUAGD_DIR}/src/${f} ${OPENBFME_LUAGD_STAGE}/src/${f} COPYONLY)
    endif()
endforeach()
# upstream's generated headers (src/generated/, included as "../generated/<name>")
set(OPENBFME_LUAGD_GENERATED
    ${OPENBFME_LUAGD_STAGE}/src/generated/global_enums.hpp
    ${OPENBFME_LUAGD_STAGE}/src/generated/utility_functions.hpp
    ${OPENBFME_LUAGD_STAGE}/src/generated/package_searcher.h
    ${OPENBFME_LUAGD_STAGE}/src/generated/lua_script_globals.h
    ${OPENBFME_LUAGD_STAGE}/src/generated/variant_type_constants.hpp)
string(REPLACE "." "-" OPENBFME_LUAGD_API_DASHED "${GODOTCPP_API_VERSION}")
set(OPENBFME_LUAGD_API_JSON ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/godot-cpp/gdextension/extension_api-${OPENBFME_LUAGD_API_DASHED}.json)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
    OUTPUT ${OPENBFME_LUAGD_GENERATED}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/luagd_generate.py ${OPENBFME_LUAGD_DIR} ${OPENBFME_LUAGD_API_JSON}
            ${OPENBFME_LUAGD_STAGE}/src/generated
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/luagd_generate.py ${OPENBFME_LUAGD_DIR}/tools/code_generation/generate_cpp_code.py
            ${OPENBFME_LUAGD_DIR}/src/luaopen/package_searcher.lua ${OPENBFME_LUAGD_DIR}/src/script-language/globals.lua ${OPENBFME_LUAGD_API_JSON}
    COMMENT "Generating lua-gdextension sources"
    VERBATIM)

set(OPENBFME_LUAGD_SOURCES)
foreach(f ${OPENBFME_LUAGD_PRISTINE_FILES})
    # src/main.cpp is the entry point: LuaGDExtensionRegister.cpp registers the classes
    if(f MATCHES "\\.cpp$" AND NOT f STREQUAL "main.cpp")
        list(APPEND OPENBFME_LUAGD_SOURCES ${OPENBFME_LUAGD_STAGE}/src/${f})
    endif()
endforeach()
add_library(openbfme_tree_sitter STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter/lib/src/lib.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter-lua/src/parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter-lua/src/scanner.c)
target_include_directories(openbfme_tree_sitter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter/lib/src)
target_include_directories(openbfme_tree_sitter SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter/lib/include)
set_target_properties(openbfme_tree_sitter PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 11)

# LuaGDExtensionRegister.cpp: the registration and the one place outside the addon that names its types (built with its settings)
add_library(openbfme_luagd STATIC ${OPENBFME_LUAGD_SOURCES} ${OPENBFME_LUAGD_GENERATED} ${CMAKE_CURRENT_SOURCE_DIR}/src/GodotDevice/LuaGDExtensionRegister.cpp)
target_include_directories(openbfme_luagd PRIVATE ${OPENBFME_LUAGD_STAGE}/src ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sol2/include)
target_link_libraries(openbfme_luagd PUBLIC godot-cpp openbfme_luajit openbfme_tree_sitter)
# the defines stay private: nothing outside this library includes the addon or sol2
# upstream SConstruct + tools/luajit.py + tools/sol2.py
target_compile_definitions(openbfme_luagd PRIVATE LUAJIT SOL_LUAJIT=1 SOL_USING_CXX_LUA=0 SOL_EXCEPTIONS_SAFE_PROPAGATION=1 SOL_NO_NIL=0)
target_compile_definitions(openbfme_luagd PRIVATE $<$<CONFIG:Debug>:SOL_ALL_SAFETIES_ON=1 SOL_PRINT_ERRORS=1>)
# LuaJIT's names with the prefix, and the forwarders sol2's compatibility layer needs (src/Libraries/LuaJIT/sol_bridge.h); godot-cpp 10
# no longer brings Ref<T> in with ref_counted.hpp, which upstream (written against godot-cpp's 4.6 branch) relies on
set(OPENBFME_LUAGD_FORCED ${CMAKE_CURRENT_SOURCE_DIR}/src/Libraries/LuaJIT/sol_bridge.h ${CMAKE_CURRENT_SOURCE_DIR}/src/Libraries/LuaJIT/godot_cpp_compat.h)
foreach(h ${OPENBFME_LUAGD_FORCED})
    if(MSVC)
        target_compile_options(openbfme_luagd PRIVATE "/FI${h}")
    else()
        target_compile_options(openbfme_luagd PRIVATE "SHELL:-include ${h}")
    endif()
endforeach()
target_compile_definitions(openbfme_luagd PRIVATE LUAJIT_ENABLE_LUA52COMPAT)
target_include_directories(openbfme_luagd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/Libraries/LuaJIT ${OPENBFME_LUAJIT_INCLUDE}/luajit)
set_target_properties(openbfme_luagd PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(openbfme_luagd PRIVATE /EHsc /bigobj /utf-8 /W0)
else()
    target_compile_options(openbfme_luagd PRIVATE -fexceptions -w)
endif()
