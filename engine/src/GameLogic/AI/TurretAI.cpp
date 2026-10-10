// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/AI/TurretAI.h for the target facts, the donors and the stop.

#include "GameLogic/AI/TurretAI.h"

#include "Common/NumericState.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "Common/Team.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "Common/INI/HostRealText.h"

#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace
{
const char *const kTurretAICpp = "TurretAI.cpp"; // the logic RNG call sites of RW's TurretAI (RW 0xC77918)
const unsigned kForever = 0x3FFFFFFFu;            // RW's UPDATE_SLEEP_FOREVER
const float kRelThresh = 0.035f;                  // RW 0x8DD1A6 (DAT 0xC2A1BC): about 2 degrees
const float kTwoPi = 6.28318548f;

enum TurretTargetType
{
	TARGET_NONE = 0,
	TARGET_OBJECT = 1,
	TARGET_POSITION = 2
};

// RW 0x644FD0: into (-PI, PI] (two SSE loops)
float normalizeAngle(float a)
{
	const float pi = 3.14159274f;
	while (a > pi)
	{
		a = SimMath::subf32(a, kTwoPi);
	}
	while (-pi >= a)
	{
		a = SimMath::addf32(a, kTwoPi);
	}
	return a;
}

// RW 0x8DBFCF (ZH frameToSleepTime): the earliest of the frames, as a sleep from now (continue when due)
StateReturnType frameToSleepTime(unsigned now, unsigned a, unsigned b)
{
	unsigned m = kForever;
	m = a < m ? a : m;
	m = b < m ? b : m;
	return m > now ? STATE_SLEEP(m - now) : STATE_CONTINUE;
}

bool isDead(const Object *o)
{
	return !o || o->isEffectivelyDead();
}

std::string lower(const std::string &s)
{
	std::string r = s;
	for (char &c : r)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return r;
}

// a raw line "Name = v1 v2 ; comment" -> name, values
bool splitLine(const std::string &text, std::string &name, std::vector<std::string> &values)
{
	std::string t = text.substr(0, text.find(';'));
	std::string cur;
	std::vector<std::string> tokens;
	for (char c : t)
	{
		if (std::isspace((unsigned char)c) || c == '=')
		{
			if (!cur.empty())
			{
				tokens.push_back(cur);
				cur.clear();
			}
			continue;
		}
		cur += c;
	}
	if (!cur.empty())
	{
		tokens.push_back(cur);
	}
	if (tokens.empty())
	{
		return false;
	}
	name = tokens[0];
	values.assign(tokens.begin() + 1, tokens.end());
	return true;
}

bool parseNumber(const std::string &tok, float &out)
{
	char *end = nullptr;
	const float v = strtofPortable(tok.c_str(), &end); // retail reads the field with sscanf "%f" (lane WIN-1)
	if (!end || *end != '\0' || end == tok.c_str())
	{
		return false;
	}
	out = v;
	return true;
}

int slotIndex(const std::string &tok)
{
	for (int i = 0; TheWeaponSlotTypeNames[i]; ++i)
	{
		if (lower(tok) == lower(TheWeaponSlotTypeNames[i]))
		{
			return i;
		}
	}
	return -1;
}

// ---- the states (ZH TurretAI.cpp; RW addresses in TurretAI.h) --------------------------------------------------------------------
class TurretStateBase : public AIState
{
public:
	TurretStateBase(AIStateMachine &m, TurretAI &t, const char *name) : AIState(m, name), m_turret(&t) {}

protected:
	TurretAI &turret() const { return *m_turret; }
	unsigned now() const { return ai().frame(); }

private:
	TurretAI *m_turret;
};

// RW 0x8DC6E7 / 0x8DCBF0
class TurretIdleState : public TurretStateBase
{
public:
	TurretIdleState(AIStateMachine &m, TurretAI &t) : TurretStateBase(m, t, "TurretAIIdleState") {}
	StateReturnType onEnter() override
	{
		// RW 0x8DC6E7: the AI's mood check is pushed back (RW 0x662DD9) and its turret sync cleared (AI + 0x210): not ported (S-1106); then RW 0x8DC6B0
		const unsigned n = now();
		const TurretAIData &d = turret().data();
		m_nextIdleScan = n + (unsigned)owner().logic().random().getValue((int)d.m_minIdleScanInterval, (int)d.m_maxIdleScanInterval, kTurretAICpp, 0x524);
		return frameToSleepTime(n, turret().nextIdleMoodTargetFrame(), m_nextIdleScan);
	}
	StateReturnType update() override
	{
		const unsigned n = now();
		if (m_nextIdleScan <= n)
		{
			return STATE_FAILURE;
		}
		turret().checkForIdleMoodTarget();
		return frameToSleepTime(n, turret().nextIdleMoodTargetFrame(), m_nextIdleScan);
	}
	void crc(StateHasher &h) const override { h.addU32(m_nextIdleScan); }
	void crcPersistent(StateHasher &h) const override { h.addU32(m_nextIdleScan); }

private:
	unsigned m_nextIdleScan = 0; // + 0x20
};

// RW 0x8DC75E / 0x8DD079
class TurretIdleScanState : public TurretStateBase
{
public:
	TurretIdleScanState(AIStateMachine &m, TurretAI &t) : TurretStateBase(m, t, "TurretAIIdleScanState") {}
	StateReturnType onEnter() override
	{
		const TurretAIData &d = turret().data();
		if (d.m_minIdleScanAngle == 0.0f && d.m_maxIdleScanAngle == 0.0f)
		{
			return STATE_SUCCESS;
		}
		GameLogicRandom &rng = owner().logic().random();
		const float r = rng.getValueReal(0.0f, SimMath::subf32(d.m_maxIdleScanAngle, d.m_minIdleScanAngle), kTurretAICpp, 0x565);
		m_desiredAngle = NumericState::pc24Add(r, d.m_minIdleScanAngle);
		if (rng.getValue(0, 1, kTurretAICpp, 0x566) == 0)
		{
			m_desiredAngle = NumericState::pc24Sub(0.0f, m_desiredAngle);
		}
		++turret().statsMutable().idleScans;
		return STATE_CONTINUE;
	}
	StateReturnType update() override
	{
		const TurretAIData &d = turret().data();
		const bool angle = turret().turnTowardsAngle(NumericState::pc24Add(m_desiredAngle, d.m_naturalTurretAngle), 0.5f, 0.0f);
		const bool pitch = turret().turnTowardsPitch(d.m_naturalTurretPitch, 0.5f);
		return angle && pitch ? STATE_SUCCESS : STATE_CONTINUE;
	}
	void crc(StateHasher &h) const override { h.addFloat(m_desiredAngle); }
	void crcPersistent(StateHasher &h) const override { h.addFloat(m_desiredAngle); }

private:
	float m_desiredAngle = 0.0f; // + 0x20
};

// RW 0x8DD1A6
class TurretAimState : public TurretStateBase
{
public:
	TurretAimState(AIStateMachine &m, TurretAI &t) : TurretStateBase(m, t, "TurretAIAimTurretState") {}
	StateReturnType update() override;
};

// RW 0x8DD01F
class TurretRecenterState : public TurretStateBase
{
public:
	TurretRecenterState(AIStateMachine &m, TurretAI &t) : TurretStateBase(m, t, "TurretAIRecenterTurretState") {}
	StateReturnType update() override
	{
		const TurretAIData &d = turret().data();
		const bool angle = turret().turnTowardsAngle(d.m_naturalTurretAngle, 0.5f, 0.0f);
		const bool pitch = turret().turnTowardsPitch(d.m_naturalTurretPitch, 0.5f);
		return angle && pitch ? STATE_SUCCESS : STATE_CONTINUE;
	}
};

// RW 0x8DC7E8 / 0x8DCC2F
class TurretHoldState : public TurretStateBase
{
public:
	TurretHoldState(AIStateMachine &m, TurretAI &t) : TurretStateBase(m, t, "TurretAIHoldTurretState") {}
	StateReturnType onEnter() override
	{
		const unsigned n = now();
		m_timestamp = n + turret().data().m_recenterTime;
		return frameToSleepTime(n, turret().nextIdleMoodTargetFrame(), m_timestamp);
	}
	StateReturnType update() override
	{
		const unsigned n = now();
		if (m_timestamp <= n)
		{
			return STATE_SUCCESS;
		}
		turret().checkForIdleMoodTarget();
		return frameToSleepTime(n, turret().nextIdleMoodTargetFrame(), m_timestamp);
	}
	void crc(StateHasher &h) const override { h.addU32(m_timestamp); }
	void crcPersistent(StateHasher &h) const override { h.addU32(m_timestamp); }

private:
	unsigned m_timestamp = 0; // + 0x20
};

// AIAttackFireWeaponState with the turret as its owner (RW 0x8DBFFC) and ZH's fire condition outOfWeaponRangeObject -> AIM: a victim that left the current weapon's
// range ends the shot (both transitions of FIRE lead to AIM)
class TurretFireState : public AIAttackFireState
{
public:
	TurretFireState(AIStateMachine &m, TurretAI &t) : AIAttackFireState(m, &t) {}
	StateReturnType update() override
	{
		Object *victim = machine().goalObject();
		ObjectWeapons *w = owner().getWeapons();
		if (victim && w && w->currentWeapon() && !w->isWithinAttackRange(*victim))
		{
			return STATE_SUCCESS;
		}
		return AIAttackFireState::update();
	}
};
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// TurretAIData
// ---------------------------------------------------------------------------------------------------------------------------------
bool TurretAIData::parseFromModuleLines(const std::vector<std::string> &lines, TurretAIData &out, std::string &error)
{
	out = TurretAIData();
	bool inBlock = false, found = false;
	const float rads = 0.017453292f; // RADS_PER_DEGREE (parseAngleReal RW 0x42EE15)
	const float angularVelocity = NumericState::pc24Mul(0.2f, rads); // parseAngularVelocityReal RW 0x73A2D6: deg x (SECONDS_PER_LOGICFRAME x RADS_PER_DEGREE)
	for (const std::string &text : lines)
	{
		std::string name;
		std::vector<std::string> v;
		if (!splitLine(text, name, v))
		{
			continue;
		}
		const std::string key = lower(name);
		if (!inBlock)
		{
			if (key == "turret" && v.empty())
			{
				if (found)
				{
					error = "a second Turret block (RW keeps one turret per AI: AltTurret is not in 2.01)";
					return true;
				}
				inBlock = found = true;
			}
			continue;
		}
		if (key == "end")
		{
			inBlock = false;
			continue;
		}
		float f = 0.0f;
		auto number = [&](size_t i) -> bool {
			if (i >= v.size() || !parseNumber(v[i], f))
			{
				error = "Turret " + name + ": not a number";
				return false;
			}
			return true;
		};
		auto yes = [&]() -> int {
			if (v.empty())
				return -1;
			const std::string b = lower(v[0]);
			return b == "yes" || b == "true" ? 1 : (b == "no" || b == "false" ? 0 : -1);
		};
		if (key == "turretturnrate" || key == "turretpitchrate")
		{
			if (!number(0))
				return true;
			(key == "turretturnrate" ? out.m_turnRate : out.m_pitchRate) = NumericState::pc24Mul(f, angularVelocity);
		}
		else if (key == "naturalturretangle" || key == "naturalturretpitch" || key == "firepitch" || key == "minphysicalpitch" || key == "groundunitpitch" ||
		         key == "minidlescanangle" || key == "maxidlescanangle" || key == "turretmaxdeflectioncw" || key == "turretmaxdeflectionacw")
		{
			if (!number(0))
				return true;
			const float a = NumericState::pc24Mul(f, rads);
			if (key == "naturalturretangle")
				out.m_naturalTurretAngle = a;
			else if (key == "naturalturretpitch")
				out.m_naturalTurretPitch = a;
			else if (key == "firepitch")
				out.m_firePitch = a;
			else if (key == "minphysicalpitch")
				out.m_minPitch = a;
			else if (key == "groundunitpitch")
				out.m_groundUnitPitch = a;
			else if (key == "minidlescanangle")
				out.m_minIdleScanAngle = a;
			else if (key == "maxidlescanangle")
				out.m_maxIdleScanAngle = a;
			else if (key == "turretmaxdeflectioncw")
				out.m_maxDeflectionCW = a;
			else
				out.m_maxDeflectionACW = a;
		}
		else if (key == "minidlescaninterval" || key == "maxidlescaninterval" || key == "recentertime")
		{
			// parseDurationUnsignedInt RW 0x73A429: an unsigned millisecond count
			char *end = nullptr;
			const unsigned long ms = v.empty() ? 0ul : std::strtoul(v[0].c_str(), &end, 10);
			if (v.empty() || !end || *end != '\0' || end == v[0].c_str() || v[0][0] == '-')
			{
				error = "Turret " + name + ": not a duration";
				return true;
			}
			const unsigned frames = NumericState::ceilScaled((std::uint32_t)ms, 0.005f);
			(key == "minidlescaninterval" ? out.m_minIdleScanInterval : key == "maxidlescaninterval" ? out.m_maxIdleScanInterval : out.m_recenterTime) = frames;
		}
		else if (key == "turretfireanglesweep" || key == "turretsweepspeedmodifier")
		{
			// RW 0x8DC24C / 0x8DC27F: a slot name, then the value (an angle / a real)
			const int slot = v.empty() ? -1 : slotIndex(v[0]);
			if (slot < 0 || slot >= TURRET_WEAPON_SLOTS || !number(1))
			{
				error = "Turret " + name + ": bad slot or value";
				return true;
			}
			if (key == "turretfireanglesweep")
				out.m_turretFireAngleSweep[slot] = NumericState::pc24Mul(f, rads);
			else
				out.m_turretSweepSpeedModifier[slot] = f;
		}
		else if (key == "controlledweaponslots")
		{
			// RW 0x8DC20F: the slot names, a mask
			for (const std::string &t : v)
			{
				const int slot = slotIndex(t);
				if (slot < 0)
				{
					error = "Turret ControlledWeaponSlots: unknown slot " + t;
					return true;
				}
				out.m_turretWeaponSlots |= 1u << slot;
			}
		}
		else if (key == "initiallydisabled" || key == "fireswhileturning" || key == "allowspitch")
		{
			const int b = yes();
			if (b < 0)
			{
				error = "Turret " + name + ": not a bool";
				return true;
			}
			if (key == "initiallydisabled")
				out.m_initiallyDisabled = b != 0;
			else if (key == "fireswhileturning")
				out.m_firesWhileTurning = b != 0;
			else
			{
				out.m_allowsPitch = b != 0;
				if (b)
				{
					error = "Turret AllowsPitch = Yes: the aim's pitch of RW 0x8DD1A6 is not ported (S-1106; no 2.01 turret pitches)";
					return true;
				}
			}
		}
		else
		{
			error = "Turret: unknown field " + name;
			return true;
		}
	}
	if (inBlock)
	{
		error = "Turret block without End";
	}
	else if (found && out.m_turretWeaponSlots == 0)
	{
		error = "TurretAI MUST specify controlled weapon slots!"; // RW 0x8DC8B0's INI error
	}
	return found;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// TurretAI
// ---------------------------------------------------------------------------------------------------------------------------------
TurretAI::TurretAI(AIUpdateInterface &ai, const TurretAIData &data, int whichTurret)
	: m_ai(&ai)
	, m_data(&data)
	, m_whichTurret(whichTurret)
	, m_angle(data.m_naturalTurretAngle)
	, m_pitch(data.m_naturalTurretPitch)
	, m_enabled(!data.m_initiallyDisabled)
	, m_firesWhileTurning(data.m_firesWhileTurning)
{
	// RW 0x8DBFFC (ZH TurretStateMachine): the first state is the default
	m_machine = std::make_unique<AIStateMachine>(ai, "TurretStateMachine");
	AIStateMachine &m = *m_machine;
	m.defineState(TURRETAI_IDLE, std::make_unique<TurretIdleState>(m, *this), TURRETAI_IDLE, TURRETAI_IDLESCAN);
	m.defineState(TURRETAI_IDLESCAN, std::make_unique<TurretIdleScanState>(m, *this), TURRETAI_HOLD, TURRETAI_HOLD);
	m.defineState(TURRETAI_AIM, std::make_unique<TurretAimState>(m, *this), TURRETAI_FIRE, TURRETAI_HOLD);
	m.defineState(TURRETAI_FIRE, std::make_unique<TurretFireState>(m, *this), TURRETAI_AIM, TURRETAI_AIM);
	m.defineState(TURRETAI_RECENTER, std::make_unique<TurretRecenterState>(m, *this), TURRETAI_IDLE, TURRETAI_IDLE);
	m.defineState(TURRETAI_HOLD, std::make_unique<TurretHoldState>(m, *this), TURRETAI_RECENTER, TURRETAI_RECENTER);
	m.initDefaultState();
}

TurretAI::~TurretAI()
{
	if (m_machine)
	{
		m_machine->halt();
	}
}

const char *TurretAI::stopLine()
{
	return "[S-1106] TurretAI (lane GARRISON-2): the turret's sounds and the client's reactToTurretChange, the enemy AI's targeter list (no aim prevention), the bridge attack "
	       "points, the AI's mood timer (AI + 0x21C: the turret's idle sleeps use the AI's MoodAttackCheckRate) and mood matrix (RW 0x664E18), "
	       "the pitch of AllowsPitch turrets and the turreted launch composition RW 0x6CAD3F .. 0x6CB43B (S-360) are not ported";
}

Object &TurretAI::owner() const
{
	return *m_ai->getObject();
}

void TurretAI::setState(unsigned id)
{
	const unsigned old = m_machine->currentStateId();
	m_machine->setState(id);
	if (old != id)
	{
		m_sleepUntil = m_ai->frame(); // ZH friend_notifyStateMachineChanged
	}
}

bool TurretAI::isOwnersCurWeaponOnTurret() const
{
	ObjectWeapons *w = owner().getWeapons();
	int slot = -1;
	return w && w->currentWeapon(&slot) && isWeaponSlotOnTurret(slot);
}

int TurretAI::getTurretTarget(Object *&obj, Coord3D &pos)
{
	obj = nullptr;
	pos = Coord3D{ 0.0f, 0.0f, 0.0f };
	if (m_target == TARGET_OBJECT)
	{
		obj = m_machine->goalObject();
		if (isDead(obj))
		{
			m_machine->setGoalObject(INVALID_ID);
			m_target = TARGET_NONE;
			m_targetWasSetByIdleMood = false;
		}
	}
	else if (m_target == TARGET_POSITION)
	{
		pos = m_machine->goalPosition();
	}
	return m_target;
}

void TurretAI::setTurretTargetObject(Object *victim, bool forceAttacking)
{
	if (isDead(victim) || !isOwnersCurWeaponOnTurret())
	{
		victim = nullptr;
	}
	// RW 0x8DC575 (removeSelfAsTargeter) has no port: the targeter lists are S-1106
	m_machine->setGoalObject(victim ? victim->getID() : (ObjectID)INVALID_ID);
	m_targetWasSetByIdleMood = false;
	m_target = victim ? TARGET_OBJECT : TARGET_NONE;
	m_isForceAttacking = forceAttacking;
	const unsigned sid = m_machine->currentStateId();
	if (victim)
	{
		if (sid != TURRETAI_AIM && sid != TURRETAI_FIRE)
		{
			setState(TURRETAI_AIM);
		}
		m_victimInitialTeam = victim->getTeam();
	}
	else
	{
		if (sid == TURRETAI_AIM || sid == TURRETAI_FIRE)
		{
			setState(TURRETAI_HOLD);
		}
		m_victimInitialTeam = nullptr;
	}
}

void TurretAI::setTurretTargetPosition(const Coord3D *pos)
{
	if (!pos || !isOwnersCurWeaponOnTurret())
	{
		pos = nullptr;
	}
	m_machine->setGoalObject(INVALID_ID);
	if (pos)
	{
		m_machine->setGoalPosition(*pos);
	}
	m_target = pos ? TARGET_POSITION : TARGET_NONE;
	m_targetWasSetByIdleMood = false;
	const unsigned sid = m_machine->currentStateId();
	if (pos)
	{
		if (sid != TURRETAI_AIM && sid != TURRETAI_FIRE)
		{
			setState(TURRETAI_AIM);
		}
	}
	else if (sid == TURRETAI_AIM || sid == TURRETAI_FIRE)
	{
		setState(TURRETAI_HOLD);
	}
	m_victimInitialTeam = nullptr;
}

void TurretAI::recenterTurret()
{
	setState(TURRETAI_RECENTER);
}

bool TurretAI::isTurretInNaturalPosition() const
{
	return m_data->m_naturalTurretAngle == m_angle && m_data->m_naturalTurretPitch == m_pitch;
}

bool TurretAI::isTryingToAimAtTarget(const Object *victim)
{
	Object *obj = nullptr;
	Coord3D pos;
	return m_machine->currentStateId() == TURRETAI_AIM && getTurretTarget(obj, pos) == TARGET_OBJECT && obj == victim;
}

void TurretAI::setTurretEnabled(bool on)
{
	if (on && !m_enabled)
	{
		m_sleepUntil = m_ai->frame(); // ZH setTurretEnabled: wake up
	}
	m_enabled = on;
}

bool TurretAI::isSweepEnabled() const
{
	return m_enableSweepUntil != 0 && m_enableSweepUntil > m_ai->frame();
}

unsigned TurretAI::nextIdleMoodTargetFrame() const
{
	// RW 0x8DC693 reads the AI's next mood check (AI + 0x21C), which the port does not keep (S-1106): the AI's MoodAttackCheckRate from now (now: no scan)
	return m_ai->frame() + m_ai->moodCheckInterval();
}

bool TurretAI::isAnyWeaponInRangeOf(const Object &victim) const
{
	ObjectWeapons *w = owner().getWeapons();
	if (!w)
	{
		return false;
	}
	for (int i = 0; i < TURRET_WEAPON_SLOTS; ++i)
	{
		if (w->weaponInSlot(i) && isWeaponSlotOnTurret(i) && w->isSlotWithinAttackRange(i, victim))
		{
			return true;
		}
	}
	return false;
}

void TurretAI::checkForIdleMoodTarget()
{
	// RW 0x8DCBA6: the mood matrix action (RW 0x664E18(0), lane SCRIPT-3) with IgnoreAll (0x10) looks for nothing; else the AI's next mood target RW 0x66844A
	if (m_ai->moodAdjustment(0) & 0x10u)
	{
		return;
	}
	Object *enemy = m_ai->nextMoodTarget();
	if (!enemy)
	{
		return;
	}
	setTurretTargetObject(enemy, false);
	if (ObjectWeapons *w = owner().getWeapons())
	{
		w->chooseBestWeaponForTarget(enemy, PREFER_TEMPLATE_DEFAULT, CMD_FROM_AI); // RW 0x8DCBDC .. 0x8DCBE3: RW 0x68B619(enemy, 5, 2)
	}
	m_targetWasSetByIdleMood = true;
	++m_stats.idleTargets;
}

bool TurretAI::turnTowardsAngle(float desired, float rateModifier, float relThresh)
{
	desired = normalizeAngle(desired);
	float target = desired;
	const TurretAIData &d = *m_data;
	if (SimMath::addf32(d.m_maxDeflectionACW, d.m_maxDeflectionCW) < kTwoPi)
	{
		// RotWK's deflection window around the natural angle: outside it, the turret heads for the natural angle
		const float lo = normalizeAngle(SimMath::subf32(d.m_naturalTurretAngle, d.m_maxDeflectionCW));
		const float hi = normalizeAngle(SimMath::addf32(d.m_naturalTurretAngle, d.m_maxDeflectionACW));
		const bool inside = lo > hi ? (lo <= desired || desired <= hi) : (lo <= desired && desired <= hi);
		if (!inside)
		{
			target = d.m_naturalTurretAngle;
		}
	}
	const float rate = NumericState::pc24Mul(rateModifier, d.m_turnRate);
	const float orig = m_angle;
	const float diff = normalizeAngle(SimMath::subf32(target, orig));
	Object &obj = owner();
	static const int kTurretRotate = CombatNames::modelCondition("TURRET_ROTATE");
	float actual;
	if (rate > (float)SimMath::absD(diff))
	{
		actual = target;
		if (obj.testModelCondition(kTurretRotate))
		{
			obj.setModelConditionState(kTurretRotate, false);
		}
	}
	else
	{
		actual = diff > 0.0f ? SimMath::addf32(rate, orig) : SimMath::subf32(orig, rate);
		if (!obj.testModelCondition(kTurretRotate))
		{
			obj.setModelConditionState(kTurretRotate, true);
		}
		m_playRotSound = true;
		++m_stats.turnFrames;
	}
	m_angle = normalizeAngle(actual);
	// RW 0x68B810 (reactToTurretChange) is the client's
	return !(relThresh < (float)SimMath::absD(NumericState::pc24Sub(m_angle, desired)));
}

bool TurretAI::turnTowardsPitch(float desired, float rateModifier)
{
	if (!m_data->m_allowsPitch)
	{
		return true;
	}
	desired = normalizeAngle(desired);
	const float rate = NumericState::pc24Mul(rateModifier, m_data->m_pitchRate);
	const float diff = normalizeAngle(SimMath::subf32(desired, m_pitch));
	float actual;
	if (rate > (float)SimMath::absD(diff))
	{
		actual = desired;
	}
	else
	{
		actual = diff > 0.0f ? SimMath::addf32(m_pitch, rate) : SimMath::subf32(m_pitch, rate);
		m_playPitchSound = true;
	}
	m_pitch = normalizeAngle(actual);
	return m_pitch == desired;
}

void TurretAI::setTurretAngleCondition(float relAngle)
{
	// RW 0x8DCF1D: the four conditions are cleared, then the quadrant's is set ((0.785, 2.356] 90, (2.356, 3.927] 180, (3.927, 5.498] 270, else 0)
	static const int k0 = CombatNames::modelCondition("TURRET_ANGLE_0");
	static const int k90 = CombatNames::modelCondition("TURRET_ANGLE_90");
	static const int k180 = CombatNames::modelCondition("TURRET_ANGLE_180");
	static const int k270 = CombatNames::modelCondition("TURRET_ANGLE_270");
	float a = relAngle;
	if (a < 0.0f)
	{
		a = SimMath::addf32(a, kTwoPi);
	}
	int set = k0;
	if (0.785398185f < a && a <= 2.35619450f)
	{
		set = k90;
	}
	else if (2.35619450f < a && a <= 3.92699075f)
	{
		set = k180;
	}
	else if (3.92699075f < a && a <= 5.49778700f)
	{
		set = k270;
	}
	Object &obj = owner();
	for (const int c : { k0, k90, k180, k270 })
	{
		const bool on = c == set;
		if (obj.testModelCondition(c) != on)
		{
			obj.setModelConditionState(c, on);
		}
	}
}

unsigned TurretAI::updateTurretAI()
{
	const unsigned now = m_ai->frame();
	if (m_sleepUntil != 0 && now < m_sleepUntil)
	{
		return m_sleepUntil - now;
	}
	++m_stats.updates;
	unsigned sleep = kForever;
	if (!m_firesWhileTurning || m_continuousFireExpirationFrame <= now)
	{
		m_playRotSound = false;
		m_playPitchSound = false;
	}
	if (m_enabled || m_machine->currentStateId() == TURRETAI_RECENTER)
	{
		m_didFire = false;
		const StateReturnType st = m_machine->updateStateMachine();
		if (m_didFire)
		{
			m_enableSweepUntil = now + 3;
			m_continuousFireExpirationFrame = now + 3;
		}
		// the turret move sound (RW 0x8DC60C / 0x8DCE4A) is the client's (S-1106)
		const int r = (int)st;
		if (r < 1)
		{
			sleep = 1;
		}
		else if ((unsigned)r < kForever)
		{
			sleep = (unsigned)r;
		}
	}
	m_sleepUntil = now + sleep;
	return sleep;
}

void TurretAI::crc(StateHasher &h) const
{
	h.addI32(m_whichTurret);
	m_machine->crc(h);
	for (const unsigned id : { TURRETAI_IDLE, TURRETAI_IDLESCAN, TURRETAI_HOLD })
	{
		m_machine->findState(id)->crcPersistent(h);
	}
	h.addFloat(m_angle);
	h.addFloat(m_pitch);
	h.addU32(m_enableSweepUntil);
	h.addU32(m_victimInitialTeam ? m_victimInitialTeam->getID() : 0u);
	h.addI32(m_target);
	h.addU32(m_continuousFireExpirationFrame);
	h.addU32(m_sleepUntil);
	h.addBool(m_playRotSound);
	h.addBool(m_playPitchSound);
	h.addBool(m_positiveSweep);
	h.addBool(m_didFire);
	h.addBool(m_enabled);
	h.addBool(m_firesWhileTurning);
	h.addBool(m_isForceAttacking);
	h.addBool(m_targetWasSetByIdleMood);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the aim (RW 0x8DD1A6)
// ---------------------------------------------------------------------------------------------------------------------------------
StateReturnType TurretAimState::update()
{
	TurretAI &t = turret();
	AIUpdateInterface &a = ai();
	Object &obj = owner();
	Object *enemy = nullptr;
	Coord3D enemyPos;
	const int type = t.getTurretTarget(enemy, enemyPos);
	Object *enemyForRange = enemy;
	bool nothingInRange = false;
	if (type == TARGET_NONE)
	{
		return STATE_FAILURE;
	}
	if (type == TARGET_OBJECT)
	{
		const bool isPrimaryEnemy = enemy && enemy == a.stateMachine().goalObject();
		ObjectWeapons *w = obj.getWeapons();
		// RW 0x691269 (isAbleToAttack) and 0x68D6A6(continued target, forced or not, the AI's last command source): the port's canAttackObject stands in (S-1106)
		const bool able = w && enemy && w->canAttackObject(*enemy, a.lastCommandSource(), t.forceAttacking());
		nothingInRange = !enemy || !t.isAnyWeaponInRangeOf(*enemy);
		if (!enemy || !able || (!isPrimaryEnemy && nothingInRange) || enemy->getTeam() != t.victimInitialTeam())
		{
			if (t.targetWasSetByIdleMood())
			{
				t.setTurretTargetObject(nullptr, false);
			}
			return STATE_FAILURE;
		}
		enemyPos = *enemy->getPosition(); // the bridge's attack points: 2.01 maps have no bridges
	}
	ObjectWeapons *w = obj.getWeapons();
	int slot = -1;
	if (!w || !w->currentWeapon(&slot))
	{
		return STATE_FAILURE;
	}
	const float relAngle = a.relativeAngleTo(enemyPos);
	t.setTurretAngleCondition(relAngle);
	const TurretAIData &d = t.data();
	const float sweep = slot >= 0 && slot < TURRET_WEAPON_SLOTS ? d.m_turretFireAngleSweep[slot] : 0.0f;
	float aimAngle = relAngle;
	float modifier = 1.0f;
	if (sweep > 0.0f && t.isSweepEnabled())
	{
		aimAngle = t.positiveSweep() ? SimMath::addf32(sweep, relAngle) : SimMath::subf32(relAngle, sweep);
		modifier = d.m_turretSweepSpeedModifier[slot];
	}
	bool aligned = t.turnTowardsAngle(aimAngle, modifier, kRelThresh);
	if (sweep > 0.0f)
	{
		if (aligned)
		{
			t.setPositiveSweep(!t.positiveSweep());
		}
		aligned = !(sweep <= (float)SimMath::absD(normalizeAngle(SimMath::subf32(relAngle, t.getTurretAngle()))));
	}
	// AllowsPitch turrets are refused at parse (S-1106): the pitch is always aligned
	if (aligned)
	{
		const bool inRange = enemyForRange ? w->isWithinAttackRange(*enemyForRange) : w->isWithinAttackRange(enemyPos);
		if (inRange && !nothingInRange) // the enemy's aim prevention (RW AI slot 0x208) is S-1106
		{
			return STATE_SUCCESS;
		}
	}
	return STATE_CONTINUE;
}
