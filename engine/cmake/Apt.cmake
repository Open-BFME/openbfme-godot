# EA Apt player (original-menus lane): sources and tests for the openbfme_core library.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_APT_SOURCES
    src/Libraries/Source/Apt/AptFile.cpp
    src/Libraries/Source/Apt/AptActionDecoder.cpp
    src/Libraries/Source/Apt/AptLoad.cpp
    src/Libraries/Source/Apt/AptValue.cpp
    src/Libraries/Source/Apt/AptObject.cpp
    src/Libraries/Source/Apt/AptActionInterpreter.cpp
    src/Libraries/Source/Apt/AptNativeHash.cpp
    src/Libraries/Source/Apt/AptCharacterInst.cpp
    src/Libraries/Source/Apt/AptButtonInst.cpp
    src/Libraries/Source/Apt/Apt.cpp
    src/Libraries/Source/Apt/AptNatives.cpp
    src/Libraries/Source/Apt/AptInput.cpp
    src/Libraries/Source/Apt/AptRenderList.cpp
    # APT-3: the Godot-independent half of the renderer (render list -> canvas operations), string table, font substitution
    src/GameClient/AptCanvas.cpp
    src/GameClient/FontSubstitution.cpp
    src/GameClient/GameText.cpp)

set(OPENBFME_APT_TESTS
    tests/test_apt_file.cpp
    tests/test_apt_corpus.cpp
    tests/test_apt_value.cpp
    tests/test_apt_interpreter.cpp
    tests/test_apt_player.cpp
    tests/test_apt_player_review.cpp
    tests/test_apt_input.cpp
    tests/test_apt_menu.cpp
    tests/test_fb7_menus.cpp # lane FB7-1 round 2
    tests/test_apt_canvas.cpp)

# Shell, WindowManager, native gadgets, skirmish setup (lane APT-4)
include(${CMAKE_CURRENT_LIST_DIR}/AptShell.cmake)

# The corpus golden (tests/data/apt/*.json) is read at run time from the source tree.
function(openbfme_apt_configure_tests target)
    target_compile_definitions(${target} PRIVATE
        OPENBFME_APT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/apt")
endfunction()
