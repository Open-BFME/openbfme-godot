# Lane WIN-1 (the Windows build; cross-OS lockstep): sources and tests.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): retail's INI real parser, MSVCR71's sscanf "%f" ported (every OS reads every INI real to retail's bits)
set(OPENBFME_WIN1_SOURCES
    src/Common/INI/Msvcr71Real.cpp
)
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_WIN1_SOURCES})
set(OPENBFME_WIN1_TESTS
    tests/test_win1_crt_parse.cpp
    tests/test_win1_trig.cpp
    tests/test_win1_msvcr71_real.cpp
)
