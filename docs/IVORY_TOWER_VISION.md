# Ivory Tower vision defaults

Editions: the shared vision default applies to BFME2 and RotWK; BFME1 retains
its existing behavior. No edition-specific difference is established here.

BFME2 donor: ThingTemplateConstructor.cpp (RVA 0x33E1B8) initializes
ShroudClearingRange to -1, and ObjectConstructor.cpp (RVA 0x298EA9) substitutes
VisionRange for that sentinel. Explicit zero still suppresses sight.

RotWK data: SpecialPowerIvoryTowerVision creates IvoryTowerVisionPing, whose
VisionRange is 99999 and whose inherited template has no ShroudClearingRange.
The old BFME1-only substitution therefore left the ping blind.

Tests cover omitted, explicit -1 and explicit zero clearing ranges, and the
mounted retail ping clearing both map corners for its owner while leaving the
enemy shrouded. These are engine regression tests using retail data, not a
retail executable oracle. RotWK target-binary provenance remains subject to
S-001; the implementation follows the BFME2 donor rather than claiming proven
clean RotWK 2.01 equivalence.

Merge queue q34: the code is SPELLFIX-1's identical rule with its RotWK cites (the ThingTemplate ctor's -1, RW 0x73FB7A; the Object ctor's
fallback to VisionRange, RW 0x69990F), so this change merged as tests and this note only.
