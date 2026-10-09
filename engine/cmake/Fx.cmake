# FX-1 (FXList, particle systems and emitters): sources and tests for openbfme_core / openbfme_tests.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_FX_SOURCES
    src/Common/GameClientRandomVariable.cpp
    src/GameClient/FXParticleSystem.cpp
    src/GameClient/FXList.cpp
    src/GameClient/FXListObjectFilter.cpp
    src/GameClient/ParticleSys.cpp
    src/GameClient/FXPlayback.cpp
    src/GameClient/ParticleDraw.cpp
    src/GameClient/ParticleTexture.cpp
    src/Libraries/WWVegas/WW3D2/part_emt.cpp)

set(OPENBFME_FX_TESTS
    tests/test_fx_particle_ini.cpp
    tests/test_fx_particles.cpp
    tests/test_fx_draw.cpp
    tests/test_fx_retail.cpp
    tests/test_w3d_emitter_sim.cpp
    tests/test_fx_review.cpp)
