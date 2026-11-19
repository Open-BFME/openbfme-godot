// OpenBFME. GPL-3.0.
// See Common/PlayerHeroList.h.

#include "Common/PlayerHeroList.h"

#include "Common/BuildAssistant.h"
#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Upgrade.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "Common/Xfer.h"

#include <optional>

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
HeroRecord PlayerHeroList::recordFromTemplate(const ThingTemplate &tt)
{
	HeroRecord r;
	r.cost = BuildAssistant::buildCost(tt);                                // RW 0x780742: (u16) template + 0x5EA
	r.seconds = SimMath::truncToInt32(BuildAssistant::buildTime(tt));      // RW 0x780752: _ftol(BuildTime)
	r.rank = 1;
	r.productionID = 0;                                                    // RW 0x780780: + 0xB4 = 0
	r.templateName = tt.getName();
	return r;
}

void PlayerHeroList::addPurchase(const ThingTemplate &tt)
{
	m_records.push_back(recordFromTemplate(tt));
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

namespace
{
// lane BRUTALAI-1, RW 0x780AF2 .. 0x780B5A / 0x780B91 .. 0x780BF9: the difficulty gate reads the PRODUCER's controlling player (RW 0x68B678 on the second
// argument, not the record's owner); the record's value (whose calcCostToBuild / calcTimeToBuild already took the discount once: retail discounts a Brutal
// AI's revival twice) becomes trunc((1.0 - m) * value) (subss; cvtsi2ss; mulss; cvttss2si). RW reads the producer unchecked; a null producer (only the
// port's UI callers pass one) gets no discount
int brutalHeroValue(const Object *producer, bool time, int value)
{
	const Player *p = producer ? producer->getControllingPlayer() : nullptr;
	if (p)
	{
		if (const std::optional<float> m = p->getBrutalBuildReduction(time))
		{
			return SimMath::cvttss2si(SimMath::sseMul(SimMath::sseFromInt32(value), SimMath::sseSub(1.0f, *m)));
		}
	}
	return value;
}
} // namespace

// RW 0x780AD3 (BFME2 decomp Common/RTS/UnitRevivalEntry.cpp:511)
int PlayerHeroList::costAt(GameLogic &logic, const Player &owner, int index, const Object *producer) const
{
	const HeroRecord *r = at(index);
	return r ? brutalHeroValue(producer, false, recordCost(logic, owner, *r, producer)) : 0;
}

// RW 0x780B72 (BFME2 decomp Common/RTS/UnitRevivalEntry.cpp:551)
int PlayerHeroList::framesForProductionID(GameLogic &logic, const Player &owner, std::uint32_t id, const Object *producer) const
{
	const HeroRecord *r = findByProductionID(id);
	return r ? brutalHeroValue(producer, true, recordFrames(logic, owner, *r, producer)) : 0;
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

// RW 0x780D22: an idle record returns one and writes cost; an active record leaves cost alone, and a nonpositive duration returns zero.
double PlayerHeroList::availabilityAt(GameLogic &logic, const Player &owner, int index, const Object *producer, unsigned *cost) const
{
	const HeroRecord *r = at(index);
	if (!r)
	{
		return 0.0;
	}
	if (r->startFrame == -1)
	{
		if (cost)
		{
			*cost = (unsigned)recordCost(logic, owner, *r, producer);
		}
		return 1.0;
	}
	const int time = recordFrames(logic, owner, *r, producer);
	if (time <= 0)
	{
		return 0.0;
	}
	const double elapsed = SimMath::pc24SubD((double)logic.getFrame(), (double)r->startFrame);
	return SimMath::pc24DivD(elapsed, (double)time);
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
		for (unsigned i = 0; i < UpgradeMaskType::activeWords(); ++i) // lane MODS-3: retail's 0x24 words unless the data has more upgrades
		{
			const std::uint32_t w = r.upgradeMask.words[i];
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
		if (!r.deadImage.empty())
		{
			h.addString(r.deadImage);
		}
	}
}

// lane SAVE-4a: OpenBFME native layout of the hero records (RW Player + 0x758; retail transfers them inside Player::DoXfer RW 0x6B205F, not inventoried
// field by field): every field of every record, in list order
void PlayerHeroList::xfer(Xfer &x)
{
	// version 2 (Local 8, #113): every record carries the dead hero's image name, as retail transfers + 0x00 for every record (RW 0x780A29 .. 0x780A79);
	// a version 1 save loads with no stored image (the button falls back to the template's image)
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	// a record's fixed bytes: experience, rank, base rank, the upgrade mask (0x24 words), cost, start frame, seconds, two flags, production id, level
	// cap, UI factor, two empty names
	x.xferVectorSize(m_records, 4 + 4 + 4 + 4 * UpgradeMaskType::activeWords() + 4 + 4 + 4 + 1 + 1 + 4 + 4 + 4 + 1 + 1); // lane MODS-3: activeWords
	for (HeroRecord &r : m_records)
	{
		x.xferReal(&r.experience);
		x.xferInt(&r.rank);
		x.xferInt(&r.baseRank);
		for (unsigned i = 0; i < UpgradeMaskType::activeWords(); ++i)
		{
			x.xferUnsignedInt(&r.upgradeMask.words[i]);
		}
		if (x.isLoading())
		{
			checkLoadedUpgradeMask(r.upgradeMask, "a hero record's upgrades"); // lane SAVE-4e (closed sets)
		}
		x.xferUnsignedInt(&r.cost);
		x.xferInt(&r.startFrame);
		x.xferInt(&r.seconds);
		x.xferBool(&r.dead);
		x.xferBool(&r.objectFlag);
		x.xferUnsignedInt(&r.productionID);
		x.xferInt(&r.levelCap);
		x.xferReal(&r.uiFactor);
		x.xferAsciiString(&r.objectName);
		x.xferAsciiString(&r.templateName);
		if (v.current >= 2)
		{
			x.xferAsciiString(&r.deadImage); // RW 0x780A29 .. 0x780A79: + 0x00 (the image name, every record)
		}
		else if (x.isLoading())
		{
			r.deadImage.clear();
		}
	}
}
