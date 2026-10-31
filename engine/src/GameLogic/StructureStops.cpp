// OpenBFME. GPL-3.0.
// See GameLogic/StructureStops.h.

#include "GameLogic/StructureStops.h"

std::vector<std::string> StructureStops::lines()
{
	return {
		"[S-340] structure collapse: StructureCollapseUpdate follows RW 0x8A7D1D (phases, the logic random draws in retail order, the fall under GameData Gravity, the POST_RUBBLE state, DestroyObjectWhenDone); "
		"the FX lists and OCLs a phase fires are the client's / the OCL lane's (the random pick is drawn, the use counted: a retail OCL may draw more logic random numbers), the drawable shudder "
		"(client random) and instance matrix, the BoneFXUpdate stop and the deselection (InGameUI forgets a destroyed object) are not ported; StructureToppleUpdate has no runtime",
		"[S-341] geometry: the collapse height (RW 0xAD1920), the pathfinder footprint (RW 0x936B7D) and the bounding circle / sphere of the combat distance and the footprint size (RW 0xAD2860) "
		"read every shape of the template's Geometry rows (lane PATH-2) with the object's own active flags (Object::geometryShapes, Object + 0xA8: the GeometryUpgrade "
		"and gate shape switches, lane MODCOMPAT-MOD1), the building placement overlap (BuildPlacement::footprintsOf) counts every active shape and LARGE_RECTANGLE_PATHFIND boxes use shape 0 only (INFERRED: their RW readers were not traced)",
		"[S-342] damage state: ActiveBody::updateDamageState sets the model conditions DAMAGED / REALLYDAMAGED / RUBBLE, not while the object is UNDER_CONSTRUCTION "
		"unless dead (RW 0x8C3290 -> vslot 0x94; INFERENCE: the flags vslot 0x94 sets are ZH Drawable::reactToBodyDamageStateChange's) and, for a STRUCTURE entering RUBBLE, re-enters its pathfinder footprint, sets NO_COLLISIONS and releases its command points (RW 0x8C2A5D, 0x8C1BB7); not ported: the "
		"rubble geometry height (StructureRubbleHeight: it changes the object's shape heights; only the shapes' active flags are per object here), the production reset, RemoveUpgradesOnDeath, the Damaged / ReallyDamaged attribute modifiers, the "
		"damage FX transitions, and the Bookend variant of WALK_ON_TOP_OF_WALL templates",
		"[S-343] body classes: RespawnBody classes a lethal hit (CanRespawn, PermanentlyKilledByFilter) and tells the RespawnUpdate (RW 0x8B3349 / 0x8B3744, lane HERO-1: S-851 .. "
		"S-854); DelayedDeathBody follows RW 0x8C5828 (the FX list is the client's, `checked` has no setter caller yet) and needs LifetimeUpdate (ported: RW 0x7A7F8B, no HULK lifetime "
		"override); SymbioticStructuresBody forwards to a symbiote body that nothing links yet (the writer of RW object + 0x104 was not found; an unlinked one answers health 0 and takes no damage); "
		"CastleMemberBehavior's DAMAGE interface sets its breached flag and counts the EVA, the castle's alert (RW 0x79B374) and the consumer of KeepDeathKillsEverything were not found",
	};
}
