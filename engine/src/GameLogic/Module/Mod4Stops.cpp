// OpenBFME. GPL-3.0.
// See GameLogic/Module/Mod4Stops.h.

#include "GameLogic/Module/Mod4Stops.h"

const std::vector<std::string> &Mod4Stops::classes()
{
	static const std::vector<std::string> names = { "PickupStuffUpdate", "ThreatFinderUpdate", "RebuildHoleExposeDie", "RebuildHoleBehavior" };
	return names;
}

std::vector<std::string> Mod4Stops::lines()
{
	return {
		"[S-1422] PickupStuffUpdate (lane MOD-4, not ported; create RW 0x64D5FC, constructor RW 0x895524, table RW 0xC64600: SkirmishAIOnly + 0x08, ScanRange "
		"+ 0x0C, StuffToPickUp + 0x10 (ObjectFilter), ScanIntervalSeconds + 0x14): update RW 0x8957B7 sleeps forever without an AI or, SkirmishAIOnly, without "
		"a skirmish AI (RW 0x6A950B); every ScanIntervalSeconds (RW 0x895741, float frames) while the AI's temporary state machine slot (AI + 0x34) is empty the "
		"first object within ScanRange (near to far) that StuffToPickUp allows for the player (RW 0xBE4CC8), alive and not THROWN_OBJECT (RW 0x76A28D) is "
		"given to the AI as a temporary machine (RW 0x6630A9) running RW 0x7549D7(target, 2); while picking up (RW 0x895572) an idle AI (vslot 0x1B8) gives "
		"the machine back (RW 0x6630D7). Blocked by the AI's temporary state machine and the pickup command, not ported (S-1222)",
		"[S-1423] GeometryUpgrade: custom animation conditions and per-object ShowGeometry / HideGeometry are ported from BFME2 GeometryUpgrade.cpp; "
		"WallBoundsMesh / RampMesh draw hooks and WallUpgradeUpdate navigation-mesh refresh remain unported",
		"[S-1424] ThreatFinderUpdate (lane MOD-4, not ported; create RW 0x64F96A, constructor RW 0x7EDD3D, table RW 0xC4C320: DefaultRadius + 0x08): only a "
		"map object gets a finder: behaviour slot 0xB4 (RW 0x7EDECE, from Object RW 0x694692 with the map object's properties) makes an AIThreatFinder (RW "
		"0x7EDDFB) named by the object's name, the radius the property's real or DefaultRadius, registered by name in TheThreatFinderManager (RW 0xDE8888, RW "
		"0x7EE33A); the update (RW 0x7EDDB6) copies the object's position into it every frame. Its readers are the script action RW 0x7C68AA and condition "
		"RW 0x608BCA (lookup RW 0x7EE260), not ported (the script lanes): the finder would have no reader",
		"[S-1427] RebuildHoleExposeDie / RebuildHoleBehavior (lane MOD-4, not ported; the creep lairs' and some structures' rebuild; die RW 0x889AAF, table RW "
		"0xC616B8: HoleName + 0x38, HoleMaxHealth + 0x3C, FadeInTimeSeconds + 0x40, TransferAttackers + 0x44; RebuildHoleBehavior constructor RW 0x8864EE, table "
		"RW 0xC60230: WorkerRespawnDelay + 0x08, HoleHealthRegen%PerSecond + 0x0C, WorkerObjectName + 0x10): a dead object of a live player that is not the "
		"neutral player (and Object + 0x94 bit 2 clear) leaves a HoleName object (position, angle, geometry, name, max health HoleMaxHealth) whose rebuild "
		"interface gets the template, the id and the spawner's active flag (RW 0x886831); after WorkerRespawnDelay the hole rebuilds the structure through the "
		"BuildAssistant's slot 0x38 (no WorkerObjectName) or a worker's dozer slot 0x1F8 (RW 0x8868C2 .. 0x886B72), passes it its attackers and its cost, and "
		"regenerates. Blocked: the rebuild runs through the construction lanes' build-now and reconstruction paths (RECONSTRUCTING, status 21), not ported "
		"here; a hole without its rebuild would stay for ever, so neither class is bound",
	};
}
