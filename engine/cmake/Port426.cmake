# PORT-426 (fork issue 426, P-108): Shift on a UNIT_BUILD button sends the batch flag (RW 0x940C3E),
# the logic queues five units (RW 0x8A1260). Synthetic INI, no game files.
set(OPENBFME_PORT426_TESTS tests/test_port_426.cpp)
