# Lane OPTS-2: the Options menu (RotWK's detail levels applied: GameLODManager RW 0x6020B4 / 0x601C62 / 0x6019D1, the OptionPreferences getters of
# GlobalData::applyOptionPreferences RW 0x641DED) and the OpenBFME extras screen (GDScript). Client only. Included from engine/CMakeLists.txt so the
# lane's file lists stay out of the shared list.
set(OPENBFME_OPTS2_TESTS
    tests/test_opts2_options.cpp
)
