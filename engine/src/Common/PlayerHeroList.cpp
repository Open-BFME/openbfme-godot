// OpenBFME. GPL-3.0.
// See Common/PlayerHeroList.h.

#include "Common/PlayerHeroList.h"

#include "Common/BuildAssistant.h"
#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

const HeroRecord *PlayerHeroList::at(int index) const
{
	if (index < 0 || (size_t)index >= m_records.size())
	{
		return nullptr;
	}
	return &m_records[(size_t)index];
}

HeroRecord *PlayerHeroList::at(int index)
{
	return const_cast<HeroRecord *>(static_cast<const PlayerHeroList *>(this)->at(index));
}

const HeroRecord *PlayerHeroList::findByProductionID(std::uint32_t id) const
{
	for (const HeroRecord &r : m_records)
	{
		if (r.productionID == id)
		{
			return &r;
		}
	}
	return nullptr;
}

HeroRecord *PlayerHeroList::findByProductionID(std::uint32_t id)
{
	return const_cast<HeroRecord *>(static_cast<const PlayerHeroList *>(this)->findByProductionID(id));
}

// RW 0x781801 -> RW 0x780713: a purchase record of the template (+ 0x0C from RW 0x73CE2A, see the header; INFERENCE: 1, because no create module class
// that answers RW 0x73CE2A's vslot 0x24 is ported (S-852))
void PlayerHeroList::addPurchase(const ThingTemplate &tt)
{
	HeroRecord r;
	r.cost = BuildAssistant::buildCost(tt);                                // RW 0x780742: (u16) template + 0x5EA
	r.seconds = SimMath::truncToInt32(BuildAssistant::buildTime(tt));      // RW 0x780752: _ftol(BuildTime)
	r.rank = 1;
	r.productionID = 0;                                                    // RW 0x780780: + 0xB4 = 0
	r.templateName = tt.getName();
	m_records.push_back(r);
}

int PlayerHeroList::addRecord(const HeroRecord &r)
{
	m_records.push_back(r);
	return (int)m_records.size() - 1;
}

const ThingTemplate *PlayerHeroList::templateAt(GameLogic &logic, int index) const
{
	const HeroRecord *r = at(index);
	return r ? logic.things().findTemplate(r->templateName) : nullptr;
}

bool PlayerHeroList::isAvailable(int index) const
{
	const HeroRecord *r = at(index);
	return r && r->startFrame == -1;
}

// RW 0x780614
int PlayerHeroList::recordCost(GameLogic &logic, const Player &owner, const HeroRecord &r, const Object *producer) const
{
	float m = 1.0f;
	if (producer)
	{
		if (ProductionUpdateInterface *pu = producer->getProductionUpdate())
		{
			m = pu->heroCostMultiplier(r.dead); // slot 28, RW 0x8A0A31 (byte + 0xB0)
		}
	}
	const ThingTemplate *tt = logic.things().findTemplate(r.templateName);
	if (!tt)
	{
		return 0; // RW 0x780648: no template, 0
	}
	const int cost = BuildAssistant::calcCostToBuild(*tt, &owner, nullptr, (int)r.cost); // RW 0x780662: (player, 0, + 0xA4)
	return SimMath::truncToInt32(NumericState::pc24Mul((float)cost, m));                // fild; fmul [m]; _ftol (x87 PC24)
}

// RW 0x780687
int PlayerHeroList::recordFrames(GameLogic &logic, const Player &owner, const HeroRecord &r, const Object *producer) const
{
	float m = 1.0f;
	if (producer)
	{
		if (ProductionUpdateInterface *pu = producer->getProductionUpdate())
		{
			m = pu->heroTimeMultiplier(r.dead); // slot 29, RW 0x8A0AB0
		}
	}
	const ThingTemplate *tt = logic.things().findTemplate(r.templateName);
	if (!tt)
	{
		return 0;
	}
	const int frames = BuildAssistant::calcTimeToBuild(*tt, &owner, nullptr, r.seconds, logic.productionSettings(), logic); // RW 0x7806D5: (player, 0, + 0xAC)
	return SimMath::truncToInt32(NumericState::pc24Mul((float)frames, m));
}

// RW 0x780AD3 (the Brutal AI discount of RW 0x780AF1 .. 0x780B5C is stop S-204)
int PlayerHeroList::costAt(GameLogic &logic, const Player &owner, int index, const Object *producer) const
{
	const HeroRecord *r = at(index);
	return r ? recordCost(logic, owner, *r, producer) : 0;
}

// RW 0x780B72 (Brutal AI: S-204)
int PlayerHeroList::framesForProductionID(GameLogic &logic, const Player &owner, std::uint32_t id, const Object *producer) const
{
	const HeroRecord *r = findByProductionID(id);
	return r ? recordFrames(logic, owner, *r, producer) : 0;
}

// RW 0x780C11
int PlayerHeroList::framesAt(GameLogic &logic, const Player &owner, int index, const Object *producer) const
{
	const HeroRecord *r = at(index);
	return r ? recordFrames(logic, owner, *r, producer) : 0;
}

// RW 0x7812B2
bool PlayerHeroList::startProduction(int index, std::uint32_t id, UnsignedInt frame)
{
	if (findByProductionID(id))
	{
		return false;
	}
	HeroRecord *r = at(index);
	if (!r)
	{
		return false;
	}
	r->uiFactor = 0.998f; // RW 0x7812D4: [0xC2F508]
	if (r->startFrame != -1)
	{
		return false;
	}
	r->productionID = id;
	r->startFrame = (std::int32_t)frame;
	return true;
}

// RW 0x780C64
bool PlayerHeroList::cancelProduction(std::uint32_t id)
{
	HeroRecord *r = findByProductionID(id);
	if (!r || r->startFrame == -1)
	{
		return false;
	}
	r->startFrame = -1;
	r->productionID = 0xFFFFFFFFu;
	r->uiFactor = 1.0f;
	return true;
}

// RW 0x780C9F: fild frame (+ 2^32 when negative), fisub start, fidiv time under the game's PC24 control word. The difference of two integers below 2^24 is
// exact and the quotient rounds to a 24-bit mantissa: the IEEE single quotient (SimMath::divf32) of the exact operands
double PlayerHeroList::progressAt(GameLogic &logic, const Player &owner, int index, const Object *producer) const
{
	const HeroRecord *r = at(index);
	if (!r || r->startFrame == -1)
	{
		return 0.0; // [0xC1B594]
	}
	const int time = recordFrames(logic, owner, *r, producer);
	if (time < 1)
	{
		return 1.0;
	}
	const double now = (double)logic.getFrame(); // unsigned frame: the 2^32 correction is the same value
	const double elapsed = SimMath::subD(now, (double)r->startFrame);
	return (double)SimMath::divf32((float)elapsed, (float)time);
}

// RW 0x78131E
int PlayerHeroList::findIndex(const ThingTemplate &tt, std::uint32_t productionID, int nth) const
{
	int index = 0, seen = 0;
	for (const HeroRecord &r : m_records)
	{
		if (r.templateName == tt.getName())
		{
			const bool match = productionID == 0xFFFFFFFFu ? seen == nth : productionID == r.productionID;
			if (match)
			{
				return index;
			}
			++seen;
		}
		++index;
	}
	return -1;
}

void PlayerHeroList::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_records.size());
	for (const HeroRecord &r : m_records)
	{
		h.addFloat(r.experience);
		h.addI32(r.rank);
		h.addI32(r.baseRank);
		for (std::uint32_t w : r.upgradeMask.words)
		{
			h.addU32(w);
		}
		h.addU32(r.cost);
		h.addI32(r.startFrame);
		h.addI32(r.seconds);
		h.addBool(r.dead);
		h.addBool(r.objectFlag);
		h.addU32(r.productionID);
		h.addI32(r.levelCap);
		h.addFloat(r.uiFactor);
		h.addString(r.objectName);
		h.addString(r.templateName);
	}
}
