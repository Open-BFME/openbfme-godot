# The simulation floating-point contract (lane WEAPON-1, PLAN rule 3).
#
# Lockstep multiplayer needs every build of the simulation (Windows/MSVC, Linux/GCC, Clang) to produce the same bits. That holds only
# if no compiler may fuse a multiply into an add, evaluate in a wider format, reassociate, or optimise across the rounding mode. This
# file defines ONE interface target, `openbfme_sim_fp`, that carries the flags; it is linked PUBLIC by openbfme_core and by every
# simulation-bearing library (openbfme_lua), so the requirement also reaches every consumer of an inline simulation header.
#
#   MSVC          /fp:strict                              (+ /arch:SSE2 when the target is 32-bit x86)
#   GCC / Clang   -fno-fast-math -ffp-contract=off -frounding-math   (+ -msse2 -mfpmath=sse when the target is 32-bit x86)
#
# Conflicting overrides are rejected at configure time (flags in CMAKE_<LANG>_FLAGS*, link-time optimisation) and again, on the
# generated compile commands, by tools/sim/sim_audit.py (the audit that CI runs after configure/build).
#
# OPENBFME_SIM_SOURCES (set in CMakeLists.txt) is THE manifest of simulation sources: the sources registered with target_sources() and
# the list the audit classifies. engine/cmake/SimFp.cmake writes it to <build>/sim_manifest.json next to the required flags.

add_library(openbfme_sim_fp INTERFACE)

# The manifest of simulation sources: initialised HERE (included before every lane's cmake file) so that a lane's .cmake file may append to it.
set(OPENBFME_SIM_SOURCES)

set(OPENBFME_SIM_X86_32 OFF)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    if(MSVC)
        # MSVC reports the target through CMAKE_CXX_COMPILER_ARCHITECTURE_ID (X86, x64, ARM64...)
        if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "X86" OR CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
            set(OPENBFME_SIM_X86_32 ON)
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86|X86|AMD64|x86_64)$")
        set(OPENBFME_SIM_X86_32 ON)
    endif()
endif()

if(MSVC)
    set(OPENBFME_SIM_FP_FLAGS /fp:strict)
    if(OPENBFME_SIM_X86_32)
        list(APPEND OPENBFME_SIM_FP_FLAGS /arch:SSE2)
    endif()
    set(OPENBFME_SIM_FP_FAMILY msvc)
else()
    set(OPENBFME_SIM_FP_FLAGS -fno-fast-math -ffp-contract=off -frounding-math)
    if(OPENBFME_SIM_X86_32)
        list(APPEND OPENBFME_SIM_FP_FLAGS -msse2 -mfpmath=sse)
    endif()
    set(OPENBFME_SIM_FP_FAMILY gnu)
endif()
target_compile_options(openbfme_sim_fp INTERFACE ${OPENBFME_SIM_FP_FLAGS})

# ---- configure-time rejection of conflicting overrides ------------------------------------------------------------------------------
set(_sim_flag_vars CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
set(_sim_link_vars CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
set(_sim_configs DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
foreach(cfg ${_sim_configs})
    list(APPEND _sim_flag_vars CMAKE_C_FLAGS_${cfg} CMAKE_CXX_FLAGS_${cfg})
    list(APPEND _sim_link_vars CMAKE_EXE_LINKER_FLAGS_${cfg} CMAKE_SHARED_LINKER_FLAGS_${cfg} CMAKE_STATIC_LINKER_FLAGS_${cfg}
        CMAKE_MODULE_LINKER_FLAGS_${cfg})
endforeach()

if(MSVC)
    set(_sim_forbidden_regex "(^| )[-/](fp:fast|fp:precise|fp:contract|GL|arch:IA32|arch:SSE([^2]|$)|Qvec-report:0)( |$)")
    set(_sim_lto_regex "(^| )[-/](GL|LTCG[^ ]*)( |$)")
else()
    set(_sim_forbidden_regex "(^| )-(ffast-math|Ofast|funsafe-math-optimizations|fassociative-math|freciprocal-math|ffinite-math-only|fno-signed-zeros|fno-trapping-math|ffp-contract=(fast|on)|mfpmath=387|m80387|mno-sse2|fexcess-precision=fast|fno-rounding-math|ffloat-store)( |$)")
    set(_sim_lto_regex "(^| )-(flto[^ ]*|fuse-linker-plugin|fwhole-program)( |$)")
endif()
foreach(v ${_sim_flag_vars})
    if("${${v}}" MATCHES "${_sim_forbidden_regex}")
        message(FATAL_ERROR "${v}='${${v}}' overrides the simulation floating-point contract (engine/cmake/SimFp.cmake): "
            "no fast-math, contraction, reassociation, x87 evaluation or non-strict /fp mode is allowed in the lockstep simulation")
    endif()
    if("${${v}}" MATCHES "${_sim_lto_regex}")
        message(FATAL_ERROR "${v}='${${v}}' enables link-time optimisation: LTO may inline across the out-of-line rounding operations of "
            "the numeric facade and fuse them (engine/cmake/SimFp.cmake)")
    endif()
endforeach()
# the LINK step counts too: gcc -ffast-math at link time pulls in crtfastmath.o, which sets FTZ / DAZ in MXCSR before main (1F80 -> 9FC0)
foreach(v ${_sim_link_vars})
    if("${${v}}" MATCHES "${_sim_forbidden_regex}")
        message(FATAL_ERROR "${v}='${${v}}' overrides the simulation floating-point contract at link time (engine/cmake/SimFp.cmake): "
            "a fast-math or contraction option on the link line changes the start-up floating-point environment")
    endif()
    if("${${v}}" MATCHES "${_sim_lto_regex}")
        message(FATAL_ERROR "${v}='${${v}}' enables link-time optimisation (engine/cmake/SimFp.cmake)")
    endif()
endforeach()
# directory-wide options (add_compile_options / add_link_options before this file)
foreach(prop COMPILE_OPTIONS LINK_OPTIONS)
    get_directory_property(_sim_dir_opts ${prop})
    string(REPLACE ";" " " _sim_dir_opts "${_sim_dir_opts}")
    if("${_sim_dir_opts}" MATCHES "${_sim_forbidden_regex}" OR "${_sim_dir_opts}" MATCHES "${_sim_lto_regex}")
        message(FATAL_ERROR "directory ${prop} '${_sim_dir_opts}' overrides the simulation floating-point contract (engine/cmake/SimFp.cmake)")
    endif()
endforeach()
set(_sim_ipo_vars CMAKE_INTERPROCEDURAL_OPTIMIZATION)
foreach(cfg ${_sim_configs})
    list(APPEND _sim_ipo_vars CMAKE_INTERPROCEDURAL_OPTIMIZATION_${cfg})
endforeach()
foreach(v ${_sim_ipo_vars})
    if(${v})
        message(FATAL_ERROR "${v} is ON: link-time optimisation is not allowed for the lockstep simulation (engine/cmake/SimFp.cmake)")
    endif()
endforeach()

# Every simulation-bearing target links this: openbfme_sim_register_target(<target>) checks the target-level overrides (LTO property) and applies
# openbfme_sim_fp PUBLIC.
function(openbfme_sim_register_target target)
    get_target_property(_ipo ${target} INTERPROCEDURAL_OPTIMIZATION)
    if(_ipo)
        message(FATAL_ERROR "target ${target}: INTERPROCEDURAL_OPTIMIZATION is not allowed for simulation code (engine/cmake/SimFp.cmake)")
    endif()
    target_link_libraries(${target} PUBLIC openbfme_sim_fp)
endfunction()

# openbfme_sim_check_targets([<targets...>]): call at the END of the top level CMakeLists.txt (no arguments: every target of this directory): rejects forbidden
# compile / link options (and LTO) that a target added after its registration, with target_compile_options / target_link_options (also the interface ones,
# which reach consumers) and with target_link_libraries (an item that starts with `-` is a raw link option: `target_link_libraries(t PRIVATE -ffast-math)`).
function(openbfme_sim_check_targets)
    set(_sim_targets ${ARGN})
    if(NOT _sim_targets)
        get_directory_property(_sim_targets BUILDSYSTEM_TARGETS)
    endif()
    foreach(t ${_sim_targets})
        if(TARGET ${t})
            get_target_property(_sim_type ${t} TYPE)
            set(_sim_props COMPILE_OPTIONS LINK_OPTIONS INTERFACE_COMPILE_OPTIONS INTERFACE_LINK_OPTIONS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            if(_sim_type STREQUAL "INTERFACE_LIBRARY")
                set(_sim_props INTERFACE_COMPILE_OPTIONS INTERFACE_LINK_OPTIONS INTERFACE_LINK_LIBRARIES)
            endif()
            foreach(prop ${_sim_props})
                get_target_property(_o ${t} ${prop})
                if(_o)
                    string(REPLACE ";" " " _o "${_o}")
                    if("${_o}" MATCHES "${_sim_forbidden_regex}" OR "${_o}" MATCHES "${_sim_lto_regex}")
                        message(FATAL_ERROR "target ${t}: ${prop} '${_o}' overrides the simulation floating-point contract (engine/cmake/SimFp.cmake)")
                    endif()
                endif()
            endforeach()
            if(NOT _sim_type STREQUAL "INTERFACE_LIBRARY")
                get_target_property(_ipo ${t} INTERPROCEDURAL_OPTIMIZATION)
                if(_ipo)
                    message(FATAL_ERROR "target ${t}: INTERPROCEDURAL_OPTIMIZATION is not allowed for simulation code (engine/cmake/SimFp.cmake)")
                endif()
            endif()
        endif()
    endforeach()
endfunction()

# The audit (tools/sim/sim_audit.py) reads compile_commands.json and the link commands of the generated build; the generators CMake writes it for are Ninja and Makefiles.

# openbfme_sim_write_manifest(<file>): the manifest and the contract for the audit. Called once, after OPENBFME_SIM_SOURCES is complete.
function(openbfme_sim_write_manifest file)
    set(_json "{\n  \"compiler_family\": \"${OPENBFME_SIM_FP_FAMILY}\",\n  \"x86_32\": ")
    if(OPENBFME_SIM_X86_32)
        string(APPEND _json "true")
    else()
        string(APPEND _json "false")
    endif()
    string(APPEND _json ",\n  \"required_flags\": [")
    set(_first TRUE)
    foreach(f ${OPENBFME_SIM_FP_FLAGS})
        if(NOT _first)
            string(APPEND _json ", ")
        endif()
        string(APPEND _json "\"${f}\"")
        set(_first FALSE)
    endforeach()
    string(APPEND _json "],\n  \"source_dir\": \"engine\",\n  \"sources\": [")
    set(_first TRUE)
    foreach(s ${OPENBFME_SIM_SOURCES} ${OPENBFME_SIM_SOURCES_GODOT})
        if(NOT _first)
            string(APPEND _json ",")
        endif()
        string(APPEND _json "\n    \"${s}\"")
        set(_first FALSE)
    endforeach()
    string(APPEND _json "\n  ]\n}\n")
    file(WRITE ${file} "${_json}")
endfunction()
