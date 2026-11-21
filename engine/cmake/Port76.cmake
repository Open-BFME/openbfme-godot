# PORT-76: SpawnBehavior/FadeInTime - a spawned unit's drawable fades in over FadeInTime * 0.03 client frames (issue fork issue 208; RW 0x862C18 .. 0x862C43 ->
# Drawable::fadeIn RW 0x670AA2). The engine change is in SpawnBehavior.cpp, a file other lists already carry; the tests are synthetic (no game files).

set(OPENBFME_PORT76_TESTS
    tests/test_port_76.cpp
)