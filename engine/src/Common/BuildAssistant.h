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
//     producer's ProductionModifier cost multiplier (ProductionUpdate slot 26); the result is truncated to int. Lane FEAT-BRUTAL-1 (RW 0x73C344 ..
//     0x73C390): when Player::getDifficulty (RW 0x6AA61B: the player's AI's + 0x30, else TheScriptEngine + 0x1A5C4) is 3 (BRUTAL) and RW 0x6A950B
//     (TheSkirmishAIManager RW 0xDE4938) finds the player's AI, the stored float32 product is scaled by 1 - min(BuildCostReduction, 1.0f)
//     (TheSkirmishAIData + 0x94C = manager + 0x95C; comiss 1.0 at RW 0x73C370, SSE subss / mulss at RW 0x73C383 / 0x73C387) and the one cvttss2si
//     truncates the discounted value (RW 0x73C38C). Every other player truncates the product as it stands.
//   calcTimeToBuild(player, producer, override) RW 0x73C39E: SECONDS are truncated to whole numbers at several steps: t = (int)BuildTime; t = ftol(handicap *
//     t); t = ftol((1.0 + ProductionTimeChange) * t); t = ftol(P * t) (slot 27); sec = (int)((float)t / lowEnergyRate); the multi-factory and multiplayer
//     speed factors (GameData MultipleFactory, MultiPlayUnitSpeedMult / MultiPlayBuildingSpeedMult MPn) scale sec; frames = 5 * sec. Lane FEAT-BRUTAL-1
//     (RW 0x73C567 .. 0x73C5CB): under the same Brutal condition x87 computes frames * (1 - min(BuildTimeReduction, 1.0f)) (TheSkirmishAIData + 0x950 =
//     manager + 0x960; fsub RW 0x73C5B5, fmulp RW 0x73C5B8) and MSVCR71 floor truncates it (RW 0x73C5BD calls [0xBD0580] = floor, NOT [0xBD0588] ceil):
//     floor(75 * 0.85) = 63 frames for a 15 s Farm, where the raw seconds would give 60. The normal branch floors too (identity on the integer 5 * sec).
//   Key bytes (PE image base 0x400000, .text; cost): 83 F8 03 (cmp eax, 3) 0x73C349; 8B 0D 38 49 DE 00 (mov ecx, [0xDE4938]) 0x73C34E; E8 B1 D1 F6 FF
//     (call 0x6A950B) 0x73C355; F3 0F 10 05 08 19 BD 00 (movss xmm0, 1.0f) 0x73C363; 05 5C 09 00 00 (add eax, 0x95C) 0x73C36B; 0F 2F 00 (comiss)
//     0x73C370; F3 0F 5C C1 (subss) 0x73C383; F3 0F 59 45 10 (mulss) 0x73C387; F3 0F 2C C0 (cvttss2si) 0x73C38C. Time: 83 F8 03 0x73C56E;
//     E8 8C CF F6 FF (call 0x6A950B) 0x73C57A; 05 60 09 00 00 (add eax, 0x960) 0x73C590; DB 45 08 (fild frames) 0x73C5AE; D9 E8 (fld1) 0x73C5B2;
//     D8 65 10 (fsub reduction) 0x73C5B5; DE C9 (fmulp) 0x73C5B8; DD 1C 24 (fstp qword) 0x73C5BA; FF 15 80 05 BD 00 (call [0xBD0580] = floor)
//     0x73C5BD; D9 5D 10 (fstp dword) 0x73C5C4; DB 5D 08 (fistp) 0x73C5CB.
//   A vanilla unit with BuildTime 20.0 therefore takes 100 frames; BuildTime 7.5 takes 35, not 37.5.
// Not ported (stop S-204): the handicap table (lobby handicap, 1.0), the energy ratio (no energy system: 1.0), the MultipleFactory count for BuildCompletion APPEARS_AT_RALLY_POINT templates
// (reported when GameData's MultipleFactory is not 1.0). The hero record's own second factor (PlayerHeroList RW 0x780AD3 / 0x780B72) and the upgrade cost/time
// scaling (UpgradeTemplate RW 0x66F2C8 / 0x66F1A8) are S-855 / S-204's.

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
int buildable(const ThingTemplate &tt);             ///< the INI's Buildable: 0 Yes, 1 Ignore_Prerequisites, 2 No, 3 Only_By_AI (RW +0x602)
// ThingTemplate::getBuildable RW 0x73BFE8: the game's script override (GameLogic::findBuildableStatusOverride, RW 0x62EE6D) when there is one, else
// buildable(tt) (lane BFME2CAMP1)
int buildable(const ThingTemplate &tt, const GameLogic &logic);
int buildCompletion(const ThingTemplate &tt);       ///< BuildCompletion: 0 INVALID, 1 APPEARS_AT_RALLY_POINT, 2 PLACED_BY_PLAYER
int maxSimultaneousOfType(const ThingTemplate &tt); ///< MaxSimultaneousOfType (u16)

// RW 0x6AC856 Player::allowedToBuild: units and structures are gated by the player's two flags, then the scripts' forbidden template ids
bool playerAllowedToBuild(const Player &player, const ThingTemplate *tt, GameLogic &logic);
// RW 0x6AC927 Player::canBuild: allowedToBuild, then Buildable (No refuses, Ignore_Prerequisites accepts, Only_By_AI needs a computer player), then every typed Prerequisites record
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

// RW 0x794F38 (BuildAssistant vtable slot 0x68): a non-null `what` matching a SpawnUnitBehavior's UnitName returns true first, even for a revival index.
// Otherwise `what` must be offered by a button of the builder's CommandSet (UNIT_BUILD, DOZER_CONSTRUCT or
// FOUNDATION_CONSTRUCT whose Object is equivalent), the button's NeededUpgrade list satisfied (all, or any with NeededUpgradeAny: object upgrades by the
// builder, player upgrades by its owner) and the owner able to build it. The hero / REVIVE path checks the indexed button and hero record availability.
bool isInProducersCommandSet(Object &builder, const ThingTemplate *what, int buildIndex);

// lane QA2-FIX: RW 0x793E33 (BuildAssistant vtable slot 0x60, the line build test the placement code asks, RW 0x83E911 / 0x6A2B6D): `what` and the builder's
// template both carry KindOf WALL_HUB (template + 0x118 & 0x10000000, i.e. + 0x11B bit 4); a null `what` or builder is no line build
bool isLineBuildTemplate(GameLogic &logic, const ThingTemplate *what, const Object *builder);

// ZH BuildAssistant::canMakeUnit, RW vtable + 0x64 (RW 0x793ECB). `ignoreCommandPoints` (lane MODS-3, mod engine feature edain-unchained.queue-ignore-cp:
// Edain Unchained's hook at RW 0x79402B continues at RW 0x794033 when its global is set) skips only the command point refusal (CANMAKE_UNKNOWN_7 at RW
// 0x794026); retail never sets it. Only ProductionUpdate::queueCreateUnit's doCommandButton path passes true (Object::doCommandButton, QueueIgnoreCP)
CanMakeType canMakeUnit(Object &builder, const ThingTemplate *what, int buildIndex, bool ignoreCommandPoints = false);
} // namespace BuildAssistant

// the name the dispatcher and the tests use for ZH's `ThingTemplate::isEquivalentTo`
namespace ThingTemplateEquivalence
{
inline bool isEquivalentTo(const ThingTemplate *a, const ThingTemplate *b) { return BuildAssistant::isEquivalentTo(a, b); }
} // namespace ThingTemplateEquivalence
