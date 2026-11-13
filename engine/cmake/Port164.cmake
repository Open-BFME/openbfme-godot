# PORT-164: the GiantBird swoop / attack contact point through getBestContactPoint mode 1 (RW 0x76B6FF .. 0x76B721 -> RW 0x690D20 ->
# RW 0xAD3187 -> RW 0xAD3340: the template's highest GeometryContactPoint, z clamped to the geometry height). The engine change is in files other
# lists already carry (GameLogic/Object/ObjectGeometry.cpp, GameLogic/Module/GiantBirdAIUpdate.cpp).

set(OPENBFME_PORT164_TESTS
    tests/test_port_164.cpp)
