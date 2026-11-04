# Lane SAVE-4b (docs/SAVES.md phase SAVE-4 part (b)): the objects, their modules, the scheduler and the object-side subsystems in the snapshot. Included
# from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): they move the objects' state in and out between frames (no arithmetic)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/SnapshotXfer.cpp
    src/GameLogic/GameLogicObjectSnapshot.cpp
)
set(OPENBFME_SAVE4B_TESTS
    tests/test_cah_monitor_save.cpp
    tests/test_save4b.cpp
    tests/test_save4b_retail.cpp
)
