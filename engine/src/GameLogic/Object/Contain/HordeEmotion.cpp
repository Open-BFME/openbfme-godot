// OpenBFME. GPL-3.0.
//
// HordeContain's emotion slots (lane MODULES-3): what the emotion AI states (GameLogic/AI/AIEmotionStates.cpp) ask a horde, and the member pass branches that move
// its members. RotWK only, ported from the binary (caveat S-001). HordeContainInterface (module + 0x11C, vtable 0xC5B1F8):
//   * slot 0x13C RW 0x876D4E: a melee-engaged horde (interface + 0x184) ends the melee (slot 0x138 RW 0x86C0F9), then every member with an AI gets AI command 0x31 from
//     the AI (RW 0x852E2A(0, 2): busy);
//   * slot 0x19C RW 0x878905 (the back-up records of BACK_AWAY): with module data, every member is recorded when the horde or the member is UNCONTROLLABLY_SCARED (status
//     0x45, RW 0x44DDEC), the data's CowerRadius (+ 0x1F0) is not 0, or Random(0, 1) ("HordeContain.cpp" line 0x1FD8) is at most BackUpPercentage (+ 0x1EC); a record
//     draws the distance Random(BackUpMinDistance, BackUpMaxDistance) (+ 0x1E4 / + 0x1E8, line 0x1FDD) then the delay RandomInt(BackUpMinDelayTime, BackUpMaxDelayTime)
//     (+ 0x1DC / + 0x1E0, line 0x1FDE) and keeps member + normalize(member - scarer) * distance * 10 (RW 0xBD83D8) in the map + 0x1A0 (by member id: an older record is
//     replaced); the scarer becomes + 0x2B0 and the update wakes next frame (RW 0x850C32);
//   * slots 0x1A0 / 0x1A4 (RW 0x872B10 / 0x872B1D): the "cowering" byte + 0x2AC; 0x1D0 (RW 0x78856D): dirty;
//   * slot 0x1A8 RW 0x876DA5 (QUARREL, at least two members): the member nearest the members' centre (1-based index; the first when none is within 100) is fighter B
//     (+ 0x1B0), RandomInt(1, count - 1) (line 0x204F) further along the list (wrapping) is fighter A (+ 0x1AC); every other member draws its distance from B,
//     (int) Random(min, max) (line 0x206A, _ftol2), into the map + 0x24C; the spectator (+ 0x200) and fighter (+ 0x1B4) model conditions are kept; a missing member
//     ends the quarrel at once; slot 0x1AC RW 0x870B91: the spectator conditions leave every member, the map is cleared, the fighter conditions leave A and B, A is 0;
//   * slots 0x210 / 0x214 (RW 0x86C1C6 / 0x86C1E5): the face point + 0x2B8 and its flag + 0x2C4 (the set also marks the horde dirty);
//   * slot 0x90 RW 0x86EE52 (attacked within `frames`): false before frame `frames`; the horde's own record (+ 0x298 / + 0x29C) answers while it is recent, else any
//     member damaged within 4 seconds (RW 0x68C933 on the member: its body's last damage frame, the damager).
// The member pass (RW 0x873FE8, not for an IsPorcupineFormation horde): while the quarrel map is not empty, a member showing the model condition SELECTED (Object + 0x120
// bit 10) or a missing fighter ends the quarrel and the pass; with the squared distance between the fighters (x87, stored as a float) below 410 (RW 0xC5B698) B shows
// the fighter conditions and faces A, A shows them and walks to 20 from B (RW 0xBDBC6C) facing B (A walks there in any case), a spectator shows the spectator
// conditions and stands its drawn distance from B facing B, stepping 4 aside (the side away from its nearest neighbour, RW 0x870C29) when that neighbour is closer than
// 55 squared (RW 0xC5B694). Else, with back-up records: a recorded member without a scarer (or with CowerRadius 0) waits out its delay (one frame per pass, the member
// gets no order), then walks to its record; with a scarer and a CowerRadius, a member within 0.9 CowerRadius (RW 0xBDE1A0) steps 5 (RW 0xBDAE58) away from it, one within
// CowerRadius stands, one beyond walks to its slot; every member faces the scarer when there is one. Else a horde with a face point turns every member to it.
// The update (RW 0x872EFC, base register module + 0x10) drops the back-up records and the scarer and marks the horde dirty while its AI moves or it is melee-engaged
// (RW 0x872FED .. 0x873002 -> 0x873155: the map clear RW 0x86DD36): the members stay at their back-up points after the cowering until the horde moves. It runs the
// member pass while the horde is dirty, has back-up records or a fighter B (RW 0x8730CD .. 0x8730E4); with B but no A (the quarrel ended) B and the distance map are
// cleared first (RW 0x8730F7 .. 0x873107).
//
// INFERENCE / NOT PORTED (stop S-1028): the horde's own "attacked" record (+ 0x298 / + 0x29C) has no identified writer (never set); the members of the map + 0x170 (skipped by the pass, counted by
// the nearest-neighbour search) are not ported; the clear of RW 0x686D65 at the quarrel's begin is taken to be the distance map's.

#include "Common/GameCommon.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

namespace
{
const char *const kHordeFile = "HordeContain.cpp"; // RW 0xC5B0C8 (the path ends with this name)
const float kQuarrelNear = 410.0f;  // RW 0xC5B698
const float kNeighbourNear = 55.0f; // RW 0xC5B694
const float kFighterGap = 20.0f;    // RW 0xBDBC6C
const float kBackUpScale = 10.0f;   // RW 0xBD83D8
const float kCowerStep = 5.0f;      // RW 0xBDAE58

const char *const kStopHordeEmotion =
	"[S-1028] horde emotions: the back-up records (RW 0x878905), cowering, the quarrel (RW 0x876DA5 / 0x870B91), the face point and the member pass branches (RW 0x873FE8) "
	"are ported (the records end when the horde moves or melees, RW 0x873155); INFERENCE: the horde's own attacked record "
	"(+ 0x298) is never set, the pass's skipped-member map + 0x170 is not ported, RW 0x686D65 at the quarrel's begin is the distance map's clear";

void normalize3(float &x, float &y, float &z)
{
	// RW 0x403175
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(x, y, z)));
	if (len != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, len);
		x = SimMath::mulf32(x, inv);
		y = SimMath::mulf32(y, inv);
		z = SimMath::mulf32(z, inv);
	}
}

// RW 0x66137C: the squared 2D distance in the x87 register (PC24), as the pass stores it (fstp dword)
float distSq2DPC24(const Coord3D &a, const Coord3D &b)
{
	const float dx = SimMath::pc24Sub(a.x, b.x), dy = SimMath::pc24Sub(a.y, b.y);
	return SimMath::pc24Add(SimMath::pc24Mul(dx, dx), SimMath::pc24Mul(dy, dy));
}

// `relAngle(obj, p) + obj's angle` (fadd dword; fstp dword)
float facing(const Object &obj, const Coord3D &p)
{
	return SimMath::pc24Add(emotionRelAngle(obj, p), obj.getOrientation());
}

int statusBit(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}

void setFlags(Object &o, const HordeContainInterface::ConditionFlags &f)
{
	o.clearAndSetModelConditionFlags(Object::ModelConditionBits{}, f); // RW 0x5E3BA5
}

void clearFlags(Object &o, const HordeContainInterface::ConditionFlags &f)
{
	o.clearAndSetModelConditionFlags(f, Object::ModelConditionBits{}); // RW 0x5E3B79
}
} // namespace

std::vector<std::string> HordeContain::emotionStops()
{
	return { kStopHordeEmotion };
}

void HordeContain::setMembersBusy()
{
	// RW 0x876D4E
	if (m_meleeEngaged)
	{
		setMeleeEngaged(false); // slot 0x138 (RW 0x86C0F9)
	}
	const std::vector<Object *> members(m_members.begin(), m_members.end());
	for (Object *m : members)
	{
		if (AIUpdateInterface *ai = m ? m->getAIUpdateInterface() : nullptr)
		{
			ai->aiBusy(CMD_FROM_AI);
		}
	}
}

void HordeContain::recordBackUp(Object *scarer)
{
	// RW 0x878905
	if (!scarer || !m_data)
	{
		return;
	}
	Object *horde = getObject();
	GameLogic &logic = horde->logic();
	logic.noteStop(kStopHordeEmotion);
	const HordeContainModuleData &d = m_data->horde;
	static const int kScared = statusBit("UNCONTROLLABLY_SCARED");
	const bool hordeScared = kScared >= 0 && horde->testStatus((unsigned)kScared);
	const Coord3D s = *scarer->getPosition();
	for (Object *m : m_members)
	{
		if (!m)
		{
			continue;
		}
		if (!(hordeScared || (kScared >= 0 && m->testStatus((unsigned)kScared)) || d.m_cowerRadius != 0.0f ||
		      !(logic.random().getValueReal(0.0f, 1.0f, kHordeFile, 0x1FD8) > d.m_backupPercentage)))
		{
			continue;
		}
		const float distance = logic.random().getValueReal(d.m_backUpMinDistance, d.m_backUpMaxDistance, kHordeFile, 0x1FDD);
		const int delay = logic.random().getValue((int)d.m_backUpMinDelayTime, (int)d.m_backUpMaxDelayTime, kHordeFile, 0x1FDE);
		BackUpEntry &e = m_backUp[m->getID()];
		const Coord3D p = *m->getPosition();
		float dx = SimMath::subf32(p.x, s.x), dy = SimMath::subf32(p.y, s.y), dz = SimMath::subf32(p.z, s.z);
		normalize3(dx, dy, dz);
		const float k = SimMath::mulf32(distance, kBackUpScale);
		e.position.x = SimMath::addf32(p.x, SimMath::mulf32(k, dx));
		e.position.y = SimMath::addf32(p.y, SimMath::mulf32(dy, k));
		e.position.z = SimMath::addf32(p.z, SimMath::mulf32(dz, k));
		e.delay = (unsigned)delay;
	}
	m_scarer = scarer->getID();
	setWakeFrame(horde, UPDATE_SLEEP(1)); // RW 0x850C32(object, 1)
}

void HordeContain::setCowering(bool on)
{
	// RW 0x872B10
	m_cowering = on;
}

void HordeContain::setFacePoint(const Coord3D &p)
{
	// RW 0x86C1C6
	m_facePoint = p;
	m_hasFacePoint = true;
	m_dirty = true;
}

void HordeContain::beginQuarrel(float minDistance, float maxDistance, const ConditionFlags &spectators, const ConditionFlags &fighters)
{
	// RW 0x876DA5
	m_quarrelDistance.clear(); // RW 0x686D65 (inference)
	const std::vector<Object *> members(m_members.begin(), m_members.end());
	const int count = (int)members.size();
	if (count < 2)
	{
		return;
	}
	float sx = 0.0f, sy = 0.0f;
	for (Object *m : members)
	{
		if (!m)
		{
			endQuarrel();
			return;
		}
		sx = SimMath::addf32(m->getPosition()->x, sx);
		sy = SimMath::addf32(m->getPosition()->y, sy);
	}
	const float inv = SimMath::divf32(1.0f, (float)count);
	const float cx = SimMath::mulf32(inv, sx), cy = SimMath::mulf32(sy, inv);
	int centre = -1;
	float best = 10000.0f; // RW 0xBDE8B8
	for (int i = 0; i < count; ++i)
	{
		const float dx = SimMath::subf32(members[(size_t)i]->getPosition()->x, cx), dy = SimMath::subf32(members[(size_t)i]->getPosition()->y, cy);
		const float d = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
		if (d < best)
		{
			best = d;
			centre = i + 1;
		}
	}
	if (centre == -1)
	{
		centre = 1;
	}
	GameLogic &logic = getObject()->logic();
	logic.noteStop(kStopHordeEmotion);
	int fighterA = logic.random().getValue(1, count - 1, kHordeFile, 0x204F) + centre;
	if (count < fighterA)
	{
		fighterA -= count;
	}
	m_quarrelFighterFlags = fighters;
	m_quarrelSpectatorFlags = spectators;
	for (int i = 1; i <= count; ++i)
	{
		Object *m = members[(size_t)i - 1];
		if (i == fighterA)
		{
			m_quarrelA = m->getID();
		}
		else if (i == centre)
		{
			m_quarrelB = m->getID();
		}
		else
		{
			const float r = logic.random().getValueReal(minDistance, maxDistance, kHordeFile, 0x206A);
			m_quarrelDistance[m->getID()] = (int)SimMath::ftol2Low32((double)r);
		}
	}
	setWakeFrame(getObject(), UPDATE_SLEEP(1));
}

void HordeContain::endQuarrel()
{
	// RW 0x870B91
	for (Object *m : m_members)
	{
		if (m)
		{
			clearFlags(*m, m_quarrelSpectatorFlags);
		}
	}
	m_quarrelDistance.clear();
	GameLogic &logic = getObject()->logic();
	Object *a = logic.findObjectByID(m_quarrelA);
	Object *b = logic.findObjectByID(m_quarrelB);
	if (a)
	{
		clearFlags(*a, m_quarrelFighterFlags);
	}
	if (b)
	{
		clearFlags(*b, m_quarrelFighterFlags);
	}
	m_quarrelA = INVALID_ID;
}

bool HordeContain::attackedWithin(unsigned frames, ObjectID &attacker) const
{
	// RW 0x86EE52
	attacker = INVALID_ID;
	if (!(frames < getObject()->logic().getFrame()))
	{
		return false;
	}
	for (Object *m : m_members)
	{
		if (m && emotionAttackedWithin(*m, 4, attacker))
		{
			return true;
		}
	}
	return false;
}

// RW 0x870C29: the member (or a member of the map + 0x170, not ported) nearest to `member` within 1000 (squared 1e6)
static Object *nearestOtherMember(const HordeContain::ContainedItemsList &members, const Object &member)
{
	Object *best = nullptr;
	float bestD = 1000000.0f; // RW 0xBDCDC0
	for (Object *o : members)
	{
		if (!o || o == &member)
		{
			continue;
		}
		const float dx = SimMath::subf32(o->getPosition()->x, member.getPosition()->x), dy = SimMath::subf32(o->getPosition()->y, member.getPosition()->y);
		const float d = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
		if (d < bestD)
		{
			best = o;
			bestD = d;
		}
	}
	return best;
}

int HordeContain::emotionMemberOrder(Object &member, Object *scarer, Coord3D &pos, float &angle)
{
	// RW 0x874155 .. 0x874671
	GameLogic &logic = getObject()->logic();
	if (!m_quarrelDistance.empty())
	{
		static const int kSelected = AIUpdateInterface::modelConditionBit("SELECTED");
		Object *b = logic.findObjectByID(m_quarrelB);
		Object *a = logic.findObjectByID(m_quarrelA);
		if (member.testModelCondition(kSelected) || !a || !b)
		{
			endQuarrel(); // slot 0x1AC, the pass ends
			return 2;
		}
		const float d = distSq2DPC24(*b->getPosition(), *a->getPosition());
		const bool near = kQuarrelNear > d;
		if (&member == b)
		{
			if (near)
			{
				setFlags(member, m_quarrelFighterFlags);
				angle = facing(member, *a->getPosition());
			}
			return 0;
		}
		const Coord3D bp = *b->getPosition();
		if (&member == a)
		{
			if (near)
			{
				setFlags(member, m_quarrelFighterFlags);
			}
			float dx = SimMath::subf32(member.getPosition()->x, bp.x), dy = SimMath::subf32(member.getPosition()->y, bp.y),
			      dz = SimMath::subf32(member.getPosition()->z, bp.z);
			normalize3(dx, dy, dz);
			pos.x = SimMath::addf32(bp.x, SimMath::mulf32(dx, kFighterGap));
			pos.y = SimMath::addf32(bp.y, SimMath::mulf32(dy, kFighterGap));
			pos.z = SimMath::addf32(bp.z, SimMath::mulf32(dz, kFighterGap));
			angle = facing(member, bp);
			return 0;
		}
		if (!near)
		{
			return 0;
		}
		const int dist = m_quarrelDistance[member.getID()]; // RW 0x7871FC: operator[]
		setFlags(member, m_quarrelSpectatorFlags);
		angle = facing(member, bp);
		const Coord3D mp = *member.getPosition();
		float dx = SimMath::subf32(mp.x, bp.x), dy = SimMath::subf32(mp.y, bp.y), dz = SimMath::subf32(mp.z, bp.z);
		normalize3(dx, dy, dz);
		const float k = (float)dist; // cvtsi2ss
		pos.x = SimMath::addf32(bp.x, SimMath::mulf32(k, dx));
		pos.y = SimMath::addf32(bp.y, SimMath::mulf32(dy, k));
		pos.z = SimMath::addf32(bp.z, SimMath::mulf32(dz, k));
		Object *n = nearestOtherMember(m_members, member);
		if (n && kNeighbourNear > distSq2DPC24(mp, *n->getPosition()))
		{
			// RW 0x8743D2: v = normalize((0, 0, 1) x (member - B)) with the zero products kept, the side away from the neighbour
			const float ex = SimMath::subf32(mp.x, bp.x), ey = SimMath::subf32(mp.y, bp.y), ez = SimMath::subf32(mp.z, bp.z);
			const float ez0 = SimMath::mulf32(ez, 0.0f);
			float vx = SimMath::subf32(ez0, ey);
			float vz = SimMath::subf32(SimMath::mulf32(ey, 0.0f), SimMath::mulf32(ex, 0.0f));
			float vy = SimMath::subf32(ex, ez0);
			const Coord3D np = *n->getPosition();
			const float nx = SimMath::subf32(np.x, mp.x), ny = SimMath::subf32(np.y, mp.y), nz = SimMath::subf32(np.z, mp.z);
			normalize3(vx, vy, vz);
			const float dot = SimMath::addf32(SimMath::addf32(SimMath::mulf32(vx, nx), SimMath::mulf32(nz, vz)), SimMath::mulf32(ny, vy));
			const float side = dot > 0.0f ? -4.0f : 4.0f;
			pos.x = SimMath::addf32(SimMath::mulf32(side, vx), pos.x);
			pos.y = SimMath::addf32(SimMath::mulf32(vy, side), pos.y);
			pos.z = SimMath::addf32(SimMath::mulf32(vz, side), pos.z);
		}
		return 0;
	}
	if (!m_backUp.empty())
	{
		auto e = m_backUp.find(member.getID());
		if (e != m_backUp.end())
		{
			const float cower = m_data->horde.m_cowerRadius;
			if (!scarer || cower == 0.0f)
			{
				if (e->second.delay != 0)
				{
					--e->second.delay;
					return 1;
				}
				pos = e->second.position;
			}
			else
			{
				const Coord3D mp = *member.getPosition(), sp = *scarer->getPosition();
				float dx = SimMath::subf32(mp.x, sp.x), dy = SimMath::subf32(mp.y, sp.y), dz = SimMath::subf32(mp.z, sp.z);
				const float d = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, dz))); // RW 0x403111
				if (SimMath::pc24MulD((double)cower, 0.9) > d)
				{
					normalize3(dx, dy, dz);
					pos.x = SimMath::addf32(SimMath::mulf32(dx, kCowerStep), mp.x);
					pos.y = SimMath::addf32(mp.y, SimMath::mulf32(dy, kCowerStep));
					pos.z = SimMath::addf32(mp.z, SimMath::mulf32(dz, kCowerStep));
				}
				else if (cower > d)
				{
					pos = mp; // stands
				}
			}
		}
		if (scarer)
		{
			angle = facing(member, *scarer->getPosition());
		}
		return 0;
	}
	if (m_hasFacePoint)
	{
		angle = facing(member, m_facePoint);
	}
	return 0;
}
