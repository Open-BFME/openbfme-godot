// OpenBFME. GPL-3.0.
//
// PlayerEconomyHost (lane ECON-1): the questions a Player cannot answer alone because they need the world (its objects, GameData, the game's mode). The
// economy system (GameLogic/Economy.h) implements it and attaches itself to the PlayerList, which hands it to every Player it makes; a Player asked without a
// host throws (no silent default: PLAN rule 10).

#pragma once

#include <cstdint>

class Player;
class ThingTemplate;
struct ObjectFilter;
class UpgradeTemplate;

class PlayerEconomyHost
{
public:
	virtual ~PlayerEconomyHost() = default;
	// RW 0x6ABD0B Player::hasObjectMatching(filter, completedOnly): the player owns a live object (not destroyed, not a sold / ghost object) whose template the
	// filter allows; `completedOnly` additionally skips structures that are still under construction (RW 0x7A075A)
	virtual bool hasObjectMatching(const Player &player, const ObjectFilter &filter, bool completedOnly) const = 0;
	// RW 0x6A7B9F: the player's command point limit (records included)
	virtual int commandPointLimit(const Player &player) const = 0;
	// RW 0x6AD8A7: 1.0 + the CostModifierUpgrade percentage that applies to the template (`slaughter` = RW's second argument)
	virtual float productionCostChange(const Player &player, const ThingTemplate &tt, bool slaughter) const = 0;
	// RW 0x6AA858: the money handicap of the game kind (identity outside campaign handicaps)
	virtual int applyHandicapMoney(const Player &player, int amount) const = 0;
	// RW 0x6A7F79 with the template's CommandPoints / ARMY_OF_DEAD read from the template
	virtual bool canAffordCommandPoints(const Player &player, const ThingTemplate &tt) const = 0;
	// lane UPGRADE-1. RW 0x6AE483 onUpgradeCompleted: Object::updateUpgradeModules (RW 0x6936FE) on every object of the player's teams
	virtual void updateUpgradeModulesOfPlayer(const Player &player) = 0;
	// RW 0x6AE546 onUpgradeRemoved: Object::removeUpgrade(upgrade) (RW 0x691438) on every object of the player's teams
	virtual void removeUpgradeFromObjectsOfPlayer(const Player &player, const UpgradeTemplate &upgrade) = 0;
};
