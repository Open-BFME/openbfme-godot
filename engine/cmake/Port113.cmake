# PORT-113: the light points of the War of the Ring. TheLightPointSystem (RW 0xDE8C04, the LightPointLevel block RW 0x830EB5) and the
# player's tracker (Player + 0x338) are src/Common/LightPointSystem.cpp; the script actions 351 .. 354 / 405 (RW 0x7BD2D0, 0x7BD311, 0x7BD34F, 0x7BD390,
# 0x7C36BD) are in ScriptActionsOrders.cpp, the condition 124 (RW 0x7E5B56) in ScriptConditions.cpp, the special powers' test and cost (RW 0x6AA8FB,
# 0x6AA91D) in the special power modules. Included from engine/CMakeLists.txt.

# simulation sources (sim audit manifest): the tracker is player state (hashed, saved with the player); integer arithmetic only
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/LightPointSystem.cpp
)
set(OPENBFME_PORT113_TESTS
    tests/test_port_113.cpp
)
