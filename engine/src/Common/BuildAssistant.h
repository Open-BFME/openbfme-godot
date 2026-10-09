// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// BuildAssistant (ZH Include/Common/BuildAssistant.h, Source/Common/System/BuildAssistant.cpp canMakeUnit) and the ThingTemplate build arithmetic
// (ZH ThingTemplate::calcCostToBuild / calcTimeToBuild, RotWK RW 0x73C25F / 0x73C39E), lane PROD-1. Free functions over the template, the player and
// the producing object; the numbers come from the template's BuildCost / BuildTime fields, never from code.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; see workspace/rebuild/specs/production.md and the command-path research):
//   calcCostToBuild(player, producer, override) RW 0x73C25F: cost = override == -1 ? BuildCost (u16) : override; no player -> cost; then
//     r = (float)(((double)cost * handicap) * X * P) at x87 PC24 with X = (1.0 + ProductionCostChange) * the CostModifierUpgrade factor (RW 0x6AD8A7,
//     Player::getProductionCostChangeBasedOnKindOf: wired by UPGRADE-1) and P the
//     producer's ProductionModifier cost multiplier (ProductionUpdate slot 26); the result is truncated to int (Brutal AI: times 1 - BuildCostReduction).
//   calcTimeToBuild(player, producer, override) RW 0x73C39E: SECONDS are truncated to whole numbers at several steps: t = (int)BuildTime; t = ftol(handicap *
//     t); t = ftol((1.0 + ProductionTimeChange) * t); t = ftol(P * t) (slot 27); sec = (int)((float)t / lowEnergyRate); the multi-factory and multiplayer
//     speed factors (GameData MultipleFactory, MultiPlayUnitSpeedMult / MultiPlayBuildingSpeedMult MPn) scale sec; frames = 5 * sec.
//   A vanilla unit with BuildTime 20.0 therefore takes 100 frames; BuildTime 7.5 takes 35, not 37.5.
// Not ported (stop S-204): the handicap table (lobby handicap, 1.0), the energy ratio (no energy system: 1.0), the Brutal AI reductions, the MultipleFactory count for BuildCompletion APPEARS_AT_RALLY_POINT templates
// (reported when GameData's MultipleFactory is not 1.0), the Create-A-Hero surcharge.

#pragma once

#include "GameLogic/ProductionSettings.h"

class Object;
class Player;
class ThingTemplate;
class GameLogic;

// ZH BuildAssistant.h CanMakeType (RotWK's UI knows 2 = not enough money, 4 queue full, 5 parking places, 6 maxed out, 7 = abort silently)
enum CanMakeType
{
	CANMAKE_OK = 0,
	CANMAKE_NO_PREREQUISITES = 1,
	CANMAKE_NO_MONEY = 2,
	CANMAKE_FACTORY_IS_DISABLED = 3,
	CANMAKE_QUEUE_FULL = 4,
	CANMAKE_PARKING_PLACES_FULL = 5,
	CANMAKE_MAXED_OUT_FOR_PLAYER = 6,
	CANMAKE_UNKNOWN_7 = 7
};

namespace BuildAssistant
{
// the template fields the production code reads, from the final override; an unset field reads 0 (S-072: field defaults of a fresh template are not
// ported)
unsigned buildCost(const ThingTemplate &tt);        ///< BuildCost (u16)
float buildTime(const ThingTemplate &tt);           ///< BuildTime (seconds)
int commandPoints(const ThingTemplate &tt);         ///< CommandPoints
int buildable(const ThingTemplate &tt);             ///< Buildable: 0 Yes, 1 Ignore_Prerequisites, 2 No (RW +0x602)
int buildCompletion(const ThingTemplate &tt);       ///< BuildCompletion: 0 INVALID, 1 APPEARS_AT_RALLY_POINT, 2 PLACED_BY_PLAYER
int maxSimultaneousOfType(const ThingTemplate &tt); ///< MaxSimultaneousOfType (u16)

// RW 0x6AC856 Player::allowedToBuild: units and structures are gated by the player's two flags, then the scripts' forbidden template ids
bool playerAllowedToBuild(const Player &player, const ThingTemplate *tt, GameLogic &logic);
// RW 0x6AC927 Player::canBuild: allowedToBuild, then Buildable (No refuses, Ignore_Prerequisites accepts, Only_By_AI needs a computer player), then the
// template's Prerequisites (none in retail data; a mod's block is reported, stop S-207)
bool playerCanBuild(const Player &player, const ThingTemplate *tt, GameLogic &logic);
// RW 0x6A7F79: CommandPoints == 0, a KindOf bit 143 template, or usage + CommandPoints <= limit
bool commandPointsAvailable(const Player &player, const ThingTemplate &tt, GameLogic &logic);

// RW 0x73C25F. `producer` may be null.
int calcCostToBuild(const ThingTemplate &tt, const Player *player, const Object *producer, int costOverride);
// RW 0x73C39E: logic frames. The GameData numbers come from `settings` (loaded: PLAN rule 10).
int calcTimeToBuild(const ThingTemplate &tt, const Player *player, const Object *producer, int secondsOverride, const ProductionSettings &settings, GameLogic &logic);

// ZH ThingTemplate::isEquivalentTo, RW 0x73D5C2: the same template, the same final override, or one names the other in its EquivalentTo (RW +0x33C) or
// BuildVariations (RW +0x330) list (no reskin chain: RotWK dropped it)
bool isEquivalentTo(const ThingTemplate *a, const ThingTemplate *b);

// RW 0x794F38 (BuildAssistant vtable slot 0x68): `what` must be offered by a button of the builder's CommandSet (UNIT_BUILD, DOZER_CONSTRUCT or
// FOUNDATION_CONSTRUCT whose Object is equivalent), the button's NeededUpgrade list satisfied (all, or any with NeededUpgradeAny: object upgrades by the
// builder, player upgrades by its owner) and the owner able to build it. The hero / REVIVE path (buildIndex != -1) is HERO-1's: false.
bool isInProducersCommandSet(Object &builder, const ThingTemplate *what, int buildIndex);

// lane QA2-FIX: RW 0x793E33 (BuildAssistant vtable slot 0x60, the line build test the placement code asks, RW 0x83E911 / 0x6A2B6D): `what` and the builder's
// template both carry KindOf WALL_HUB (template + 0x118 & 0x10000000, i.e. + 0x11B bit 4); a null `what` or builder is no line build
bool isLineBuildTemplate(GameLogic &logic, const ThingTemplate *what, const Object *builder);

// ZH BuildAssistant::canMakeUnit, RW vtable + 0x64 (RW 0x793ECB)
CanMakeType canMakeUnit(Object &builder, const ThingTemplate *what, int buildIndex);
} // namespace BuildAssistant

// the name the dispatcher and the tests use for ZH's `ThingTemplate::isEquivalentTo`
namespace ThingTemplateEquivalence
{
inline bool isEquivalentTo(const ThingTemplate *a, const ThingTemplate *b) { return BuildAssistant::isEquivalentTo(a, b); }
} // namespace ThingTemplateEquivalence
