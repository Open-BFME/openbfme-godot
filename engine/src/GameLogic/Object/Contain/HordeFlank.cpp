// OpenBFME. GPL-3.0.
// Lane HORDE-2: flanking. See HordeFlank.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * HordeContain::update RW 0x872F05-0x872F5D, first of all: with FlankedDelay (module data + 0x268) n > 0, the horde object's angle is pushed onto the vector H+0x2CC while it
//     holds fewer than n entries, else it overwrites entry H+0x2D8, which then advances and wraps to 0 at n;
//   * Object::isFlankedBy RW 0x68FB63: victim and attacker resolve to their HORDE (KindOf HORDE, template + 0x114 bit 0x2000, else the container when that is a HORDE, else false);
//     the victim horde's HordeContainInterface (contain vslot 0x7C) slot 0x24C decides;
//   * slot 0x24C RW 0x876FC4 (this = the victim horde H):
//       1. FrontAngle (data + 0x264, degrees) >= 360.0 (RW 0xBDC1F4): never flanked;
//       2. the attacker must be KindOf HORDE with a HordeContainInterface whose slot 0x254 (RW 0x872B09, byte H+0x2E8) is set;
//       3. the map H+0x2DC (attacker id -> frame) answers when it has the attacker: flanked while frame < the stored frame;
//       4. else c = (float)cos((double)(FrontAngle * 0.0087250005f)) (x87 under PC24, MSVCR71 cos RW 0xA3CF84); d = the normalised planar direction victim -> attacker
//          (RW 0x405553); f = the victim's unit direction (RW 0x70B9E0); flanked when c > f.y * d.y + f.z * d.z + f.x * d.x (SSE), or when for any angle a of the history
//          (vector order) c > cos(a) * d.x + sin(a) * d.y (x87: fsin, fmul, fstp dword; fcos, fmul, fadd);
//       5. flanked: map[attacker] = frame + FlankedDuration (data + 0x26C), H+0x2E8 = 0 (a flanked horde can no longer flank), true; else map[attacker] = 0, false.
//     The map is never cleared (its other users are the constructor, the destructor and xfer): a horde is judged once per attacker horde, and an expired flank
//     stays expired. The byte H+0x2E8 is written only by the constructor (1) and by step 5.
// INFERENCE (stop S-582): cos / sin are SimMath's deterministic functions (the CRT's / x87's bits are stop S-167); the victim's facing vector is computed from the angle
// (RW caches it per transform).

#include "GameLogic/Object/Contain/HordeFlank.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

namespace
{
const char *const kStop =
	"[S-582] flanking: the orientation history of HordeContain::update (RW 0x872F05), Object::isFlankedBy (RW 0x68FB63) and the horde test (RW 0x876FC4: FrontAngle, the history, "
	"the per-attacker map that is never cleared, the can-flank byte) feed the nugget's FlankingBonus / FlankedScalar, the armour's FlankedPenalty and the crush revenge weapon; "
	"INFERENCE: cos / sin are the deterministic SimMath functions (S-167), the victim's facing is computed from its angle; the weapon's FireFlankFX is the client's";

bool isHordeKind(const Object &o)
{
	return o.isKindOf((unsigned)CombatNames::kinds().horde);
}

// the HORDE an object resolves to (RW 0x68FB71..0x68FBB5)
Object *resolveHorde(Object &o)
{
	if (isHordeKind(o))
	{
		return &o;
	}
	Object *c = o.getContainedBy();
	return c && isHordeKind(*c) ? c : nullptr;
}

HordeContain *hordeContainOf(Object &o)
{
	ContainModuleInterface *c = o.getContain();
	return c && c->getHordeContainInterface() ? dynamic_cast<HordeContain *>(c) : nullptr;
}
} // namespace

const char *HordeFlank::stopLine()
{
	return kStop;
}

void HordeContain::recordFlankHistory()
{
	const unsigned n = m_data->horde.m_flankedDelay;
	if ((int)n <= 0)
	{
		return; // `test esi, esi; jle`
	}
	const float angle = getObject()->getOrientation();
	if (m_flankAngles.size() < n)
	{
		m_flankAngles.push_back(angle);
		return;
	}
	m_flankAngles[m_flankIndex] = angle;
	++m_flankIndex;
	if ((int)m_flankIndex >= (int)n)
	{
		m_flankIndex = 0;
	}
}

// RW 0x876FC4
bool HordeContain::isFlankedByHorde(Object &attacker)
{
	const HordeContainModuleData &data = m_data->horde;
	if (data.m_frontAngle >= 360.0f)
	{
		return false;
	}
	if (!isHordeKind(attacker))
	{
		return false;
	}
	HordeContain *other = hordeContainOf(attacker);
	if (!other || !other->canFlank())
	{
		return false;
	}
	Object *self = getObject();
	const unsigned frame = self->logic().getFrame();
	auto it = m_flankers.find(attacker.getID());
	if (it != m_flankers.end())
	{
		return frame < it->second;
	}
	const float halfAngle = NumericState::pc24Mul(data.m_frontAngle, 0.0087250005f); // RW 0xC11F90
	const float c = (float)SimMath::cosDet(halfAngle);                               // MSVCR71 cos on the double, fstp dword
	float dx = SimMath::subf32(attacker.getPosition()->x, self->getPosition()->x);
	float dy = SimMath::subf32(attacker.getPosition()->y, self->getPosition()->y);
	const float len = (float)SimMath::length3d(dx, dy, 0.0f); // RW 0x405553: the length, then each component divided when it is not 0
	if (len != 0.0f)
	{
		dx = SimMath::divf32(dx, len);
		dy = SimMath::divf32(dy, len);
	}
	const float a = self->getOrientation();
	const float fx = SimMath::cosDet(a), fy = SimMath::sinDet(a);
	const float dot = SimMath::addf32(SimMath::addf32(SimMath::mulf32(fy, dy), SimMath::mulf32(0.0f, 0.0f)), SimMath::mulf32(fx, dx));
	bool flanked = c > dot;
	for (size_t i = 0; !flanked && i < m_flankAngles.size(); ++i)
	{
		double s, co;
		SimMath::sinCosDet((double)m_flankAngles[i], s, co);
		const float t = NumericState::pc24MulD(s, (double)dy);                      // fsin; fmul dword d.y; fstp dword
		const float v = NumericState::pc24AddD((double)NumericState::pc24MulD(co, (double)dx), (double)t); // fcos; fmul d.x; fadd t (PC24)
		flanked = c > v;
	}
	if (flanked)
	{
		m_flankers[attacker.getID()] = frame + data.m_flankedDuration;
		m_canFlank = false;
		return true;
	}
	m_flankers[attacker.getID()] = 0;
	return false;
}

// RW 0x68FB63
bool HordeFlank::isFlankedBy(Object &victim, Object &attacker)
{
	CombatState::Counters &k = victim.logic().combat().counters();
	++k.flankTests;
	Object *v = resolveHorde(victim);
	Object *a = resolveHorde(attacker);
	if (!v || !a)
	{
		return false;
	}
	HordeContain *hc = hordeContainOf(*v);
	if (!hc)
	{
		return false;
	}
	const bool flanked = hc->isFlankedByHorde(*a);
	if (flanked)
	{
		++k.flanks;
	}
	return flanked;
}

const char *HordeContain::reformStopLine()
{
	return "[S-584] re-forming: the RotWK member pass (RW 0x873FE8) orders the members with force = !isMoving(horde), so a parked horde lets no member wait ahead of its slot "
	       "and the survivors of a melee walk back; the B1 hub block 3 is not in the RotWK hub (RW 0x87468B); NOT READ: the hub's melee branch (MeleeBehavior slots 7 .. 11) and the "
	       "pass's porcupine / H+0x250 branches: the formation stays frozen while engaged (COMBAT-1)";
}
