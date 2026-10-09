// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIEmotionStates.h for the sources and what is inference.

#include "GameLogic/AI/AIEmotionStates.h"

#include "Common/GameCommon.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/EmotionSystem.h"

namespace
{
const float kAwayFromSource = 20.0f; // RW 0xBDBC6C
const float kFacedEnough = 0.035f;   // RW 0xC2A1BC

HordeContainInterface *ownHorde(Object &obj)
{
	ContainModuleInterface *c = obj.getContain();
	return c ? c->getHordeContainInterface() : nullptr;
}

// RW 0x403175 Coord3D::normalize: the length of RW 0x403111 (the PC24 root of the float32 sum of squares), scaled by its float32 inverse when it is not 0
void normalize3(float &x, float &y, float &z)
{
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(x, y, z)));
	if (len != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, len);
		x = SimMath::mulf32(x, inv);
		y = SimMath::mulf32(y, inv);
		z = SimMath::mulf32(z, inv);
	}
}

// RW 0x70C31E(relAngle + angle): fld relAngle; fadd dword angle; fstp dword (PC24)
void turnTo(Object &obj, const Coord3D &p)
{
	obj.setOrientation(SimMath::pc24Add(emotionRelAngle(obj, p), obj.getOrientation()));
}

int kindBit(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}
} // namespace

float emotionRelAngle(const Object &obj, const Coord3D &p)
{
	// RW 0x4B3D8D
	const Coord3D &o = *obj.getPosition();
	const float dx = SimMath::subf32(p.x, o.x), dy = SimMath::subf32(p.y, o.y);
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares2(dx, dy)));
	if (len == 0.0f)
	{
		return 0.0f;
	}
	const float inv = SimMath::divf32(1.0f, len);
	const float ux = SimMath::mulf32(inv, dx), uy = SimMath::mulf32(inv, dy);
	const float *b = obj.getBasis(); // RW 0x70B9E0: the facing (cos, sin)
	float c = SimMath::addf32(SimMath::mulf32(b[0], ux), SimMath::mulf32(b[3], uy));
	if (c < -1.0f)
	{
		c = -1.0f;
	}
	else if (1.0f < c)
	{
		c = 1.0f;
	}
	float a = (float)SimMath::acosDet((double)c);
	if (SimMath::subf32(SimMath::mulf32(b[0], uy), SimMath::mulf32(b[3], ux)) < 0.0f)
	{
		a = SimMath::subf32(0.0f, a);
	}
	return a;
}

bool emotionAttackedWithin(Object &obj, unsigned seconds, ObjectID &attacker)
{
	// RW 0x68C933
	const unsigned frames = (unsigned)LOGICFRAMES_PER_SECOND * seconds;
	if (HordeContainInterface *hc = ownHorde(obj))
	{
		return hc->attackedWithin(frames, attacker);
	}
	const ActiveBody *body = dynamic_cast<const ActiveBody *>(obj.getBodyModule());
	if (!body)
	{
		return false;
	}
	const UnsignedInt last = body->lastDamageFrame();
	if (last == 0xFFFFFFFFu || last + frames < obj.logic().getFrame())
	{
		return false;
	}
	attacker = body->lastDamager();
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// 20 / 21
// ---------------------------------------------------------------------------------------------------------------------------------
AIPanicState::AIPanicState(AIStateMachine &m, bool cower)
	: AIState(m, cower ? "AIPanicCowerState" : "AIPanicState")
	, m_cower(cower)
{
}

StateReturnType AIPanicState::onEnter()
{
	// RW 0x74E3E7 (20) / 0x75681A (21)
	AIUpdateInterface &a = ai();
	Object &obj = owner();
	Object *src = machine().goalObject();
	if (!src)
	{
		return STATE_FAILURE;
	}
	if (m_cower)
	{
		// the object's height above the source at most the source's geometry height (RW 0xAD1920)
		const ThingTemplate *tt = static_cast<const ThingTemplate *>(src->getTemplate())->getFinalOverride();
		const float above = SimMath::subf32(obj.getPosition()->z, src->getPosition()->z);
		if (!(above <= ObjectGeometry::maxHeightAbovePosition(ObjectGeometry::shapesOf(*tt))))
		{
			return STATE_FAILURE;
		}
	}
	a.chooseLocomotorSet(LOCOMOTORSET_PANIC); // AI vslot 0x238(4): false (no change) without a PANIC set
	a.setModelCondition(m_cower ? "EMOTION_AFRAID" : "PANICKING", true);
	m_budget = 1;
	m_goalFromPath = true;
	m_cowering = false;
	AIWorld &w = a.world();
	if (w.mapReady())
	{
		w.pathfinder().removeGoal(a.adapter()); // RW 0x68B401
	}
	if (m_cower)
	{
		// RW 0x75681A: 20 further along source -> object
		const Coord3D &p = *obj.getPosition(), &s = *src->getPosition();
		float dx = SimMath::subf32(p.x, s.x), dy = SimMath::subf32(p.y, s.y), dz = SimMath::subf32(p.z, s.z);
		normalize3(dx, dy, dz);
		Coord3D dest;
		dest.x = SimMath::addf32(SimMath::mulf32(dx, kAwayFromSource), p.x);
		dest.y = SimMath::addf32(SimMath::mulf32(dy, kAwayFromSource), p.y);
		dest.z = SimMath::addf32(SimMath::mulf32(dz, kAwayFromSource), p.z);
		a.mover().requestPath(dest, true); // RW 0x667ED1(dest, 1)
	}
	else
	{
		a.mover().requestSafePath((PathfindObjectID)src->getID()); // RW 0x663C6B
	}
	m_move = std::make_unique<AIMoveToState>(a.mover(), m_goal, false); // + 0x48 = 0
	m_move->setComputePathBudget(&m_budget);
	return m_move->onEnter();
}

StateReturnType AIPanicState::update()
{
	AIUpdateInterface &a = ai();
	if (!m_move)
	{
		return STATE_FAILURE;
	}
	if (!m_cowering)
	{
		// RW 0x74952D / 0x749693 phase 0: the goal is the path's last node once the path is there
		Path *path = a.mover().path();
		if (m_goalFromPath && path && !a.mover().isWaitingForPath() && path->getLastNode())
		{
			m_goal = *path->getLastNode()->getPosition();
			m_move->setGoal(m_goal);
			m_goalFromPath = false;
		}
		const StateReturnType st = m_move->update();
		if (!m_cower || st == STATE_CONTINUE)
		{
			return m_cower ? STATE_CONTINUE : st;
		}
		// RW 0x749693: the move ended (either way): cower
		m_cowering = true;
		a.setLocomotorGoalNone();       // vslot 0x220
		a.mover().endingMove();         // RW 0x6627CB
		a.mover().setFinalPosition(m_goal, false); // AI + 0x180, + 0x3B0 = 0
		const unsigned now = a.frame();
		if (a.fastCower())
		{
			m_cowerUntil = (a.minCowerTime() >> 2) + now;
		}
		else
		{
			m_cowerUntil = (unsigned)owner().logic().random().getValue((int)a.minCowerTime(), (int)a.maxCowerTime(), "AIStates.cpp", 0xFDA) + now;
		}
		return STATE_CONTINUE;
	}
	// phase 1
	Object *src = machine().goalObject();
	if (!src || src->isEffectivelyDead() || m_cowerUntil < a.frame())
	{
		return STATE_SUCCESS;
	}
	a.setLocomotorGoalNone();
	a.setModelCondition("MOVING", false);
	turnTo(owner(), *src->getPosition());
	return STATE_CONTINUE;
}

void AIPanicState::onExit(StateExitType)
{
	// RW 0x7495F9 / 0x7497FA
	if (m_move)
	{
		m_move->onExit();
		m_move.reset();
	}
	ai().setModelCondition(m_cower ? "EMOTION_AFRAID" : "PANICKING", false);
	if (m_cower)
	{
		ai().setFastCower(false);
	}
}

void AIPanicState::crc(StateHasher &h) const
{
	h.addBool(m_cower);
	h.addFloat(m_goal.x);
	h.addFloat(m_goal.y);
	h.addFloat(m_goal.z);
	h.addI32(m_budget);
	h.addBool(m_goalFromPath);
	h.addBool(m_cowering);
	h.addU32(m_cowerUntil);
	h.addBool(m_move != nullptr);
	if (m_move)
	{
		m_move->crc(h);
	}
}

void AIPanicState::onTemporaryStateEnded()
{
	if (m_move)
	{
		m_move->clearWaitingForPath();
	}
}

void AIPanicState::crcPersistent(StateHasher &h) const
{
	h.addFloat(m_goal.x);
	h.addFloat(m_goal.y);
	h.addFloat(m_goal.z);
	h.addU32(m_cowerUntil);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// 48
// ---------------------------------------------------------------------------------------------------------------------------------
StateReturnType AIBackAwayState::onEnter()
{
	// RW 0x745FBB
	Object &obj = owner();
	Object *src = machine().goalObject();
	if (!src)
	{
		return STATE_FAILURE;
	}
	AIUpdateInterface &a = ai();
	// AI vslot 0x244 (RW 0x66737F): the moving and turning conditions go
	for (const char *c : { "MOVING", "TURN_LEFT", "TURN_RIGHT", "TURN_LEFT_HIGH_SPEED", "TURN_RIGHT_HIGH_SPEED", "ACCELERATE", "DECELERATE", "ENGAGED" })
	{
		a.setModelCondition(c, false);
	}
	if (HordeContainInterface *hc = ownHorde(obj))
	{
		hc->recordBackUp(src); // slot 0x19C
		hc->setCowering(true); // slot 0x1A0
		hc->markDirty();       // slot 0x1D0
	}
	else
	{
		turnTo(obj, *src->getPosition());
	}
	return STATE_CONTINUE;
}

StateReturnType AIBackAwayState::update()
{
	// RW 0x7546DD
	Object *src = machine().goalObject();
	static const int kSpellBook = kindBit("SPELL_BOOK");
	if (!src || (src->isEffectivelyDead() && !(kSpellBook >= 0 && src->isKindOf((unsigned)kSpellBook))))
	{
		return STATE_SUCCESS;
	}
	ObjectID attacker = INVALID_ID;
	bool attacked = false;
	if (emotionAttackedWithin(owner(), 4, attacker))
	{
		// RW 0x693A6E: the attacker exists and has an AI (AI vslot 0x248 RW 0x8BD372: true)
		Object *who = owner().logic().findObjectByID(attacker);
		attacked = who && who->getAIUpdateInterface();
	}
	if (attacked && !machine().temporaryStateLocked())
	{
		return STATE_SUCCESS; // the client's VoiceDesperateAttack (RW 0x8DEDBB) is not ported (S-1027)
	}
	return STATE_CONTINUE;
}

void AIBackAwayState::onExit(StateExitType)
{
	// RW 0x741CA9
	Object &obj = owner();
	EmotionTrackerUpdate::clearEmotionRequest(obj, EMOTION_FEAR); // RW 0x68F3A3(4)
	if (HordeContainInterface *hc = ownHorde(obj))
	{
		hc->setCowering(false);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// 58
// ---------------------------------------------------------------------------------------------------------------------------------
StateReturnType AIQuarrelState::onEnter()
{
	// RW 0x74983C: the object's horde begins the quarrel: spectators 50 .. 60 away showing EMOTION_QUARRELSOME, the two fighters QUARRELSOME_FIGHTING
	HordeContainInterface *hc = ownHorde(owner());
	if (!hc)
	{
		return STATE_FAILURE;
	}
	HordeContainInterface::ConditionFlags spectators{}, fighters{};
	const int q = AIUpdateInterface::modelConditionBit("EMOTION_QUARRELSOME"), f = AIUpdateInterface::modelConditionBit("QUARRELSOME_FIGHTING");
	spectators[(size_t)q >> 5] |= 1u << (q & 31);
	fighters[(size_t)f >> 5] |= 1u << (f & 31);
	hc->beginQuarrel(50.0f, 60.0f, spectators, fighters);
	return STATE_CONTINUE;
}

StateReturnType AIQuarrelState::update()
{
	// RW 0x741D1C
	ObjectID attacker = INVALID_ID;
	return emotionAttackedWithin(owner(), 4, attacker) ? STATE_SUCCESS : STATE_CONTINUE;
}

void AIQuarrelState::onExit(StateExitType)
{
	// RW 0x741CEF
	if (HordeContainInterface *hc = ownHorde(owner()))
	{
		hc->endQuarrel();
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// 59
// ---------------------------------------------------------------------------------------------------------------------------------
AIFaceObjectIdleState::AIFaceObjectIdleState(AIStateMachine &m)
	: AIIdleState(m)
{
}

StateReturnType AIFaceObjectIdleState::onEnter()
{
	// RW 0x74D68C
	AIUpdateInterface &a = ai();
	m_canTurn = a.curLocomotor() && a.curLocomotor()->getTemplate().m_minSpeed == 0.0f; // RW 0x5E3796
	if (!machine().goalObject())
	{
		return STATE_FAILURE;
	}
	return AIIdleState::onEnter(); // mode 2: RW 0x7489B6
}

StateReturnType AIFaceObjectIdleState::update()
{
	// RW 0x75638B
	Object *target = machine().goalObject();
	if (!target)
	{
		return STATE_FAILURE;
	}
	Object &obj = owner();
	AIUpdateInterface &a = ai();
	const Coord3D tp = *target->getPosition();
	const float rel = emotionRelAngle(obj, tp);
	if ((float)SimMath::absD((double)rel) < kFacedEnough)
	{
		return STATE_SUCCESS;
	}
	if (!m_canTurn)
	{
		a.setLocomotorGoalPositionExplicit(tp); // AI vslot 0x210 (RW 0x662916)
	}
	else if (HordeContainInterface *hc = ownHorde(obj))
	{
		hc->setFacePoint(tp); // slot 0x210 (RW 0x86C1C6)
	}
	else
	{
		a.setLocomotorGoalOrientation(SimMath::addf32(obj.getOrientation(), rel)); // AI vslot 0x21C (RW 0x7563F3: addss)
	}
	return AIIdleState::update(); // RW 0x7553AB
}

void AIFaceObjectIdleState::onExit(StateExitType status)
{
	// RW 0x743DA9
	AIIdleState::onExit(status);
	if (HordeContainInterface *hc = ownHorde(owner()))
	{
		hc->clearFacePoint();
	}
}

void AIFaceObjectIdleState::crc(StateHasher &h) const
{
	AIIdleState::crc(h);
	h.addBool(m_canTurn);
}
