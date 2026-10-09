# Lane XP-1 (experience, veterancy and skill points: the ExperienceLevel / ExperienceScalarTable / ModifierList stores, the ExperienceTracker, the
# AttributeModifierPoolUpdate, the delayed level grants and the skill point awards): sources and tests.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_XP_SOURCES
    src/GameLogic/ExperienceLevels.cpp
    src/GameLogic/AttributeModifiers.cpp
    src/GameLogic/ExperienceWorld.cpp
    src/GameLogic/Object/ExperienceTracker.cpp
    src/GameLogic/Object/AttributeModifierPool.cpp
    src/GameLogic/Object/Contain/HordeContainExperience.cpp
    src/GameLogic/Module/ExperienceUpgradeModules.cpp
)
set(OPENBFME_XP_TESTS
    tests/test_xp_core.cpp
    tests/test_xp_retail.cpp
)
