# Lane ANTICHEAT-1: Palantír detector D1 (orders on objects the player could not see), run while a replay is re-simulated (openbfme_peer --fog-check).
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. Not simulation code: it only reads the logic.
set(OPENBFME_ANTICHEAT_SOURCES
    src/GameNetwork/OrdersIntoFog.cpp
    src/GameNetwork/CommandStreamChecks.cpp
    src/GameNetwork/ViewChecks.cpp
)
set(OPENBFME_ANTICHEAT_TESTS
    tests/test_orders_into_fog.cpp
    tests/test_orders_into_fog_retail.cpp
)
