# Lane BACKLOG4 (ports behind S-529, S-813 and S-1222 from the byte-matched BFME2 decomp): tests. Included from engine/CMakeLists.txt so the lane's file
# lists stay out of the shared list.
set(OPENBFME_BACKLOG4_TESTS
    tests/test_backlog4_teleport.cpp
    tests/test_backlog4_powers.cpp
    tests/test_backlog4_defect.cpp
)
