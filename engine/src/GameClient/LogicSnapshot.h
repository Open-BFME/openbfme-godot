// OpenBFME. GPL-3.0.
//
// LogicSnapshot (lane SMOOTH-1, stop S-810): the immutable picture of one completed logic frame that the render side draws from.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   RW 0x62E908 .. 0x62E933 (GameLogic::update, phase 1, the "drawableCallback" row): for every object, Drawable RW 0x674B1F(0) gathers the inputs
//     of the drawn pose BEFORE the frame's recordTransform (phase 2) and movement (phases 3-6), i.e. the state at the end of the previous frame:
//       +0x3AC  the recorded transform (RW 0x6725C9: Object +0x158 when +0x1A4, else the current transform +0x08)
//       +0x3DC  the current transform (Object +0x08 .. +0x37)
//       +0x40C  P0, the previous translation (RW 0x6725DC: Object +0x18C when +0x1A5, else the current position +0x38)
//       +0x418  P1, the recorded transform's translation (RW 0x68EF2F)
//       +0x424  P2, the current position (+0x38)
//       +0x430  P3, the movers' pending position (RW 0x5E3BD1: +0x198 when +0x1A6, else +0x38); RW 0x6260E1 clears +0x1A6 in every recordTransform
//     The drawn transform (RW 0x6765B9) is built from these until the next gather, so the drawable never sees a frame being computed.
//   RW 0x69355D (Object's transform change): Drawable +0x3A4 = the logic frame of the last transform change, +0x3A8 = 0.
// THIS PORT: the snapshot is built when a frame is COMPLETE (after phase 6 and the state hash), which is the same state RW 0x674B1F reads at the
// next phase 1. It holds values only (no pointer into mutable objects or modules; the template pointers are immutable for the loaded game),
// is published once and never changed, and lives while any reader holds it (shared_ptr).

#pragma once

#include "Common/GameCommon.h"
#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameClient/DrawableScriptTarget.h"

#include <cstdint>
#include <memory>
#include <vector>

class GameLogic;
class ThingTemplate;
struct EndGameView;

struct ObjectSnapshot
{
	ObjectID id = INVALID_ID;
	const ThingTemplate *tmpl = nullptr;
	bool hasRecorded = false;      ///< Object +0x1A4: a transform was recorded
	float recordedBasis[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	float recordedAngle = 0.0f;
	Coord3D recordedPos;           ///< P1
	float basis[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	float angle = 0.0f;
	Coord3D position;              ///< P2
	Coord3D previousPos;           ///< P0
	Coord3D nextPos;               ///< P3
	bool hasNext = false;          ///< the pending position was set during this frame (retail +0x1A6)
	UnsignedInt lastMovedFrame = 0; ///< retail Drawable +0x3A4: the last frame whose snapshot showed a different transform than the one before
	float instanceScale = 1.0f;
	// lane SMOOTH-3: the speed the model draw syncs its Distance animations to (RW 0x4B67D4 at every draw update, RW 0x4BFAEA): RW 0x68B34C, the current
	// locomotor's speed (AI +0x260 -> +0x1F0 -> +0x40, world units per logic frame; 0 without AI or locomotor, or when not positive), else when that is 0
	// the same of the object's horde (RW 0x693A1A(0): the object itself when HORDE, else its container when that is a HORDE)
	float moveSpeed = 0.0f;
	// lane FX-3: the object's target record for the draw scripts (CurDrawableIsCurrentTargetKindof RW 0x73667D, CurDrawableGetCurrentTargetBearing RW 0x734A75)
	DrawableScriptTarget scriptTarget;
	// the construction look (Drawable::updateConstruction, RW 0x4B51B5 / 0x4B686D)
	float constructionPercent = -1.0f;
	int buildFrames = 0;           ///< RW 0x68BD71's calcTimeToBuild (0 without a controlling player or when not being built)
	// the radar (Radar::blips): an object of a coloured owner that is selectable, not contained and not destroyed is a blip
	bool radarBlip = false;
	std::uint32_t ownerColor = 0;  ///< 0xRRGGBB of the controlling player
	int ownerIndex = -1;           ///< the controlling player's index (-1: none)
	bool structure = false;        ///< KINDOF STRUCTURE (a larger blip)
	// merge with VIS-1 / HUD-2: the object is SHROUDED for the local player (ShroudManager::getObjectStatus, RW 0xB4E890) while the shroud is displayed:
	// the radar leaves it out and the device layer does not draw it
	bool shroudedForLocal = false;
	int objectShroud = 0;          ///< lane PROJ-2: the ObjectShroudStatus for the local player while the shroud is displayed (0 OBJECTSHROUD_INVALID otherwise)
	// lane STEALTH-1: the invisibility look for the local player (InvisibilityManager::clientLook, RW 0x81AA03): 0 normal, 1 / 4 the friend's opacity pulse, 3 detected
	// for an enemy, 5 invisible for an enemy (not drawn, not picked)
	int stealthLook = 0;
	// lane GARRISON-1: Object::isDrawableHidden (RW 0x6718FB, set by the contains): the device layer draws nothing for it
	bool drawableHidden = false;
	float stealthOpacityMin = 1.0f, stealthOpacityMax = 1.0f; ///< the friend's pulse range (InvisibilityManager::clientOpacityRange), set for looks 1 / 4
	unsigned stealthCycleFrames = 0;
	// lane PROJ-2: a launched projectile (ProjectileUpdateInterface::projectileClientInfo): the frame of its last launch or bounce, its launcher, the end
	// of its flight and the segment count; the drawable's fade (GameClient/DrawableFade, RW 0x85EE00 .. 0x85EF31) starts from them
	bool projectile = false;
	UnsignedInt projectileFireFrame = 0;
	ObjectID projectileLauncher = INVALID_ID;
	Coord3D projectileEnd;
	int projectileSegments = 0;
};

// The local player's shroud as the client reads it (VIS-1's ShroudManager; merge with SMOOTH-1): the statuses of the local player's cells, the GameData
// display levels and the manager's edge counter (the version: a new copy is made only when it moved, otherwise the previous snapshot's is shared).
struct ShroudView
{
	bool displayed = false;
	int localPlayer = -1;
	int countX = 0, countY = 0;
	float cellSize = 0.0f, originX = 0.0f, originY = 0.0f;
	unsigned long long version = 0;      ///< the radar's version: edges * 2 + displayed (Radar::shroudVersion)
	unsigned levels[3] = { 255, 255, 255 }; ///< ShroudManager::displayLevel by CellShroudStatus
	std::vector<std::uint8_t> status;    ///< CellShroudStatus per cell, row y * countX + x
	CellShroudStatus cellStatus(int cx, int cy) const; ///< ShroudManager::getCellStatus for the local player (out of the grid: SHROUDED)
	CellShroudStatus statusAt(float x, float y) const;  ///< ShroudManager::getStatusAt (RW 0xB4FB20) for the local player
	unsigned displayLevel(CellShroudStatus s) const { return levels[(int)s]; }
};

// The players as the client's per-frame readers ask them (the audio manager's shouldPlayLocally queries)
struct PlayerView
{
	int localIndex = -1;
	int count = 0;
	std::vector<std::int8_t> relationship; ///< Player::getRelationship(owner -> other), [owner * count + other]
	int relationshipOf(int owner, int other) const
	{
		return owner >= 0 && other >= 0 && owner < count && other < count ? relationship[(size_t)owner * (size_t)count + (size_t)other] : (int)NEUTRAL;
	}
};

struct LogicSnapshot
{
	UnsignedInt frame = 0;          ///< the completed logic frame
	ObjectID nextObjectId = 1;      ///< the logic's next object id at that frame: an id below it that the snapshot lacks is gone, one at or above it is not published yet
	bool hashed = false;
	std::uint32_t stateHash = 0;    ///< the state hash of the frame (when hashed)
	std::vector<ObjectSnapshot> objects; ///< ascending id
	std::shared_ptr<const ShroudView> shroud;   ///< null without a shroud manager
	std::shared_ptr<const PlayerView> players;
	std::shared_ptr<const EndGameView> endGame; ///< lane END-1: the victory state the end sequence reads (GameClient/EndGame.h)

	const ObjectSnapshot *find(ObjectID id) const;

	// Builds the snapshot of the logic's current (completed) state. `previous` gives lastMovedFrame its history (null: the first snapshot).
	// Must run on the thread that owns the simulation, between frames.
	static std::shared_ptr<const LogicSnapshot> build(GameLogic &logic, const LogicSnapshot *previous, bool hashed, std::uint32_t stateHash);
};
