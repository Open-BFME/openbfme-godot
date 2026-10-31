# Lane MP-DROP-1 (stop S-4556: a player who crashes or drops must never freeze an online game): tests. Included from engine/CMakeLists.txt so the
# lane's file list stays out of the shared list. No new sources: the fixes are in GameNetwork/DisconnectManager / Network (already simulation-audited).
set(OPENBFME_MPDROP1_TESTS
    tests/test_mpdrop1_disconnect.cpp
    tests/test_mpdrop3_farewell.cpp) # MPDROP-3: a dropped player always learns it was dropped

# called once openbfme_tests exists
macro(openbfme_mpdrop1_targets)
    target_sources(openbfme_tests PRIVATE ${OPENBFME_MPDROP1_TESTS})
endmacro()
