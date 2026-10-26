// OpenBFME. GPL-3.0.
//
// The acceptance stops of lane PROD-1 (S-201 .. S-209, docs/STOPS.md) as report lines. Reported by RetailObjectWorld::acceptanceStops() and pinned by
// tests/test_prod_production.cpp; S-200 (CommandButton cross references) is CommandStore::acceptanceStops(), S-208 (the dispatcher's coverage) is
// GameLogicDispatch::acceptanceStops() because it carries run-time counts.

#pragma once

#include <string>
#include <vector>

namespace ProductionStops
{
inline std::vector<std::string> lines()
{
	return {
		"[S-201] production hands the produced object to its AI through GameLogic::aiCommands() (GameLogic/AI/AICommandSink.h): aiFollowExitProductionPath, aiIdle, aiMoveToPosition "
		"run in a game with an AIWorld (lane MOVE-1 installs the handler: the unit walks to its rally point); a logic without an AIWorld only records them and counts them as "
		"unexecuted, as does an object without an AI module",
		"[S-202] the exit modules skip the pathfinder and partition calls (snapPosition RW 0x6EF225, adjustDestination RW 0x6FE456 and moveAlliesAwayFromDestination RW 0x6F85A6 but in the queue exit and ProductionUpdate (lane MOVE-3), "
		"addObjectToPathfindMap RW 0x6E85E9), the physics kick of an airborne exit (RW 0x792DBD), the rally override search (queryRallyOverride RW 0x8A3AB4: no Slaughter "
		"contain is ported), the rally path validation of RW 0x779544 and the player group condition of releaseLastExit (RW 0x6A950B)",
		"[S-203] ProductionUpdate does not port (hero / build-index entries are HERO-1's since S-850 .. S-855, upgrade entries UPGRADE-1's, S-486; the producer's experience, "
		"VeteranUnitsFromVeteranFactory and the PRODUCTION rate are PRODUCTION1's): audio (voices, the speed bonus loop) and EVA messages, "
		"the speed bonus model condition, script naming of a produced object, the horde join of entry + 0x3C, the pending template name set (RW +0x130) and the score keeper",
		"[S-204] build cost and time use neutral inputs where their sources are not ported: the lobby handicap (RW 0x7B19BF), the energy ratio of calcTimeToBuild (RW 0x8E3503), "
		"the Brutal AI reductions, the MultipleFactory facility count (an APPEARS_AT_RALLY_POINT template with GameData MultipleFactory != 1.0 "
		"throws), the Create-A-Hero surcharge (the CostModifierUpgrade factor RW 0x6AD8A7 IS applied since UPGRADE-1)",
		"[S-205] closed by UPGRADE-1: the UpgradeCenter, the upgrade masks, the upgrade modules and the research through the production queue are ported "
		"(what remains of the research is S-486)",
		"[S-206] command points: production asks the economy (Economy::canAffordCommandPoints, lane ECON-1: one model, RW 0x6A7C86 / 0x6A7B9F / 0x6A7F79, tier records ported); what the economy "
		"does not do is its own stops S-252 (upgrades), S-253 (attribute modifier of a bonus) and S-254 (that game kind 3 is the skirmish is not verified, the Living World branches are not ported)",
		"[S-207] closed: typed prerequisite parsing, satisfaction and help formatting, script/construction gates, and SpawnUnitBehavior's exact UnitName offer "
		"before command-set lookup (RW 0x794F69 / 0x8BC992) are ported. SpawnUnitBehavior's automatic production update remains separately unported (S-140)",
		"[S-209] SpawnPointProductionExitUpdate takes its spawn bones from the logic's bone provider (lane PRODUCTION1) and, without one (no W3D assets), keeps no door and "
		"reports it; SupplyCenterProductionExitUpdate's setForceWantingState is ported (lane LUMBER1, RW 0x8A9FC4)" };
}
} // namespace ProductionStops
