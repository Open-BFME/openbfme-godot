// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/Module/SalvageCrateCollide.h for the target facts, the donors and what is inference.

#include "GameLogic/Module/SalvageCrateCollide.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/StateHash.h"
#include "Common/Thing/KindOfTokens.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "GameLogic/Economy.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

namespace
{
const char kFile[] = "SalvageCrateCollide.cpp"; // RW 0xC715F8 (the retail source path's file)

int kindOfBit(const char *name)
{
	return KindOfTokens::indexOf(name);
}

// RW 0x70C4FE testSetAndClear(mustBeSet, mustBeClear): every bit of the first mask, none of the second
bool kindOfMulti(const Object &obj, const KindOfMaskType &set, const KindOfMaskType &clear)
{
	const KindOfMaskType &k = obj.getKindOf();
	for (size_t w = 0; w < k.size(); ++w)
	{
		if ((k[w] & set[w]) != set[w] || (k[w] & clear[w]) != 0)
		{
			return false;
		}
	}
	return true;
}

// RW 0x68B760 Object::isNeutralControlled: the controlling player is the neutral one
bool isNeutralControlled(const Object &obj)
{
	const Player *p = obj.getControllingPlayer();
	return p && p == obj.logic().players().getNeutralPlayer();
}

// RW 0x4B12E8: Thing::getHeightAboveTerrain (RW 0x70B8AE: z less the terrain height, stored as a float) above 0.0 (GarrisonRules::isAboveTerrain)
bool isAboveTerrain(const Object &obj)
{
	const Coord3D &p = *obj.getPosition();
	return SimMath::subf32(p.z, obj.logic().getGroundHeight(p.x, p.y)) > 0.0f;
}
} // namespace

// ---- CrateCollide --------------------------------------------------------------------------------------------------------------------------------------------

// RW 0x8C066C: BehaviorModule (RW 0x8BF84B), + 0x14 = 0
CrateCollide::CrateCollide(Thing *thing, const CrateCollideModuleData *data)
	: BehaviorModule(thing, data)
	, m_crateData(data)
{
}

bool CrateCollide::isCrate(const Object &obj)
{
	for (CollideModuleInterface *c : obj.collideModules())
	{
		if (dynamic_cast<CrateCollide *>(c))
		{
			return true;
		}
	}
	return false;
}

// RW 0x8C0664: add ecx, -0x10; jmp [vtable + 0x34]
bool CrateCollide::wouldLikeToCollideWith(const Object &other) const
{
	return isValidToExecute(&other);
}

// RW 0x8C06B6
void CrateCollide::onCollide(Object *other, const Coord3D *, const Coord3D *)
{
	if (!isValidToExecute(other)) // vslot 0x34
	{
		return;
	}
	if (executeCrateBehavior(*other)) // vslot 0x30
	{
		Object *crate = getObject();
		GameLogic &logic = crate->logic();
		const std::string &fx = m_crateData->m_executeFX; // data + 0x48
		if (FXEventLog::isFXName(fx))
		{
			logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "CrateCollide", logic.getFrame(), fx, *other)); // RW 0x4B1B5A(fx, other, 0)
		}
		m_everExecuted = true;      // RW 0x8C06F8: + 0x14 = 1
		++m_executions;
		logic.destroyObject(crate); // RW 0x8C0702 -> 0x62BBAB
	}
	// RW 0x8C0707 .. 0x8C0754: the ExecuteAnimation (client Anim2D; no 2.01 crate sets one): not shown (S-260)
}

// RW 0x8C080B
bool CrateCollide::isValidToExecute(const Object *other) const
{
	if (!other || m_everExecuted) // + 0x14
	{
		return false;
	}
	if (isNeutralControlled(*other)) // RW 0x68B760
	{
		return false;
	}
	static const int kStructure = kindOfBit("STRUCTURE"), kCreep = kindOfBit("CREEP");
	const CrateCollideModuleData *d = m_crateData;
	const bool validBuildingAttempt = d->m_buildingPickup && kStructure >= 0 && other->isKindOf((unsigned)kStructure); // + 0x41, template + 0x108 bit 7
	if (!other->getAIUpdateInterface() && !validBuildingAttempt) // Object + 0x260
	{
		return false;
	}
	if (kCreep >= 0 && other->isKindOf((unsigned)kCreep)) // template + 0x11D bit 6
	{
		const Player *p = other->getControllingPlayer();
		if (!p || !p->getPlayerTemplate() || !p->getPlayerTemplate()->m_playableSide) // RW 0x6AAC66 (a player without a template answers false)
		{
			return false;
		}
	}
	if (!kindOfMulti(*other, d->m_requiredKindOf, d->m_forbiddenKindOf)) // RW 0x70C4FE(+ 0x08, + 0x24)
	{
		return false;
	}
	if (other->isEffectivelyDead()) // + 0x458 bit 0
	{
		return false;
	}
	const Object *crate = getObject();
	if (isAboveTerrain(*crate) && !validBuildingAttempt) // RW 0x4B12E8
	{
		return false;
	}
	if (d->m_forbidOwnerPlayer && crate->getControllingPlayer() == other->getControllingPlayer()) // + 0x40
	{
		return false;
	}
	const Player *otherPlayer = other->getControllingPlayer();
	if (d->m_humanOnly && otherPlayer && otherPlayer->getPlayerType() != PLAYER_HUMAN) // + 0x42, Player + 0x5C != 0
	{
		return false;
	}
	if (d->m_pickupScience != -1 && otherPlayer && !otherPlayer->hasScience(d->m_pickupScience)) // + 0x44, RW 0x6AC207
	{
		return false;
	}
	return true; // RW 0x8C0915: not an undetected defector (+ 0x458 bit 1, never set here: S-1020)
}

void CrateCollide::crc(StateHasher &hasher) const
{
	hasher.addBool(m_everExecuted);
}

// RW 0x8C0760
void CrateCollide::xferCrateCollide(Xfer &x)
{
	SnapshotXfer::version(x, 2, 2); // RW's current version 2 (+ 0x14 is read from version 2 on)
	xferBehaviorModule(x);          // RW 0x9933FE
	x.xferBool(&m_everExecuted);
	SnapshotXfer::u64(x, m_executions);
}

// ---- SalvageCrateCollide -----------------------------------------------------------------------------------------------------------------------------------

// RW 0x8C12C2
SalvageCrateCollide::SalvageCrateCollide(Thing *thing, const SalvageCrateCollideModuleData *data)
	: CrateCollide(thing, data)
	, m_salvageData(data)
{
}

// RW 0x8C145D
bool SalvageCrateCollide::isValidToExecute(const Object *other) const
{
	static const int kImmobile = kindOfBit("IMMOBILE");
	return CrateCollide::isValidToExecute(other) && other->getAIUpdateInterface() && !(kImmobile >= 0 && other->isKindOf((unsigned)kImmobile)); // + 0x260, + 0x108 bit 2
}

// RW 0x8C136C
int SalvageCrateCollide::pickOutcome()
{
	const float r = getObject()->logic().random().getValueReal(0.0f, 1.0f, kFile, 0x45); // RW 0x6D332C
	if (salvageData()->m_levelUpChance > r)                                                  // fld + 0x64; fcomip; jbe
	{
		return OUTCOME_LEVEL_UP;
	}
	return salvageData()->m_upgrade.empty() ? OUTCOME_MONEY : OUTCOME_UPGRADE; // + 0x78: the length word > 0 -> 4
}

// RW 0x8C158B
bool SalvageCrateCollide::executeCrateBehavior(Object &other)
{
	const SalvageCrateCollideModuleData *d = salvageData();
	if (!d->m_allowAIPickup) // + 0x7C
	{
		const Player *p = other.getControllingPlayer(); // RW 0x68B678
		if (p && p->getPlayerType() == PLAYER_COMPUTER) // Player + 0x5C == 1
		{
			++m_refusedComputer;
			return false;
		}
	}
	const int outcome = pickOutcome();
	m_lastOutcome = outcome;
	if (outcome == OUTCOME_LEVEL_UP)
	{
		// RW 0x8C1625: the experience tracker (+ 0x26C) gains one level; the "GUI:GainRank" floating text (RW 0x8C13BB) is the client's: not shown (S-260)
		if (ExperienceTracker *xp = other.getExperienceTracker())
		{
			xp->gainExpForLevel(1, true, false); // RW 0x79DA0A(1, 1, 0)
		}
	}
	else if (outcome == OUTCOME_UPGRADE)
	{
		// RW 0x8C15EE
		const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(d->m_upgrade) : nullptr; // RW 0x66F5E5
		if (u && u->getUpgradeType() == UPGRADE_TYPE_PLAYER)                                                // + 4 == 0
		{
			// retail calls RW 0x68B678's result without a null test: an AI object always has a controlling player here
			if (Player *p = other.getControllingPlayer())
			{
				p->addUpgrade(u, Player::UPGRADE_STATUS_COMPLETE, false); // RW 0x6AEE22(u, 2, 0)
			}
		}
	}
	else
	{
		doMoney(other); // RW 0x8C171A
	}
	return true;
}

// RW 0x8C148B
void SalvageCrateCollide::doMoney(Object &other)
{
	const SalvageCrateCollideModuleData *d = salvageData();
	Object *crate = getObject();
	GameLogic &logic = crate->logic();
	int amount = d->m_minResource; // + 0x70
	if (d->m_minResource != d->m_maxResource)
	{
		amount = logic.random().getValue(d->m_minResource, d->m_maxResource, kFile, 0xDB); // RW 0x6D328E
	}
	if (amount <= 0) // RW 0x8C14C8: jle
	{
		return;
	}
	Economy &economy = logic.economy();
	if (economy.isMultiplayerGame()) // RW 0x625456
	{
		// RW 0x8C14DD .. 0x8C1508: MultiPlayMoneyMult[n - 1] (RW 0x6A8630(0), RW 0x642002) in st0; fild amount; fmul st(1); _ftol (RW 0xA3CFA4)
		const float mult = economy.multiPlayMoneyMult(economy.livePlayableCount(false));
		amount = (int)NumericState::ftol2Low32(NumericState::pc24MulW((double)amount, (double)mult));
	}
	Player *player = other.getControllingPlayer(); // RW 0x68B678
	if (player)
	{
		amount = economy.applyHandicapMoney(*player, amount);              // RW 0x6AA858
		player->getMoney()->deposit((std::uint32_t)amount, &player->getScoreKeeper(), true); // RW 0x7B18B8(amount, Player + 0x3DC, 1)
		economy.noteIncome(*crate, *player, amount); // the "GUI:AddCash" floating text at the crate + 10 (RW 0x8C13BB, client)
	}
	m_lastMoney = amount;
}

void SalvageCrateCollide::crc(StateHasher &hasher) const
{
	CrateCollide::crc(hasher);
}

// RW 0x8C1442: version (1, 1), then CrateCollide's (RW 0x8C0760)
void SalvageCrateCollide::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1); // RW 0x404F15
	xferCrateCollide(x);
}
