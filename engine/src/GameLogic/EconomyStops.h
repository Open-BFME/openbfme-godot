// OpenBFME. GPL-3.0.
//
// The acceptance stops of lane ECON-1 (S-250 .. S-269, docs/STOPS.md) as report lines. Reported by Economy::report(); pinned by tests/test_econ_*.cpp.

#pragma once

#include <string>
#include <vector>

namespace EconomyStops
{
inline std::vector<std::string> lines()
{
	return {
		"[S-250] TerrainResourceManager: the extent comes from TerrainLogic::getExtent (lo (0, 0), hi = the active boundary * 10) as W3DTerrainLogic does in ZH; retail's body behind TerrainLogic vslot 0x180 "
		"was not read. The blocked cells (cell types 1, 2, 5, 7 of the pathfinder's ground layer) need a Pathfinder: the live game has none yet, so a map's grid is built from a private "
		"Pathfinder (PathfinderResourceTerrain). The client half of the class (decal alpha RW 0x75C1A9, the placement preview observers, the dead-owner erase that RW 0x75C02D does when a client asks "
		"with a player filter) is not ported",
		"[S-251] ScoreKeeper keeps only the money counters of RW + 4 / + 8 / + 0x114; the keep-score switch (GameLogic + 0x98) defaults to on (the game kinds that switch it off were not recovered)",
		"[S-252] the UpgradeCenter is not ported (UPGRADE-1): an Upgrade field of an economy module keeps the upgrade's name, a player's completed upgrades are a set of names, and the "
		"upgrade modules (CommandPointsUpgrade, CostModifierUpgrade) act when the upgrade system calls their upgradeIt / removeIt; the TriggeredBy / ConflictsWith lists are kept as names",
		"[S-253] AttributeModifiers are not ported: no PRODUCTION (RW type 0xD), COMMAND_POINT_BONUS (0x18) or BOUNTY_PERCENTAGE (0x11) modifier can be active (the seam is "
		"Economy::setAttributeModifierProvider); the object's model conditions are not on Object, so `completed` (RW 0x7A075A tests AWAITING_CONSTRUCTION / PARTIALLY_CONSTRUCTED) and "
		"CostModifierUpgrade's PARTIALLY_CONSTRUCTED test use the construction statuses, and AutoDepositUpdate's RUBBLE / POST_RUBBLE / POST_COLLAPSE / GARRISONED tests use a hook "
		"(AutoDepositUpdate::setObjectConditionSource)",
		"[S-254] game context: that GameLogic + 0x114 is 3 in a skirmish (RW 0x6A7C86 and 0x602E64 branch on it) is inference, the new-game message handler (RW 0x779CB4) writes it from "
		"message argument 1 and the sender was not located; the Living World / campaign branches of the command point initialisation (RW 0x6A7CEB, 0x6A7ED1 bonuses) and the "
		"campaign money handicap (RW 0x6AA858 .. 0x6E1F2F) are not ported; the replay clause of the multiplayer test (RW 0x625456) is not ported",
		"[S-255] MultiPlayMoneyMult is parsed for MP1 .. MP8; the retail lookup reads 20 slots (RW 0x642002), the slots past MP8 keep GlobalData's constructor value, assumed 1.0",
		"[S-256] the experience a resource building earns from its income (ExperienceTracker, RW 0x79D833) is dropped unless Economy::setExperienceHook is installed (counted); the "
		"original owner of a TEMPORARILY_DEFECTED object (RW + 0x320, the DefectionHelper's) is not tracked, so its command points leave the CURRENT owner",
		"[S-257] the death pipeline is not here: Object::friend_onDie is the seam (command points leave, every die module runs; TerrainResourceBehavior releases its ground there) and "
		"GameLogic::destroyObject does not call it, exactly like retail, so a resource building removed without dying keeps its claim; the kill credit (Object::scoreTheKill RW 0x6955BC) "
		"that awards a bounty (Player::awardBounty RW 0x6AC06F) belongs to the combat lane, Economy::awardBounty is the function it calls",
		"[S-258] the CreateObject nugget's IgnoreCommandPointLimit (RW 0xBF75A8 + 0x91) is parsed by the OCL lane's table, not by this lane; a unit made by an OCL counts against the limit "
		"like any other object",
		"[S-259] the live playable player count (the MPn command point tier and the MultiPlayMoneyMult index, RW 0x6A8630) counts every player of the map whose template has "
		"PlayableSide and that is not an observer or defeated: the map's placeholder skirmish sides (SkirmishElves .. SkirmishAngmar) count unless a map script defeated or "
		"re-templated them first; map scripts do not run in this build and the lobby's slot count is not consulted, so the tier follows the map's own player list (RotWK 2.01 "
		"retail behaviour on that point was not verified in a running game)",
		"[S-260] SalvageCrateCollide (RW create 0x651052, data 0x655980) has typed data but no runtime: the collide interface and the crate pickup belong to the collision / crate lane; "
		"a salvage crate does not pay money or upgrade a weapon in this build",
		"[S-261] Economy::awardBounty (RW 0x6AC06F) scales a HERO victim's BountyValue by its experience level through Economy::setHeroBountyScale (RW 0x68DDF1: the level tables behind "
		"Object + 0x26c are not ported); without the hook the unscaled value is paid and counted in the run report. The kill credit that calls it (Object::scoreTheKill, RW 0x6955BC, "
		"and the 0x695743 call site) is the combat lane's" };
}
} // namespace EconomyStops
