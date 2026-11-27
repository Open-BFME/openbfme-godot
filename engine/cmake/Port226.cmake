# The idle state's stance countdown (RotWK's AIIdleState::update RW 0x7553AB, the counter + 0x22 set again at onEnter RW 0x7489B6, the idle state's
# failure id 0x4E RW 0x7537E0 and AIGoingIdleState's onEnter RW 0x743864 -> StancesBehavior::onMoveEnd RW 0x86228E).

set(OPENBFME_PORT226_TESTS
    tests/test_port_226.cpp)
