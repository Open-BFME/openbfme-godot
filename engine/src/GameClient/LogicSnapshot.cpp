// OpenBFME. GPL-3.0.
//
// LogicSnapshot (lane SMOOTH-1): see LogicSnapshot.h. Reads the completed logic state, writes only the new snapshot.

#include "GameClient/LogicSnapshot.h"

#include "GameClient/EndGame.h"

#include "GameLogic/Module/ProjectileModules.h"

#include "Common/BuildAssistant.h"
#include "Common/ModelState.h"
#include "Common/Player.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/HudObjects.h"
#include "GameClient/Radar.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/InvisibilityManager.h"

#include "Common/PlayerList.h"

#include <algorithm>
#include <cmath>
#include <cstring>

CellShroudStatus ShroudView::cellStatus(int cx, int cy) const
{
	if (cx < 0 || cy < 0 || cx >= countX || cy >= countY || localPlayer < 0)
	{
		return CELLSHROUD_SHROUDED;
	}
	return (CellShroudStatus)status[(size_t)cy * (size_t)countX + (size_t)cx];
}

CellShroudStatus ShroudView::statusAt(float x, float y) const
{
	if (!(cellSize > 0.0f))
	{
		return CELLSHROUD_SHROUDED;
	}
	// ShroudManager::worldToCell: floor((p - lo) * inverse) (client values: the drawn shroud only)
	const float inv = 1.0f / cellSize;
	return cellStatus((int)std::floor((x - originX) * inv), (int)std::floor((y - originY) * inv));
}

const ObjectSnapshot *LogicSnapshot::find(ObjectID id) const
{
	auto it = std::lower_bound(objects.begin(), objects.end(), id, [](const ObjectSnapshot &o, ObjectID v) { return o.id < v; });
	return (it != objects.end() && it->id == id) ? &*it : nullptr;
}

namespace
{
// RW 0x68B34C: the current locomotor's speed per logic frame (AI +0x260, its current locomotor +0x1F0, +0x40), 0 (RW 0xC1B594) when there is none or it is not
// positive. Client read of the completed state (the snapshot is built between frames)
float locomotorSpeedOf(const Object &o)
{
	const AIUpdateInterface *ai = o.getAIUpdateInterface();
	const Locomotor *l = ai ? ai->curLocomotor() : nullptr;
	if (!l || !(l->speed() > 0.0f))
	{
		return 0.0f;
	}
	return l->speed();
}

// RW 0x4B67D4 .. 0x4B683A: the object's speed, else (exactly 0) its horde's (RW 0x693A1A(0): itself when HORDE, else its container when that is a HORDE)
float drawSpeedOf(const Object &o)
{
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	const float own = locomotorSpeedOf(o);
	if (own != 0.0f || kHorde < 0)
	{
		return own;
	}
	const Object *horde = o.isKindOf((unsigned)kHorde) ? &o : nullptr;
	if (!horde)
	{
		const Object *c = o.getContainedBy();
		horde = (c && c->isKindOf((unsigned)kHorde)) ? c : nullptr;
	}
	return horde ? locomotorSpeedOf(*horde) : 0.0f;
}

bool sameTransform(const ObjectSnapshot &a, const ObjectSnapshot &b)
{
	return std::memcmp(a.basis, b.basis, sizeof(a.basis)) == 0 && a.position.x == b.position.x && a.position.y == b.position.y &&
		a.position.z == b.position.z;
}
} // namespace

std::shared_ptr<const LogicSnapshot> LogicSnapshot::build(GameLogic &logic, const LogicSnapshot *previous, bool hashed, std::uint32_t stateHash)
{
	static const int kActivelyBeingConstructed = ModelCondition::indexOf("ACTIVELY_BEING_CONSTRUCTED");
	auto snap = std::make_shared<LogicSnapshot>();
	snap->frame = logic.getFrame();
	snap->nextObjectId = logic.getNextObjectID();
	snap->hashed = hashed;
	snap->stateHash = stateHash;
	snap->objects.reserve(previous ? previous->objects.size() + 16 : 256);
	// the local player's shroud (merge with VIS-1): a new copy when its version moved
	ShroudManager *sm = logic.shroud();
	const Player *local = logic.players().getLocalPlayer();
	int shroudLocal = -1;
	if (sm)
	{
		const int li = local ? local->getPlayerIndex() : -1;
		const unsigned long long version = sm->stats().edges * 2 + (sm->displayed() ? 1 : 0);
		const ShroudView *pv = previous ? previous->shroud.get() : nullptr;
		if (pv && pv->version == version && pv->localPlayer == li && pv->countX == sm->cellCountX() && pv->countY == sm->cellCountY())
		{
			snap->shroud = previous->shroud;
		}
		else
		{
			auto v = std::make_shared<ShroudView>();
			v->displayed = sm->displayed();
			v->localPlayer = li;
			v->countX = sm->cellCountX();
			v->countY = sm->cellCountY();
			v->cellSize = sm->cellSize();
			v->originX = sm->originX();
			v->originY = sm->originY();
			v->version = version;
			for (int k = 0; k < 3; ++k)
			{
				v->levels[k] = sm->displayLevel((CellShroudStatus)k);
			}
			v->status.assign((size_t)std::max(0, v->countX) * (size_t)std::max(0, v->countY), (std::uint8_t)CELLSHROUD_SHROUDED);
			for (int y = 0; li >= 0 && y < v->countY; ++y)
			{
				for (int x = 0; x < v->countX; ++x)
				{
					v->status[(size_t)y * (size_t)v->countX + (size_t)x] = (std::uint8_t)sm->getCellStatus(li, x, y);
				}
			}
			snap->shroud = v;
		}
		if (sm->displayed() && li >= 0)
		{
			shroudLocal = li;
		}
	}
	{
		auto pv = std::make_shared<PlayerView>();
		PlayerList &players = logic.players();
		pv->localIndex = local ? local->getPlayerIndex() : -1;
		pv->count = players.getPlayerCount();
		pv->relationship.assign((size_t)pv->count * (size_t)pv->count, (std::int8_t)NEUTRAL);
		for (int a = 0; a < pv->count; ++a)
		{
			const Player *pa = players.getNthPlayer(a);
			for (int b = 0; pa && b < pv->count; ++b)
			{
				if (const Player *pb = players.getNthPlayer(b))
				{
					pv->relationship[(size_t)a * (size_t)pv->count + (size_t)b] = (std::int8_t)pa->getRelationship(pb);
				}
			}
		}
		snap->players = pv;
	}
	// lane END-1: the victory state and the local player's answers (GameClient/EndGame.h); the previous view is shared when nothing changed
	snap->endGame = EndGameView::build(logic, previous ? previous->endGame : std::shared_ptr<const EndGameView>());
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		snap->objects.emplace_back();
		ObjectSnapshot &s = snap->objects.back();
		s.id = o->getID();
		s.tmpl = o->getTemplate();
		std::memcpy(s.basis, o->getBasis(), sizeof(s.basis));
		s.angle = o->getOrientation();
		s.position = *o->getPosition();
		s.hasRecorded = o->hasRecordedTransform();
		if (s.hasRecorded)
		{
			// RW 0x6725C9 / 0x68EF2F / 0x6725DC: the recorded transform, its translation, the previous translation (+0x1A5 is set with every record)
			std::memcpy(s.recordedBasis, o->getRecordedBasis(), sizeof(s.recordedBasis));
			s.recordedAngle = o->getRecordedOrientation();
			s.recordedPos = o->getRecordedPosition();
			s.previousPos = o->getPreviousPosition();
		}
		else
		{
			std::memcpy(s.recordedBasis, s.basis, sizeof(s.basis));
			s.recordedAngle = s.angle;
			s.recordedPos = s.position;
			s.previousPos = s.position;
		}
		// RW 0x5E3BD1: the pending position when it was set during this frame (retail clears it with every recordTransform), else the position
		s.nextPos = s.position;
		if (const AIUpdateInterface *ai = o->getAIUpdateInterface())
		{
			s.hasNext = ai->pendingPositionOfFrame(snap->frame, s.nextPos);
		}
		else
		{
			// lane PROJ-2: a projectile's look-ahead point (RW 0x85F6E7 .. 0x85F71E) and its launch (an object without AI; ZH getProjectileUpdateInterface)
			for (const std::unique_ptr<BehaviorModule> &m : o->modules())
			{
				if (const ProjectileUpdateInterface *pi = m->getProjectileUpdateInterface())
				{
					s.hasNext = pi->projectilePendingPositionOfFrame(snap->frame, s.nextPos);
					const ProjectileUpdateInterface::ClientInfo ci = pi->projectileClientInfo();
					s.projectile = ci.fireFrame != 0;
					s.projectileFireFrame = ci.fireFrame;
					s.projectileLauncher = ci.launcher;
					s.projectileEnd = ci.end;
					s.projectileSegments = ci.segments;
					break;
				}
			}
		}
		s.instanceScale = o->getInstanceScale();
		s.moveSpeed = drawSpeedOf(*o); // lane SMOOTH-3
		s.scriptTarget = DrawableScriptTarget::capture(logic, *o); // lane FX-3
		// the radar's inputs (Radar::blips: retail's live test of the same conditions)
		if (const Player *owner = o->getControllingPlayer())
		{
			s.ownerIndex = owner->getPlayerIndex();
			s.ownerColor = owner->getPlayerColor();
			s.radarBlip = !o->isDestroyed() && !o->getContainedBy() && owner->hasTeamColor() && HudObjects::isSelectable(*o);
		}
		static const int kStructure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
		s.structure = kStructure >= 0 && o->isKindOf((unsigned)kStructure);
		Radar::captureObject(logic, *o, local, s.radar); // lane RADAR-1
		if (shroudLocal >= 0)
		{
			const ObjectShroudStatus os = sm->clientObjectStatus(*o, shroudLocal);
			s.shroudedForLocal = os == OBJECTSHROUD_SHROUDED;
			s.objectShroud = (int)os;
		}
		s.drawableHidden = o->isDrawableHidden(); // lane GARRISON-1
		s.containedBy = o->getContainedBy() ? o->getContainedBy()->getID() : (ObjectID)INVALID_ID; // lane COMBAT-4
		s.stealthLook = InvisibilityManager::clientLook(*o, local); // lane STEALTH-1
		if (s.stealthLook == 1 || s.stealthLook == 4)
		{
			logic.invisibility().clientOpacityRange(*o, &s.stealthOpacityMin, &s.stealthOpacityMax, &s.stealthCycleFrames);
		}
		s.constructionPercent = o->getConstructionPercent();
		const bool building = s.constructionPercent >= 0.0f || (kActivelyBeingConstructed >= 0 && o->testModelCondition(kActivelyBeingConstructed));
		if (building)
		{
			if (const Player *owner = o->getControllingPlayer())
			{
				s.buildFrames = BuildAssistant::calcTimeToBuild(*o->getTemplate(), owner, nullptr, -1, logic.productionSettings(), logic); // RW 0x68BD71
			}
		}
	}
	std::stable_sort(snap->objects.begin(), snap->objects.end(), [](const ObjectSnapshot &a, const ObjectSnapshot &b) { return a.id < b.id; });
	// RW 0x69355D: the frame of the last transform change (a new object counts as changed now)
	for (ObjectSnapshot &s : snap->objects)
	{
		const ObjectSnapshot *before = previous ? previous->find(s.id) : nullptr;
		s.lastMovedFrame = (before && sameTransform(*before, s)) ? before->lastMovedFrame : snap->frame;
	}
	return snap;
}
