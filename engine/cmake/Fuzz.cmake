# Build with CC=clang CXX=clang++ cmake -S engine -B engine/build-fuzz -G Ninja
#   -DOPENBFME_BUILD_GODOT=OFF -DOPENBFME_FUZZ=ON
# The normal build does not compile or link any of these targets.
set(OPENBFME_PARSER_FUZZ_TARGETS
    big csf str ini map w3d apt_const apt apt_dat apt_geometry
    tga audio replay save cursor vp6 json refpack font map_cache text_file)

set(_index 0)
foreach(_parser IN LISTS OPENBFME_PARSER_FUZZ_TARGETS)
    math(EXPR _index "${_index} + 1")
    add_executable(fuzz_${_parser} fuzz/ParserFuzz.cpp)
    target_compile_definitions(fuzz_${_parser} PRIVATE OPENBFME_FUZZ_TARGET=${_index})
    target_link_libraries(fuzz_${_parser} PRIVATE openbfme_core)
    target_link_options(fuzz_${_parser} PRIVATE -fsanitize=fuzzer,address,undefined)
endforeach()

add_custom_target(openbfme_fuzz_targets DEPENDS
    fuzz_big fuzz_csf fuzz_str fuzz_ini fuzz_map fuzz_w3d fuzz_apt_const fuzz_apt
    fuzz_apt_dat fuzz_apt_geometry fuzz_tga fuzz_audio fuzz_replay fuzz_save
    fuzz_cursor fuzz_vp6 fuzz_json fuzz_refpack fuzz_font fuzz_map_cache fuzz_text_file)
