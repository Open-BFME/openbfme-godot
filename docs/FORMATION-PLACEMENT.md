# Two-button formation placement

Hold both mouse buttons and drag with units selected. The second press must be
strictly within 500 ms and within five pixels on each axis. Either release sends
one formation command; Escape cancels.

The mounted `formationassistant.ini` supplies ordered UnitDefinition filters,
formation row/cell candidates, repeated rows, width selection limits, padding,
preview textures and their dimensions. Cells fill from the middle outward, using
candidate priority and distance-sorted selected objects. Preview and simulation
share this layout. Release sends MSG_DO_MOVETO_FORMATION (1124) with position,
facing, template index and pending attack-move flag. Over a firing-arc object,
the gesture emits MSG_DO_ROTATE_FIRINGARC (1127) with the aimed position.

The preview uses the game's FormationPreviewItemDecal/FormationPreviewDecal
textures instead of green line circles. Battalion previews expand their horde
slots into individual figures. Figures are tinted with the local player's color;
head/body/base facing arrows use their configured artwork and dimensions.
After release, the preview remains for 45 client ticks and fades over 15 ticks.
Textures remain in the player's installed game files, never in this repository.

Recovered sources in Open-BFME-2, under `Code/GameEngine/Source`:

- `GameClient/FormationTranslatorRva00431C9D.cpp`, `FormationTranslatorRva00432038.cpp`:
  gesture start, first-press anchor and pending-command checks.
- `Common/GapBodiesS3E_Rva003F9CD2.cpp`, `Common/INI/FormationTemplate.cpp`,
  `GameLogic/System/FormationAssistantParseFormation.cpp`,
  `Common/INI/FormationSelectionCriteriaRva00424185.cpp`: INI fields and rows.
- `Common/Rva004233F5Lookup.cpp`, `Common/System/FormationBuild.cpp`,
  `FormationBuildRow.cpp`, `FormationAssistant.cpp`: selection and placement.
- `GameClient/InGameUIUpdateFormationPreview.cpp`, `FormationPreviewPoolLookup.cpp`,
  `Rva0029B1C2.cpp`, `Common/Rva0029E159Audio.cpp`: textures, figures, arrows and fade.
- `GameClient/InGameUIFormationFinalize.cpp`, `GameLogic/System/GameLogicDispatch.cpp`:
  message payloads. Firing-arc direction: `GameLogic/AI/AIGroupScatter.cpp`.

S-4464 records the remaining parity limits: native group pathing/slot optimization,
activation settings for modified data, firing-arc AI state behavior, and independent
RotWK verification. Current movement uses the port's existing per-slot movement
and locomotor facing. This change does not claim a complete byte-identical port.

The port treats each preview cell as the centre of its occupied ranks. Movement
subtracts the rotated rank centroid from the battalion object's destination;
figures use the same occupied slot coordinates and facing as HordeContain. This
corrects the differing object origins of mixed battalions (Knights and Archers)
and keeps casualties from substituting unrelated slots in the preview. This is
an explicit adaptation of the current per-slot movement, not a claim that the
native formation-group execution has been recovered. Solo figures reserve at
least their decal dimensions, and successive rows use both rows' half-depths,
so hero icons and rows of different sizes have space between them.

`mixed_formation_placement_test.gd` exercises three Gondor Knight battalions and
one Gondor Archer battalion, checking their soldiers' centres after arrival
against the preview cells. It approaches clear ground from the north; terrain
and blocked destinations can still legitimately prevent a requested formation.
