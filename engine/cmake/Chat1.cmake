# Lane CHAT-1 (in-game chat: All / Allies, the message lines, mute; multiplayer beacons): client sources and tests. Included from engine/CMakeLists.txt so
# the lane's file lists stay out of the shared list. The chat is client state and direct network commands only; the beacon's executor (below) is
# simulation code, as in retail.
set(OPENBFME_CHAT1_CLIENT_SOURCES
    src/GameClient/InGameChat.cpp                       # the chat window's rules (modes, recipients, formats, scroll-back)
    src/GameClient/InGameUIMessages.cpp                 # the InGameUI message lines (chat lines and GUI notices)
    src/GameClient/GUI/AptScreens/AptInGameChat.cpp     # InGameChat.apt
    src/GameClient/BeaconClientUpdate.cpp               # the multiplayer beacon's client module (minimap pulses, hidden for enemies)
)
set(OPENBFME_CHAT1_TESTS
    tests/test_chat1.cpp
    tests/test_chat1_retail.cpp
)
# the multiplayer beacon's executor is simulation code (MSG_PLACE_BEACON runs on every peer in lockstep, RW 0x77C634)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/BeaconCommands.cpp
)
