// OpenBFME. GPL-3.0.
// BFME1 1.03 horde emotion variant: backup 0x00246490, quarrel
// 0x00244680 / 0x002396F0, member pass 0x00241050, nearest 0x00235520.
// RW keeps HordeEmotion.cpp (0x878905 / 0x876DA5 / 0x873FE8).
#include "GameLogic/Object/Contain/Bfme1HordeRuntime.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include <algorithm>

namespace {
const char *const kHordeFile="HordeContain.cpp";
constexpr float kQuarrelNear=410.0f,kNeighbourNear=55.0f,kFighterGap=20.0f,kCowerStep=5.0f;
double distSq2DPC24(const Coord3D &a,const Coord3D &b)
{
 const double x=SimMath::pc24SubW(a.x,b.x),y=SimMath::pc24SubW(a.y,b.y);
 return SimMath::pc24AddW(SimMath::pc24MulW(x,x),SimMath::pc24MulW(y,y));
}
void normalize3(float &x,float &y,float &z)
{
 // Native Coord3D 0x000FB930 keeps its root and reciprocal wide.
 const auto len=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(x,x),SimMath::pc24MulW(y,y)),SimMath::pc24MulW(z,z)));
 if(len!=0){const auto inv=SimMath::pc24DivW(1,len);x=SimMath::fstpDword(SimMath::pc24MulW(x,inv));y=SimMath::fstpDword(SimMath::pc24MulW(y,inv));z=SimMath::fstpDword(SimMath::pc24MulW(z,inv));}
}
float facing(const Object &o,const Coord3D &p)
{
 // Thing 0x00150510. Only the normalized vector and dot are stored;
 // x87 PC24 intermediates retain the wide exponent. CRT parity is S-167.
 const double x=SimMath::pc24SubW(p.x,o.getPosition()->x),y=SimMath::pc24SubW(p.y,o.getPosition()->y);
 const auto len=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24MulW(x,x),SimMath::pc24MulW(y,y)));
 if(len==0)return o.getOrientation();
 const auto inv=SimMath::pc24DivW(1,len);const float nx=SimMath::fstpDword(SimMath::pc24MulW(x,inv)),ny=SimMath::fstpDword(SimMath::pc24MulW(y,inv));
 const float co=SimMath::cosf32(o.getOrientation()),si=SimMath::sinf32(o.getOrientation());
 const float dot=std::clamp(SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(nx,co),SimMath::pc24MulW(ny,si))),-1.0f,1.0f);
 float a=SimMath::fstpDword(SimMath::acosDet(dot));if(SimMath::pc24SubW(SimMath::pc24MulW(ny,co),SimMath::pc24MulW(nx,si))<0)a=SimMath::pc24Sub(0,a);
 return SimMath::pc24Add(a,o.getOrientation());
}
void setFlags(Object &o,const HordeContainInterface::ConditionFlags &flags){o.clearAndSetModelConditionFlags({},flags);}
static Object *nearestOtherMember(const HordeContain::ContainedItemsList &members, const Object &member)
{
	Object *best = nullptr;
	float bestD = 1000000.0f; // BFME1 0x235520
	for (Object *o : members)
	{
		if (!o || o == &member)
		{
			continue;
		}
		const auto dx=SimMath::pc24SubW(o->getPosition()->x,member.getPosition()->x),dy=SimMath::pc24SubW(o->getPosition()->y,member.getPosition()->y);
		const auto d=SimMath::pc24AddW(SimMath::pc24MulW(dy,dy),SimMath::pc24MulW(dx,dx));
		if (d < bestD)
		{
			best = o;
			bestD = SimMath::fstpDword(d);
		}
	}
	return best;
}
}
void Bfme1HordeContain::recordBackUp(Object *scarer)
{
 if(!scarer)return;
 const auto &d=hordeData();const auto source=*scarer->getPosition();
 const auto scared=(unsigned)CombatNames::status("UNCONTROLLABLY_SCARED");
 for(Object *m:*getContainedItemsList())
 {
  if(!getObject()->testStatus(scared) && !m->testStatus(scared) && d.m_cowerRadius==0 && getObject()->logic().random().getValueReal(0,1,kHordeFile,0x1ABD)>d.m_backupPercentage)continue;
  const float distance=getObject()->logic().random().getValueReal(d.m_backUpMinDistance,d.m_backUpMaxDistance,kHordeFile,0x1AC2);
  const int delay=getObject()->logic().random().getValue((int)d.m_backUpMinDelayTime,(int)d.m_backUpMaxDelayTime,kHordeFile,0x1AC3);
  const auto &p=*m->getPosition();double x=SimMath::pc24SubW(p.x,source.x),y=SimMath::pc24SubW(p.y,source.y);const auto originalZ=SimMath::pc24SubW(p.z,source.z);float z=SimMath::fstpDword(originalZ);
  const auto len=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(originalZ,z),SimMath::pc24MulW(y,y)),SimMath::pc24MulW(x,x)));
  if(len!=0){const auto inv=SimMath::pc24DivW(1,len);x=SimMath::pc24MulW(x,inv);y=SimMath::pc24MulW(y,inv);z=SimMath::fstpDword(SimMath::pc24MulW(z,inv));}
  const auto wideScale=SimMath::pc24MulW(distance,10);const float scale=SimMath::fstpDword(wideScale);const float offsetX=SimMath::fstpDword(SimMath::pc24MulW(wideScale,x));auto &e=m_backUp[m->getID()];e.delay=(unsigned)delay;
  e.position={SimMath::fstpDword(SimMath::pc24AddW(p.x,offsetX)),SimMath::fstpDword(SimMath::pc24AddW(p.y,SimMath::pc24MulW(scale,y))),SimMath::fstpDword(SimMath::pc24AddW(p.z,SimMath::pc24MulW(scale,z)))};
 }
 m_scarer=scarer->getID();setWakeFrame(getObject(),UPDATE_SLEEP(1));
}

void Bfme1HordeContain::beginQuarrel(float minDistance, float maxDistance, const ConditionFlags &spectators, const ConditionFlags &fighters)
{
	// BFME1 0x00244680; native parameters are integers.
	m_quarrelDistance.clear(); // Native 0x0023AB70 map clear.
	const std::vector<Object *> members(m_members.begin(), m_members.end());
	const int count = (int)members.size();
	if (count < 2)
	{
		return;
	}
	double sx = 0.0, sy = 0.0;
	for (Object *m : members)
	{
		if (!m)
		{
			endQuarrel();
			return;
		}
		sx = SimMath::pc24AddW(m->getPosition()->x, sx);
		sy = SimMath::pc24AddW(m->getPosition()->y, sy);
	}
	const auto wideInv=SimMath::pc24DivW(1,count);const float inv=SimMath::fstpDword(wideInv);
	const float cx=SimMath::fstpDword(SimMath::pc24MulW(wideInv,sx)),cy=SimMath::pc24Mul(SimMath::fstpDword(sy),inv);
	int centre = -1;
	float best = 10000.0f; // BFME1 0x244680
	for (int i = 0; i < count; ++i)
	{
		const float dx=SimMath::pc24Sub(members[(size_t)i]->getPosition()->x,cx);const auto dy=SimMath::pc24SubW(members[(size_t)i]->getPosition()->y,cy);
		const auto d=SimMath::pc24AddW(SimMath::pc24MulW(dy,dy),SimMath::pc24MulW(dx,dx));
		if (d < best)
		{
			best = SimMath::fstpDword(d);
			centre = i + 1;
		}
	}
	if (centre == -1)
	{
		centre = 1;
	}
	GameLogic &logic = getObject()->logic();

	int fighterA = logic.random().getValue(1, count - 1, kHordeFile, 0x1B34) + centre;
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
			const float r = logic.random().getValueReal((float)SimMath::ftol2Low32(minDistance), (float)SimMath::ftol2Low32(maxDistance), kHordeFile, 0x1B4F);
			m_quarrelDistance[m->getID()] = (int)SimMath::ftol2Low32((double)r);
		}
	}
	setWakeFrame(getObject(), UPDATE_SLEEP(1));
}
int Bfme1HordeContain::emotionMemberOrder(Object &member, Object *scarer, Coord3D &pos, float &angle)
{
	// BFME1 0x00241050, also for porcupine formations.
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
		const auto d = distSq2DPC24(*b->getPosition(), *a->getPosition());
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
			float dx = SimMath::pc24Sub(member.getPosition()->x, bp.x), dy = SimMath::pc24Sub(member.getPosition()->y, bp.y),
			      dz = SimMath::pc24Sub(member.getPosition()->z, bp.z);
			normalize3(dx, dy, dz);
			pos.x = SimMath::pc24Add(bp.x, SimMath::pc24Mul(dx, kFighterGap));
			pos.y = SimMath::pc24Add(bp.y, SimMath::pc24Mul(dy, kFighterGap));
			pos.z = SimMath::pc24Add(bp.z, SimMath::pc24Mul(dz, kFighterGap));
			angle = facing(member, bp);
			return 0;
		}
		if (!near)
		{
			return 0;
		}
		const int dist = m_quarrelDistance[member.getID()]; // BFME1 0x00226FA0: operator[]
		setFlags(member, m_quarrelSpectatorFlags);
		angle = facing(member, bp);
		const Coord3D mp = *member.getPosition();
		float dx = SimMath::pc24Sub(mp.x, bp.x), dy = SimMath::pc24Sub(mp.y, bp.y), dz = SimMath::pc24Sub(mp.z, bp.z);
		normalize3(dx, dy, dz);
		const float k = (float)dist; // native FILD, stored float
		pos.x = SimMath::pc24Add(bp.x, SimMath::pc24Mul(k, dx));
		pos.y = SimMath::pc24Add(bp.y, SimMath::pc24Mul(dy, k));
		pos.z = SimMath::pc24Add(bp.z, SimMath::pc24Mul(dz, k));
		Object *n = nearestOtherMember(m_members, member);
		if (n && kNeighbourNear > distSq2DPC24(mp, *n->getPosition()))
		{
			// BFME1 0x241050: v = normalize((0, 0, 1) x (member - B)) with the zero products kept, the side away from the neighbour
			const float ex = SimMath::pc24Sub(mp.x, bp.x), ey = SimMath::pc24Sub(mp.y, bp.y), ez = SimMath::pc24Sub(mp.z, bp.z);
			const float ez0 = SimMath::pc24Mul(ez, 0.0f);
			float vx = SimMath::pc24Sub(ez0, ey);
			float vz = SimMath::pc24Sub(SimMath::pc24Mul(ey, 0.0f), SimMath::pc24Mul(ex, 0.0f));
			float vy = SimMath::pc24Sub(ex, ez0);
			const Coord3D np = *n->getPosition();
			const float nx = SimMath::pc24Sub(np.x, mp.x), ny = SimMath::pc24Sub(np.y, mp.y), nz = SimMath::pc24Sub(np.z, mp.z);
			normalize3(vx, vy, vz);
			const float dot = SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24Mul(vx, nx), SimMath::pc24Mul(nz, vz)), SimMath::pc24Mul(ny, vy));
			const float side = dot > 0.0f ? -4.0f : 4.0f;
			pos.x = SimMath::pc24Add(SimMath::pc24Mul(side, vx), pos.x);
			pos.y = SimMath::pc24Add(SimMath::pc24Mul(vy, side), pos.y);
			pos.z = SimMath::pc24Add(SimMath::pc24Mul(vz, side), pos.z);
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
				float dx = SimMath::pc24Sub(mp.x, sp.x), dy = SimMath::pc24Sub(mp.y, sp.y), dz = SimMath::pc24Sub(mp.z, sp.z);
				const auto d=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dx,dx),SimMath::pc24MulW(dy,dy)),SimMath::pc24MulW(dz,dz))); // 0xB6CA0 returns wide.
				if (SimMath::pc24MulD((double)cower, 0.9) > d)
				{
					normalize3(dx, dy, dz);
					pos.x = SimMath::pc24Add(SimMath::pc24Mul(dx, kCowerStep), mp.x);
					pos.y = SimMath::pc24Add(mp.y, SimMath::pc24Mul(dy, kCowerStep));
					pos.z = SimMath::pc24Add(mp.z, SimMath::pc24Mul(dz, kCowerStep));
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
bool Bfme1HordeContain::participatesInMemberPass(const Object &m) const
{
 return !m_garrisonEntering.count(m.getID()); // BFME1 primary +0x114, 0x002410CB.
}

void Bfme1HordeContain::setMembersBusy()
{
 // BFME1 0x00244080, called by emotion entry 0x0026FBE0: command
 // source is 1 (SCRIPT), unlike RW 0x876D4E's AI source 2.
 if(m_meleeEngaged)setMeleeEngaged(false);
 else if(m_frozen)endNativeMelee();
 for(Object *m:*getContainedItemsList())if(auto *a=m->getAIUpdateInterface())a->aiBusy(CMD_FROM_SCRIPT);
}

Object *Bfme1HordeContain::nativeAttackRepresentative(bool longestRange) const
{
 // BFME1 0x238A60: first raw member, then first registered ID;
 // MELEE_HORDE uses 0x237E50's greatest positive current range instead.
 Object *best=nullptr;float range=0;
 auto consider=[&](Object *m) {
  if(!m)return;
  if(!longestRange){if(!best)best=m;return;}
  const auto *w=m->getWeapons();if(w && w->currentWeapon()) {
   const float r=w->currentAttackRange();if(r>range){range=r;best=m;}
  }
 };
 for(Object *m:*getContainedItemsList())consider(m);
 for(auto id:m_garrisonEntering)consider(getObject()->logic().findObjectByID(id));
 return best;
}
