# PORT-316 (fork issue 752, S-1583): a drawable's model condition flush points on the creation paths and before the queries that read the
# draw modules. RW 0x690F02 Object::friend_bindToDrawable hands the new drawable the object's whole condition set (RW 0x68D607 ->
# RW 0x679512) plus the map's NIGHT / SNOW (RW 0x690F8B .. 0x690FD0); ClientEventRecorder::objectCreated carries it in the CREATED event and
# DrawableManager::created gives it to the draw modules at once. Client-only: no simulation state, no crc / xfer.
set(OPENBFME_PORT316_TESTS
    tests/test_port_316.cpp)
