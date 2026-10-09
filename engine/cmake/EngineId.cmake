# The engine compatibility id of the profile identity (lane MP-1; GameNetwork/ProfileIdentity.h): SHA-256 of a canonical manifest of everything that decides
# what the simulation computes, so two builds share an id exactly when they run the same simulation contract. The Git commit is PROVENANCE only
# (OPENBFME_GIT_PROVENANCE, never part of the id): a source export, a machine without Git or a dirty tree gets the id of its actual contents.
#
# The manifest (text, LF):
#   openbfme-engine-id 1
#   config x86_32=<ON|OFF> options=<OPENBFME_ENGINE_ID_OPTIONS>        (the simulation-affecting configuration: the 32-bit x86 SSE2 contract of cmake/SimFp.cmake and
#                                                                       the explicit option string; the compiler itself is not part of it: the floating-point
#                                                                       contract flags and tools/sim/sim_audit.py hold every compiler to the same bits)
#   file <relative path> <sha256>   for every file under src/, cmake/, data/ and thirdparty/lua-4.0.1/, and CMakeLists.txt, sorted by path
# Text files are hashed with CRLF turned into LF (a Windows checkout and a Linux one are the same source). The tests, tools and docs do not feed the simulation.
#
# A release pipeline that builds from an export may supply the id it computed from the pristine tree with -DOPENBFME_ENGINE_ID=<64 hex>: it is validated and
# used as is. With no id supplied and no hashable sources the configure step FAILS (never a shared sentinel).
#
# Usable in script mode: cmake -DOPENBFME_ID_ROOT=<engine dir> [-DOPENBFME_ID_X86_32=ON] [-DOPENBFME_ID_OPTIONS=...] -P EngineId.cmake  (prints "engine-id <hex>")

function(openbfme_compute_engine_id root x86_32 options out_id out_files)
    set(_manifest "openbfme-engine-id 1\nconfig x86_32=${x86_32} options=${options}\n")
    set(_files)
    foreach(_dir src cmake data thirdparty/lua-4.0.1)
        if(IS_DIRECTORY "${root}/${_dir}")
            file(GLOB_RECURSE _found RELATIVE "${root}" "${root}/${_dir}/*")
            list(APPEND _files ${_found})
        endif()
    endforeach()
    if(EXISTS "${root}/CMakeLists.txt")
        list(APPEND _files CMakeLists.txt)
    endif()
    list(SORT _files)
    list(REMOVE_DUPLICATES _files)
    set(_count 0)
    foreach(_rel IN LISTS _files)
        if(IS_DIRECTORY "${root}/${_rel}")
            continue()
        endif()
        if(_rel MATCHES "^src/")
            math(EXPR _count "${_count} + 1")
        endif()
        file(READ "${root}/${_rel}" _text)
        string(REPLACE "\r\n" "\n" _text "${_text}")
        string(SHA256 _h "${_text}")
        string(APPEND _manifest "file ${_rel} ${_h}\n")
    endforeach()
    if(_count EQUAL 0)
        message(FATAL_ERROR "OpenBFME engine id: no engine sources found under ${root}/src: there is no trustworthy "
                            "identity to give the build; pass -DOPENBFME_ENGINE_ID=<64 hex> from the release pipeline")
    endif()
    string(SHA256 _id "${_manifest}")
    set(${out_id} "${_id}" PARENT_SCOPE)
    set(${out_files} "${_files}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE AND CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT OPENBFME_ID_ROOT)
        message(FATAL_ERROR "OPENBFME_ID_ROOT is required")
    endif()
    if(NOT OPENBFME_ID_X86_32)
        set(OPENBFME_ID_X86_32 OFF)
    endif()
    openbfme_compute_engine_id("${OPENBFME_ID_ROOT}" "${OPENBFME_ID_X86_32}" "${OPENBFME_ID_OPTIONS}" _id _files)
    message(STATUS "engine-id ${_id}")
endif()
