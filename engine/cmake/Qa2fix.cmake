# Lane QA2-FIX (QA-2 #3 / #5: a player's wall span through the HUD; the lobby after a game): tests. Included from engine/CMakeLists.txt so the lane's file
# lists stay out of the shared list. No new simulation source (the placement translator and the control bar are client code already in the manifest).
set(OPENBFME_QA2FIX_TESTS
    tests/test_qa2fix_walls.cpp
)
