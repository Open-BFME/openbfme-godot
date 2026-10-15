# Lua 5.4.9 companion VM for the Second Age-style API. Retail scripts still use Lua 4.
set(LUA54_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lua-5.4.9/src)
file(GLOB LUA54_SOURCES CONFIGURE_DEPENDS ${LUA54_DIR}/*.c)
list(REMOVE_ITEM LUA54_SOURCES ${LUA54_DIR}/lua.c ${LUA54_DIR}/luac.c)
add_library(openbfme_lua54 STATIC ${LUA54_SOURCES})
set_target_properties(openbfme_lua54 PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(openbfme_lua54 PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/src/Libraries/Lua54/prefix.h)
else()
    target_compile_options(openbfme_lua54 PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/src/Libraries/Lua54/prefix.h)
endif()
# No dynamic native modules: the companion exposes only the explicitly loaded libraries.
