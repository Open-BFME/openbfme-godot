// OpenBFME. GPL-3.0.
// See GameLogic/Module/SlavedUpdate.h for the sources and what is inference.

#include "GameLogic/Module/SlavedUpdate.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/AI/AIApproachMath.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const char *const kStop =
	"[S-1426] SlavedUpdate (lane MOD-4): onEnslave / onSlaverDie (RW 0x8A510B / 0x8A51B9), DieOnMastersDeath, the team follow, AttackRange (RW 0x8A5319), "
	"GuardMaxRange, GuardWanderRange and LeashRange (RW 0x8A5D00 / 0x8A567E) run; INFERENCE / not ported: the repair path (RW 0x8A5B75 / 0x8A58FA, no retail "
	"object repairs), the scout path (the slaver's path end, AI + 0x140), AI command 15 as COMBAT-1's attack-move (its second argument RW 0x68B58C(0) + 0x34 "
	"unread, S-328), onSlaverDamage's AI command 0x1D (ZH go prone), endRepair's AI vslot 0x238 and locomotor bits, the player's RW 0x79F486, the fade "
	"and the drawable links (client)";

const int kSlavedUpdateRate = LOGICFRAMES_PER_SECOND / 4; // RW 0xDE95FC (RW 0xBC6E11: [0xD9F608] / 4)

int statusBit(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}

// RW 0x5E3BE4 (this = the object): x87 (x - px)^2 + (y - py)^2 + (z - pz)^2, the subtractions and the products each rounded at PC24, left wide
double distSqTo(const Object &obj, const Coord3D &p)
{
	const Coord3D *o = obj.getPosition();
	const double dx = SimMath::pc24SubW((double)o->x, (double)p.x), dy = SimMath::pc24SubW((double)o->y, (double)p.y), dz = SimMath::pc24SubW((double)o->z, (double)p.z);
	return SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dz, dz), SimMath::pc24MulW(dy, dy)), SimMath::pc24MulW(dx, dx));
}

// RW 0x403175 Coord3D::normalize: the PC24 root of the float32 sum of squares (RW 0x403111), scaled by its float32 inverse when it is not 0
void normalize3(Coord3D &v)
{
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(v.x, v.y, v.z)));
	if (len != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, len);
		v.x = SimMath::mulf32(v.x, inv);
		v.y = SimMath::mulf32(v.y, inv);
		v.z = SimMath::mulf32(v.z, inv);
	}
}

// fild of an int square (RW 0x8A6040 .. 0x8A6046: imul then fild)
double intSquare(int r)
{
	return (double)(r * r);
}

// GameLogicRandomValueReal(0, 2 pi) (RW 0xBDD38C), then the offset (cos a, sin a) * range: fcos / fsin (RW 0x42F4E0 / 0x42F4D0), fimul range, fadd 0, fstp
void randomOffset(GameLogic &logic, int range, int line, Coord3D &offset)
{
	const float a = logic.random().getValueReal(0.0f, 6.2831855f, "SlavedUpdate.cpp", line);
	offset = Coord3D{ 0.0f, 0.0f, 0.0f };
	offset.x = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW((double)SimMath::cosDet(a), (double)range), (double)offset.x));
	offset.y = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW((double)SimMath::sinDet(a), (double)range), (double)offset.y));
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#define SU_OFF(m) (int)offsetof(SlavedUpdateModuleData, m)
const FieldParse kSlavedUpdate[] = { // RW 0xC08420
	{ "LeashRange", INI::parseInt, nullptr, SU_OFF(m_leashRange) },
	{ "GuardMaxRange", INI::parseInt, nullptr, SU_OFF(m_guardMaxRange) },
	{ "GuardWanderRange", INI::parseInt, nullptr, SU_OFF(m_guardWanderRange) },
	{ "AttackRange", INI::parseInt, nullptr, SU_OFF(m_attackRange) },
	{ "AttackWanderRange", INI::parseInt, nullptr, SU_OFF(m_attackWanderRange) },
	{ "ScoutRange", INI::parseInt, nullptr, SU_OFF(m_scoutRange) },
	{ "ScoutWanderRange", INI::parseInt, nullptr, SU_OFF(m_scoutWanderRange) },
	{ "RepairRange", INI::parseInt, nullptr, SU_OFF(m_repairRange) },
	{ "RepairMinAltitude", INI::parseReal, nullptr, SU_OFF(m_repairMinAltitude) },
	{ "RepairMaxAltitude", INI::parseReal, nullptr, SU_OFF(m_repairMaxAltitude) },
	{ "DistToTargetToGrantRangeBonus", INI::parseInt, nullptr, SU_OFF(m_distToTargetToGrantRangeBonus) },
	{ "RepairRatePerSecond", INI::parseReal, nullptr, SU_OFF(m_repairRatePerSecond) },
	{ "RepairWhenBelowHealth%", INI::parseInt, nullptr, SU_OFF(m_repairWhenBelowHealthPercent) },
	{ "RepairMinReadyTime", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_repairMinReadyTime) },
	{ "RepairMaxReadyTime", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_repairMaxReadyTime) },
	{ "RepairMinWeldTime", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_repairMinWeldTime) },
	{ "RepairMaxWeldTime", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_repairMaxWeldTime) },
	{ "RepairWeldingSys", INI::parseAsciiString, nullptr, SU_OFF(m_repairWeldingSys) },
	{ "RepairWeldingFXBone", INI::parseAsciiString, nullptr, SU_OFF(m_repairWeldingFXBone) },
	{ "StayOnSameLayerAsMaster", INI::parseBool, nullptr, SU_OFF(m_stayOnSameLayerAsMaster) },
	{ "UseSlaverAsControlForEvaObjectSightedEvents", INI::parseBool, nullptr, SU_OFF(m_useSlaverAsControlForEvaObjectSightedEvents) },
	{ "DieOnMastersDeath", INI::parseBool, nullptr, SU_OFF(m_dieOnMastersDeath) },
	{ "GuardPositionOffset", INI::parseCoord3D, nullptr, SU_OFF(m_guardPositionOffset) },
	{ "FadeOutRange", INI::parseInt, nullptr, SU_OFF(m_fadeOutRange) },
	{ "FadeTime", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_fadeTime) },
	{ "MarkUnselectable", INI::parseBool, nullptr, SU_OFF(m_markUnselectable) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SU_OFF
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
} // namespace

void SlavedUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	// RW 0x655085: every field 0 but MarkUnselectable (true); GuardWanderRange (+ 0x10) and RepairRange (+ 0x28) are not set by the constructor (INFERENCE: 0)
	p.add(kSlavedUpdate);
}

SlavedUpdateInterface *SlavedUpdateInterface::of(Object &obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		if (SlavedUpdateInterface *s = dynamic_cast<SlavedUpdateInterface *>(m.get()))
		{
			return s;
		}
	}
	return nullptr;
}

SlavedUpdate::Stats &SlavedUpdate::stats()
{
	static thread_local Stats s;
	return s;
}

SlavedUpdate::SlavedUpdate(Thing *thing, const SlavedUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	// RW 0x8A4F67: every field 0 (the UpdateModule constructor's wake frame)
}

void SlavedUpdate::onObjectCreated()
{
	// RW 0x8A4FF2: a repairing slave (RepairRatePerSecond above 0) starts PACKING (model condition 94, + 0x114 bit 30)
	if (m_data->m_repairRatePerSecond > 0.0f)
	{
		static const int kPacking = Construction::modelConditionIndex("PACKING");
		getObject()->setModelConditionState(kPacking, true);
	}
}

void SlavedUpdate::onEnslave(const Object *slaver)
{
	// RW 0x8A510B
	if (!slaver)
	{
		return;
	}
	m_slaverID = slaver->getID();
	randomOffset(getObject()->logic(), m_data->m_guardMaxRange, 0x35E, m_guardOffset);
	if (m_data->m_markUnselectable)
	{
		static const int kUnselectable = statusBit("UNSELECTABLE"); // status 3 (RW 0x62684D(3, 1))
		getObject()->setStatus((unsigned)kUnselectable, true);
	}
	// the drawable's slaver link (RW 0x6713A8): client
}

void SlavedUpdate::onSlaverDie(const DieModuleInterface::Event *)
{
	stopSlavedEffects(); // RW 0x8A52A7 -> 0x8A51B9
}

void SlavedUpdate::stopSlavedEffects()
{
	// RW 0x8A51B9
	if (m_data->m_dieOnMastersDeath)
	{
		return;
	}
	m_slaverID = INVALID_ID;
	m_guardOffset = Coord3D{ 0.0f, 0.0f, 0.0f };
	static const int kUnselectable = statusBit("UNSELECTABLE");
	getObject()->setStatus((unsigned)kUnselectable, false); // RW 0x62684D(3, 0)
	getObject()->clearDisabled(3);                          // RW 0x692443(3): DISABLED_HELD
}

void SlavedUpdate::onSlaverDamage(const DamageInfo &)
{
	// RW 0x8A52FB: the AI's command 0x1D with the damage info, CMD_FROM_AI (RW 0x771726; ZH aiGoProne): not ported
	if (getObject()->getAIUpdateInterface())
	{
		++stats().unported;
		getObject()->logic().noteStop(kStop);
	}
}

void SlavedUpdate::endRepair()
{
	// RW 0x8A52B2
	if (m_repairState != 0)
	{
		m_repairState = 0;
		m_framesToWait = kSlavedUpdateRate;
		m_repairing = false;
	}
	// the AI's vslot 0x238(0) and the locomotor's + 0x44 bits 3 / 6: not identified (S-1426)
}

void SlavedUpdate::setLeashed(bool on)
{
	static const int kLeashedStatus = statusBit("LEASHED_RETURNING");                     // status 30
	static const int kLeashedCondition = Construction::modelConditionIndex("LEASHED_RETURNING"); // model condition 342
	Object *obj = getObject();
	obj->setStatus((unsigned)kLeashedStatus, on);
	if (obj->testModelCondition(kLeashedCondition) != on)
	{
		obj->setModelConditionState(kLeashedCondition, on);
	}
}

void SlavedUpdate::moveTo(Coord3D pos, bool idleFirst)
{
	// RW 0x8A567E
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	static const int kLeashedStatus = statusBit("LEASHED_RETURNING");
	if (obj->testStatus((unsigned)kLeashedStatus) && distSqTo(*obj, pos) <= intSquare(m_data->m_guardWanderRange))
	{
		setLeashed(false);
	}
	if (m_data->m_guardWanderRange != 0)
	{
		randomOffset(logic, m_data->m_guardWanderRange, 0x21F, m_guardOffset);
		pos.x = SimMath::addf32(m_guardOffset.x, pos.x);
		pos.y = SimMath::addf32(m_guardOffset.y, pos.y);
		m_guardOffset.z = logic.getGroundHeight(pos.x, pos.y); // TheTerrainLogic vslot 0x18 (x, y, 0)
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return;
	}
	if (idleFirst)
	{
		ai->aiIdle(CMD_FROM_AI);                 // RW 0x5E821A(2)
		ai->aiMoveToPosition(pos, CMD_FROM_AI);  // RW 0x66C4CA(pos, 2)
	}
	else
	{
		// RW 0x696266: AI command 15 (ZH AICMD_ATTACKMOVE_TO_POSITION) with CMD_FROM_AI: COMBAT-1's attack-move (S-328)
		ai->aiMoveToPosition(pos, CMD_FROM_AI);
		ai->armAttackMove(ai->stateMachine().goalPosition(), CMD_FROM_AI);
	}
	++stats().guardMoves;
}

void SlavedUpdate::doAttackLogic(Object &slaver, Object &target)
{
	// RW 0x8A5319
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Coord3D *tp = target.getPosition();
	// RW 0x6CA525(this = the object, its position, the target's position)
	const float dist = ApproachMath::edgeSquared(*tp, *obj->getPosition(), CombatQueries::boundingCircleRadius(*obj));
	Coord3D pos = *tp;
	const int range = m_data->m_attackRange;
	if (!(dist <= (float)(range * range)))
	{
		const Coord3D *sp = slaver.getPosition();
		Coord3D v{ SimMath::subf32(tp->x, sp->x), SimMath::subf32(tp->y, sp->y), SimMath::subf32(tp->z, sp->z) };
		normalize3(v); // Coord3D::normalize RW 0x403175
		const float r = (float)range;
		pos.x = SimMath::addf32(SimMath::mulf32(r, v.x), sp->x);
		pos.y = SimMath::addf32(SimMath::mulf32(v.y, r), sp->y);
		pos.z = SimMath::addf32(sp->z, SimMath::mulf32(v.z, r));
	}
	if (m_data->m_attackWanderRange != 0)
	{
		randomOffset(logic, m_data->m_attackWanderRange, 0x1B5, m_guardOffset);
		pos.x = SimMath::addf32(pos.x, m_guardOffset.x);
		pos.y = SimMath::addf32(m_guardOffset.y, pos.y);
		m_guardOffset.z = logic.getGroundHeight(pos.x, pos.y);
	}
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		ai->aiMoveToPosition(pos, CMD_FROM_AI); // RW 0x66C4CA(pos, 2)
	}
	// within DistToTargetToGrantRangeBonus^2 the slaver's weapon bonus bit 6 (+ 0x39C) sets: the drone spotting bonus has no RotWK user (not ported)
	++stats().attackMoves;
}

UpdateSleepTime SlavedUpdate::update()
{
	// RW 0x8A5D00
	if (m_framesToWait > 0)
	{
		--m_framesToWait;
	}
	if (m_repairState == 0)
	{
		if (m_framesToWait > 0)
		{
			return UPDATE_SLEEP_NONE;
		}
		m_framesToWait = kSlavedUpdateRate;
	}
	if (m_slaverID == INVALID_ID)
	{
		return UPDATE_SLEEP_NONE;
	}
	if (m_selfTaskFlag) // slaved slot 7 (+ 0x44)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (m_data->m_dieOnMastersDeath)
	{
		const Object *slaver = logic.findObjectByID(m_slaverID);
		if (!slaver || slaver->isEffectivelyDead())
		{
			// the player's RW 0x79F486 (+ 0x3DC) is not identified (S-1426)
			obj->kill(0); // RW 0x698EC3(8 = UNRESISTABLE, 0 = NORMAL)
			++stats().mastersDeathKills;
			return UPDATE_SLEEP_FOREVER;
		}
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai || !ai->curLocomotor()) // AI + 0x1F0
	{
		return UPDATE_SLEEP_NONE;
	}
	if (m_guardTargetID != INVALID_ID)
	{
		const Object *t = logic.findObjectByID(m_guardTargetID);
		if (!t || t->isEffectivelyDead())
		{
			m_guardTargetID = INVALID_ID;
		}
	}
	Object *slaver = logic.findObjectByID(m_slaverID);
	if (!slaver || slaver->isEffectivelyDead())
	{
		stopSlavedEffects();
		return UPDATE_SLEEP_NONE;
	}
	// RW 0x7A3DAD: the slaver's team toward ours
	// (RW calls the master's TEAM relationship, so a team override on the master counts; Sol's MOD-4 r1)
	const Team *slaverTeam = slaver->getTeam();
	if (!slaverTeam || slaverTeam->getRelationship(obj->getTeam()) != ALLIES)
	{
		obj->setTeam(slaver->getTeam()); // RW 0x6996DC(team, 0)
		stopSlavedEffects();
		return UPDATE_SLEEP_FOREVER;
	}
	// StayOnSameLayerAsMaster (RW 0x68BBE0 / 0x68BB9D): the layers are not ported (every object is on the ground layer here)
	AIUpdateInterface *slaverAI = slaver->getAIUpdateInterface();
	Object *target = slaverAI ? slaverAI->currentVictim() : nullptr; // RW 0x668303
	const int healthPercent = 100; // RepairRatePerSecond above 0 only (no retail slave repairs): S-1426
	if (healthPercent <= m_data->m_repairWhenBelowHealthPercent)
	{
		++stats().unported;
		logic.noteStop(kStop);
		return UPDATE_SLEEP_NONE;
	}
	if (m_data->m_attackRange != 0 && target)
	{
		endRepair();
		doAttackLogic(*slaver, *target);
		return UPDATE_SLEEP_NONE;
	}
	if (m_data->m_scoutRange != 0 && slaverAI)
	{
		++stats().unported; // the slaver's path end (AI + 0x140): S-1426
		logic.noteStop(kStop);
	}
	// the guard point: the slaver's transform times GuardPositionOffset (SSE, RW 0x8A5F85 .. 0x8A6016)
	const float *b = slaver->getBasis();
	const Coord3D *sp = slaver->getPosition();
	const Coord3D &o = m_data->m_guardPositionOffset;
	Coord3D guard;
	guard.x = SimMath::addf32(SimMath::addf32(SimMath::addf32(SimMath::mulf32(b[2], o.z), SimMath::mulf32(b[1], o.y)), SimMath::mulf32(b[0], o.x)), sp->x);
	guard.y = SimMath::addf32(SimMath::addf32(SimMath::addf32(SimMath::mulf32(b[5], o.z), SimMath::mulf32(b[4], o.y)), SimMath::mulf32(b[3], o.x)), sp->y);
	guard.z = SimMath::addf32(SimMath::addf32(SimMath::addf32(SimMath::mulf32(b[8], o.z), SimMath::mulf32(b[7], o.y)), SimMath::mulf32(b[6], o.x)), sp->z);
	// FadeOutRange: the drawable's fade (RW 0x670A50 / 0x670AA2): client; + 0x45 keeps whether it faded
	if (m_data->m_fadeOutRange != 0)
	{
		m_faded = !(distSqTo(*obj, guard) > intSquare(m_data->m_fadeOutRange));
	}
	if (m_data->m_guardMaxRange != 0)
	{
		if (const Object *t = logic.findObjectByID(m_guardTargetID))
		{
			endRepair();
			moveTo(*t->getPosition(), false);
		}
		else if (ai->isIdle())
		{
			if (distSqTo(*obj, guard) > 225.0 || distSqTo(*obj, *sp) > intSquare(m_data->m_guardMaxRange)) // RW 0xDB0CB0
			{
				endRepair();
				moveTo(guard, false);
			}
		}
		else if (ai->currentVictim() && distSqTo(*obj, *sp) > intSquare(m_data->m_guardMaxRange))
		{
			endRepair();
			moveTo(guard, true);
		}
	}
	if (m_data->m_leashRange != 0)
	{
		if (!ai->isIdle() && !(distSqTo(*obj, *sp) > intSquare(m_data->m_leashRange)))
		{
			return UPDATE_SLEEP_NONE;
		}
		if (!ai->isIdle())
		{
			setLeashed(true); // status 30 and model condition 342 (+ 0x134 bit 22)
			++stats().leashReturns;
		}
		endRepair();
		moveTo(guard, false);
	}
	return UPDATE_SLEEP_NONE;
}

void SlavedUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32((std::uint32_t)m_slaverID);
	h.addFloat(m_guardOffset.x);
	h.addFloat(m_guardOffset.y);
	h.addFloat(m_guardOffset.z);
	h.addI32(m_framesToWait);
	h.addI32(m_repairState);
	h.addBool(m_repairing);
	h.addU32((std::uint32_t)m_guardTargetID);
	h.addBool(m_selfTaskFlag);
	h.addBool(m_faded);
}

void SlavedUpdate::registerClass(ModuleFactory &modules)
{
	const char *name = "SlavedUpdate";
	modules.bindTypedData<SlavedUpdateModuleData>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const SlavedUpdateModuleData *typed = dynamic_cast<const SlavedUpdateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<SlavedUpdate>(thing, typed);
	});
}

std::vector<std::string> SlavedUpdate::stopLines()
{
	return { kStop };
}
