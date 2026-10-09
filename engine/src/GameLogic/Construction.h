// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Construction (lane BUILD-1): the shared pieces of building a structure: the foundation object a plot or a dozer makes, the progress to completion, the player
// notifications. The module classes (FoundationAIUpdate, DozerAIUpdate, GettingBuiltBehavior) and the command dispatcher call these.
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; static disassembly):
//   FoundationAIUpdate::construct RW 0x858701 (vtable of the +0x20 interface RW 0xC30DE8 slot 7), reached from BuildAssistant slot 0x38 RW 0x797796 when the builder
//   is a BASE_FOUNDATION (a build plot) and from the dispatcher RW 0x77A91B (MSG_FOUNDATION_CONSTRUCT). In order:
//     * refused (null) when an argument is missing or the plot is destroyed (status bit 0), and, unless `instant`, when the plot's CastleMemberBehavior already records
//       an occupant (RW 0x79A09D);
//     * the object: newObject(tmpl, the owner's default team, empty status mask) (RW 0x6D165E), producer = the plot (RW 0x68B6A1);
//     * the angle: with KindOf FACE_AWAY_FROM_CASTLE_KEEP the direction from the castle keep to the plot (atan2) plus PlacementViewAngle; else with NEED_BASE_FOUNDATION
//       PlacementViewAngle plus the plot's own orientation;
//     * not instant, NEED_BASE_FOUNDATION and not BASE_FOUNDATION: construction percent 0.0 (RW 0x858924), status UNDER_CONSTRUCTION (RW 0x62684D), model conditions
//       clearAndSet(clear AWAITING_CONSTRUCTION, set PARTIALLY_CONSTRUCTED | ACTIVELY_BEING_CONSTRUCTED) (RW 0x68D607);
//     * the plot's BuildVariation 1 / 2 sets BUILD_VARIATION_ONE / TWO on the new object;
//     * setPosition, setOrientation, the pathfinder map (RW 0x6E85E9, unless WALK_ON_TOP_OF_WALL), Player::onStructureCreated (RW 0x6AAF3B);
//     * instant: the under-construction status clears and the percent is -1; otherwise the player pays calcCostToBuild(tmpl, owner, plot) (RW 0x73C25F, money RW 0x7B17EF,
//       the score RW 0x79DFC2) and the object keeps the price paid (RW + 0x33C); the plot's castle member fields are copied to the new object's (RW 0x858ADE);
//       an instant build then runs onBuildComplete (RW 0x68D252).
//   BuildAssistant slot 0x38 for any other builder with no AI path builds the object at once (RW 0x7978A1 ff): newObject with the status mask 4 (UNDER_CONSTRUCTION)
//   when the template is a STRUCTURE, producer, position, orientation, the pathfinder map, Player::onStructureCreated + onStructureConstructionComplete (RW 0x6AAF3B /
//   0x6AA72B), onBuildComplete.
// TARGET (lane BUILD-2, RW 0x857E77 / 0x88DE43; see GettingBuiltBehavior and DozerAIUpdate): a plot's rising foundation starts at 1.0 health (RW 0x85895D:
// internalChangeHealth(1.0 - health)). Its GettingBuiltBehavior runs from its constructor: without a WorkerName it builds itself (heals max health / calcTimeToBuild per
// frame through attemptHealingFromSoleBenefactor, RW 0x690584; the percent is health / max * 100), else it spawns the worker, whose dozer AI adds 100 / calcTimeToBuild
// to the percent and max health / that to the health per frame of work and completes the structure at 100; a Porter's foundation is built the same way by the
// Porter. The open parts are stops S-650 .. S-656.
//
// Simulation maths goes through SimMath.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class ThingTemplate;

namespace Construction
{
// RW 0x6AAF3B: the structure joined its player (a foundation, a map structure); nothing counts yet (command points count at completion, or at creation for a map
// object that is complete at birth: Object::friend_initObject)
void onStructureCreated(Player &player, Object *builder, Object &structure);
// RW 0x6AA72B -> 0x6AA7A8 -> 0x68E0C2: the structure is complete: its command points join the player's pool
void onStructureConstructionComplete(Player &player, Object *builder, Object &structure, bool fromRebuild);

// completes an under-construction structure: percent -1, status cleared, model conditions (clear AWAITING_CONSTRUCTION / PARTIALLY_CONSTRUCTED /
// ACTIVELY_BEING_CONSTRUCTED, set CONSTRUCTION_COMPLETE), the player notification, onBuildComplete
void completeConstruction(Object &structure, Object *builder);

// RW 0x858701: a plot makes a building (see the file comment). `instant` builds it complete at once. Null when refused (reasons go to GameLogic::reportError for the
// ones that are not the plain "plot busy / no money" refusals).
Object *constructOnPlot(Object &plot, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner, bool instant);

// ZH DozerAIUpdate::construct (the builder's vslot 0x1F8, RW 0x797796 for a DOZER builder): the structure is made UNDER_CONSTRUCTION with the builder as producer, the owner pays
// the price now, the progress runs while the builder works (GettingBuiltBehavior with needsBuilder). Null when refused.
Object *constructByDozer(Object &dozer, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner);
// BuildAssistant slot 0x38 for a builder that is neither a dozer nor a plot (the script engine, the AI): the structure appears complete at once (RW 0x7978A1 ff)
Object *buildObjectNow(GameLogic &logic, Object *builder, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner);

// the model condition flag index by the binary's name (-1: not in the table; a logic error to ask for such a name)
int modelConditionIndex(const char *name);
void setModelConditions(Object &obj, const std::vector<const char *> &clear, const std::vector<const char *> &set);

std::vector<std::string> stopLines();
} // namespace Construction
