# Lane CAMPUI-1 (the stance toggle's radial on the Palantir: InGameCommandButtonSubMenu RW 0x9D3380 .. 0x9D3550, InGameToggleStanceCommandButton
# RW 0x97004A / 0x96FAC6 / 0x96F53C). Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client-only sources (src/GameClient/GUI/AptScreens/*: excluded from the simulation audit in tools/sim/sim_policy.json)
set(OPENBFME_CAMPUI1_STANCE_SOURCES
    src/GameClient/GUI/AptScreens/InGameCommandButtonSubMenu.cpp
)
set(OPENBFME_CAMPUI1_STANCE_TESTS
    tests/test_campui1_stance.cpp
)
