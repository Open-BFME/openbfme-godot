// OpenBFME. GPL-3.0.
// See Common/Player.h.

#include "Common/Player.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/NumericState.h"
#include "GameLogic/SimMath.h"

#include "Common/PlayerEconomyHost.h"
#include "Common/StateHash.h"
#include "Common/Science.h"
#include "Common/Team.h"
#include "Common/Upgrade.h"
#include "Common/BuildAssistant.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/WorkerAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"

#include <stdexcept>

Relationship Player::getRelationship(const Team *that) const
{
	if (that)
	{
		if (!m_teamRelations.empty())
		{
			auto it = m_teamRelations.find(that->getID());
			if (it != m_teamRelations.end())
			{
				return it->second;
			}
		}
		if (!m_playerRelations.empty())
		{
			const Player *thatPlayer = that->getControllingPlayer();
			if (thatPlayer != nullptr)
			{
				auto it = m_playerRelations.find(thatPlayer->getPlayerIndex());
				if (it != m_playerRelations.end())
				{
					return it->second;
				}
			}
		}
	}
	return NEUTRAL;
}

Relationship Player::getRelationship(const Player *that) const
{
	if (that)
	{
		auto it = m_playerRelations.find(that->getPlayerIndex());
		if (it != m_playerRelations.end())
		{
			return it->second;
		}
	}
	return NEUTRAL;
}

void Player::setPlayerRelationship(const Player *that, Relationship r)
{
	if (that != nullptr)
	{
		m_playerRelations[that->getPlayerIndex()] = r; // creates the entry if it does not exist
	}
}

void Player::setTeamRelationship(const Team *that, Relationship r)
{
	if (that != nullptr)
	{
		m_teamRelations[that->getID()] = r;
	}
}

void Player::removeTeamRelationship(const Team *that)
{
	if (that != nullptr)
	{
		m_teamRelations.erase(that->getID());
	}
}

// ZH Player::init(const PlayerTemplate *) (Player.cpp:360-470, the parts this layer has)
void Player::init(const PlayerTemplate *pt, std::uint32_t defaultStartingCash)
{
	m_observer = false;
	m_defeated = false;
	m_defeatFrame = 0;
	m_bountyPercent = 0.0f;
	m_defaultTeam = nullptr;
	m_template = pt;
	m_side.clear();
	m_color = 0;
	m_nightColor = 0;
	m_skirmishDifficulty = -1;
	m_handicap = 0;
	m_colorExplicit = false;
	m_unitsShouldHunt = false; // RW 0x6B0401: + 0x35D cleared by the init (lane DECOMP-2, Sol's review)
	m_resourceGathering.reset(); // lane LUMBER1: ZH Player::init makes a new ResourceGatheringManager (RW + 0x304)
	m_money.init();
	m_money.setPlayerIndex(m_index);
	// RW 0x6B06D4 .. 0x6B0707 (the player's reset): score, cost modifier lists, command points; the upgrades are the upgrade lane's reset
	m_score.reset(m_index); // lane END-1: RW 0x79EB10 takes the player index (+ 0x100)
	m_commandPoints.reset();
	m_upgradeRecords.clear();
	m_upgradesInProgress = UpgradeMaskType{};
	m_upgradesCompleted = UpgradeMaskType{};
	m_costModifiers.clear();
	m_upgradeDiscounts.clear();
	m_heroes.clear(); // HERO-1: the list is filled at the game start (RW 0x6B16EC)
	resetAIPlayerRepair(); // LOCAL-94: RW AIPlayer::init (RW 0x8F83F9)
	if (pt)
	{
		m_side = pt->m_side;
		// ZH RGBColor::getAsInt: each channel (Int)(c * 255.0f), then | 0xff000000
		const RGBColor &c = pt->m_preferredColor;
		m_color = ((std::uint32_t)SimMath::truncToInt32(SimMath::mulf32(c.red, 255.0f)) << 16) | ((std::uint32_t)SimMath::truncToInt32(SimMath::mulf32(c.green, 255.0f)) << 8) |
			(std::uint32_t)SimMath::truncToInt32(SimMath::mulf32(c.blue, 255.0f)) | 0xff000000u;
		m_money = pt->m_money;
		m_money.setPlayerIndex(m_index);
		m_observer = pt->m_isObserver;
		if (m_money.countMoney() == 0)
		{
			m_money = Money(defaultStartingCash);
			m_money.setPlayerIndex(m_index);
		}
		// lane FACT-B: RW 0x6B0738 -> RW 0x6AFC92 Player::initPlayerUpgrades (tier A, BFME2 decomp Player_initPlayerUpgrades.cpp): every InitialUpgrades name
		// (template + 0x180, in order) that TheUpgradeCenter knows (RW 0x66F5E5; an unknown name is skipped) is added COMPLETE, not silent
		// (RW 0x6AEE22(upgrade, 2, 0)). Upgrade_IsengardFaction / Upgrade_MordorFaction / ... and the dual economy choices are what modules such
		// as the ring hero's RemoveUpgradeUpgrade are triggered by. addUpgrade's steps are taken here without its object pass (RW 0x6AE483 visits the
		// player's objects: a player being made has none, and the economy host that walks them is attached later in this port)
		for (const std::string &name : pt->m_initialUpgrades)
		{
			const UpgradeTemplate *u = resolveUpgrade(name, false);
			if (!u || u->getMaskBit() < 0)
			{
				continue;
			}
			bool listed = false; // RW 0x6AAA52: an upgrade named twice keeps its one record
			for (UpgradeRecord &r : m_upgradeRecords)
			{
				listed = listed || r.upgrade == u;
				r.status = r.upgrade == u ? UPGRADE_STATUS_COMPLETE : r.status;
			}
			if (!listed)
			{
				m_upgradeRecords.insert(m_upgradeRecords.begin(), UpgradeRecord{ u, UPGRADE_STATUS_COMPLETE }); // RW 0x6AEE6E: linked at the head
			}
			m_upgradesCompleted.set((unsigned)u->getMaskBit());
			if (m_host)
			{
				m_host->reportUpgradeEva(*this, *u, true);
			}
		}
	}
}

void Player::crc(StateHasher &h) const
{
	h.addI32(m_index);
	if (m_createAHeroSurcharge >= 0) // lane HERO-2: only a player with a Create-a-Hero adds it
	{
		h.addI32(m_createAHeroSurcharge);
	}
	h.addString(m_name);
	h.addString(m_displayName);
	h.addString(m_side);
	h.addString(m_template ? m_template->getName() : std::string()); // the faction template (stable by name; "" = none)
	h.addI32((int)m_type);
	h.addBool(m_skirmish);
	h.addBool(m_observer);
	h.addBool(m_defeated);
	h.addU32(m_defeatFrame);
	h.addFloat(m_bountyPercent);
	h.addI32(m_mpStartIndex);
	h.addU32(m_color);
	h.addU32(m_nightColor);
	h.addI32(m_skirmishDifficulty);
	if (m_handicap != 0)
	{
		h.addI32(m_handicap); // lane OWNERFIX-2: hashed only when set, so games without a handicap keep their hash
	}
	h.addBool(m_colorExplicit);
	if (m_unitsShouldHunt)
	{
		h.addBool(m_unitsShouldHunt); // lane DECOMP-2: hashed only once set (PLAYER_HUNT), so games without it keep their hash
	}
	// lane CAMP-1: the attacked-by flags, hashed only when one is set (a game without player damage keeps its hash)
	for (int i = 0; i < kAttackedBySlots; ++i)
	{
		if (m_attackedBy[i])
		{
			h.addI32(0xA77 + i);
			h.addU32(m_attackedFrame);
		}
	}
	m_money.crc(h);
	m_score.crc(h);
	m_commandPoints.crc(h);
	m_science.crc(h); // SPELL-1
	m_heroes.crc(h);  // HERO-1
	if (!m_resourceGathering.supplyCenters().empty() || !m_resourceGathering.supplyWarehouses().empty())
	{
		h.addU32(0x5C);
		m_resourceGathering.crc(h); // lane LUMBER1: hashed only once a supply center registered (a game without one keeps its hash)
	}
	if (!isAIPlayerRepairUntouched())
	{
		// LOCAL-94: RW AIPlayer + 0x4C .. + 0x6C; hashed only once PLAYER_REPAIR_NAMED_STRUCTURE queued a repair
		h.addU32(0x2FC0004Cu);
		h.addU32(m_structuresToRepair[0]);
		h.addU32(m_structuresToRepair[1]);
		h.addU32(m_repairDozer);
		h.addFloat(m_repairDozerOrigin.x);
		h.addFloat(m_repairDozerOrigin.y);
		h.addFloat(m_repairDozerOrigin.z);
		h.addI32(m_structuresInQueue);
		h.addBool(m_dozerQueuedForRepair);
		h.addBool(m_dozerIsRepairing);
		h.addI32(m_bridgeTimer);
	}
	h.addU32((std::uint32_t)m_upgradeRecords.size()); // UPGRADE-1: the records in list order, then both masks
	for (const UpgradeRecord &r : m_upgradeRecords)
	{
		h.addI32(r.upgrade ? r.upgrade->getMaskBit() : -1);
		h.addI32(r.status);
	}
	for (unsigned i = 0; i < UpgradeMaskType::activeWords(); ++i) // lane MODS-3: retail's 0x24 words unless the data has more upgrades
	{
		const std::uint32_t w = m_upgradesInProgress.words[i];
		h.addU32(w);
	}
	for (unsigned i = 0; i < UpgradeMaskType::activeWords(); ++i) // lane MODS-3: retail's 0x24 words unless the data has more upgrades
	{
		const std::uint32_t w = m_upgradesCompleted.words[i];
		h.addU32(w);
	}
	h.addU32((std::uint32_t)m_costModifiers.size());
	for (const CostModifier &c : m_costModifiers)
	{
		ObjectFilterMatch::crc(h, c.filter.get());
		h.addU32((std::uint32_t)c.percents.size());
		for (float f : c.percents)
		{
			h.addFloat(f);
		}
		h.addU32(c.source);
		h.addBool(c.slaughter);
	}
	h.addU32((std::uint32_t)m_upgradeDiscounts.size());
	for (const UpgradeDiscount &d : m_upgradeDiscounts)
	{
		h.addString(d.sourceName);
		h.addI32(d.count);
		h.addU32((std::uint32_t)d.percents.size());
		for (float f : d.percents)
		{
			h.addFloat(f);
		}
		h.addU32((std::uint32_t)d.applyTo.size());
		for (const std::string &n : d.applyTo)
		{
			h.addString(n);
		}
		h.addFloat(d.current);
	}
	h.addU32(m_defaultTeam ? m_defaultTeam->getID() : 0u);
	h.addU32((std::uint32_t)m_playerRelations.size());
	for (const auto &kv : m_playerRelations)
	{
		h.addI32(kv.first);
		h.addI32((int)kv.second);
	}
	h.addU32((std::uint32_t)m_teamRelations.size());
	for (const auto &kv : m_teamRelations)
	{
		h.addU32(kv.first);
		h.addI32((int)kv.second);
	}
	h.addBool(m_canBuildUnits);
	h.addBool(m_canBuildBase);
	h.addU32((std::uint32_t)m_disabledTemplateIds.size());
	for (unsigned short id : m_disabledTemplateIds)
	{
		h.addU32(id);
	}
	if (!m_disabledSciences.empty() || !m_hiddenSciences.empty()) // lane SCRIPT-3: hashed once a script set one
	{
		h.addU32(0x31Cu | ((std::uint32_t)m_disabledSciences.size() << 16));
		for (ScienceType st : m_disabledSciences)
		{
			h.addI32((int)st);
		}
		h.addU32((std::uint32_t)m_hiddenSciences.size());
		for (ScienceType st : m_hiddenSciences)
		{
			h.addI32((int)st);
		}
	}
	h.addU32((std::uint32_t)m_selection.size());
	for (ObjectID id : m_selection)
	{
		h.addU32(id);
	}
	// the hotkey squads (lane HUD-1): control groups are simulation state, the selection messages of a later frame read them
	for (const std::vector<ObjectID> &squad : m_hotkeySquads)
	{
		h.addU32((std::uint32_t)squad.size());
		for (ObjectID id : squad)
		{
			h.addU32(id);
		}
	}
}

// ---- lane ECON-1 ----------------------------------------------------------------------------------------------------------------
PlayerEconomyHost &Player::host() const
{
	if (!m_host)
	{
		throw std::logic_error("Player " + m_name + ": no economy host attached (PlayerList::setEconomyHost / EconomySystem)");
	}
	return *m_host;
}

int Player::commandPointLimit() const
{
	return host().commandPointLimit(*this);
}

int Player::commandPointsAvailable() const
{
	return m_commandPoints.getAvailable(commandPointLimit()); // RW 0x6A7F6A
}

bool Player::canAffordCommandPoints(const ThingTemplate &tt) const
{
	return host().canAffordCommandPoints(*this, tt);
}

bool Player::hasObjectMatching(const ObjectFilter &filter, bool completedOnly) const
{
	return host().hasObjectMatching(*this, filter, completedOnly);
}

std::optional<float> Player::getBrutalBuildReduction(bool time) const
{
	return host().brutalReduction(*this, time);
}

float Player::getProductionCostChangeBasedOnKindOf(const ThingTemplate &tt, bool slaughter) const
{
	return host().productionCostChange(*this, tt, slaughter);
}

int Player::applyHandicapMoney(int amount) const
{
	return host().applyHandicapMoney(*this, amount);
}

void Player::addCostModifier(std::shared_ptr<const ObjectFilter> filter, const std::vector<float> &percents, ObjectID source, bool slaughter)
{
	CostModifier c; // RW 0x6AD845: a new entry at the end of the list
	c.filter = std::move(filter);
	c.percents = percents;
	c.source = source;
	c.slaughter = slaughter;
	m_costModifiers.push_back(std::move(c));
}

bool Player::removeCostModifier(const ObjectFilter *filter, const std::vector<float> &percents, ObjectID source)
{
	// RW 0x6AE74C: the first entry whose percentages, filter and source object are all equal
	for (size_t i = 0; i < m_costModifiers.size(); ++i)
	{
		const CostModifier &c = m_costModifiers[i];
		const bool sameFilter = (c.filter == nullptr) == (filter == nullptr) && (!filter || *c.filter == *filter);
		if (c.source == source && sameFilter && c.percents == percents)
		{
			m_costModifiers.erase(m_costModifiers.begin() + (std::ptrdiff_t)i);
			return true;
		}
	}
	return false;
}

namespace
{
// RW 0x6AB5E0
void recomputeDiscount(Player::UpgradeDiscount &d)
{
	if (d.count == 0)
	{
		d.current = 0.0f;
		return;
	}
	size_t idx = (size_t)(d.count - 1);
	if (!d.percents.empty() && idx > d.percents.size() - 1)
	{
		idx = d.percents.size() - 1;
	}
	d.current = d.percents.empty() ? 0.0f : d.percents[idx];
}
} // namespace

void Player::addUpgradeDiscount(const std::string &sourceName, const std::vector<float> &percents, const std::vector<std::string> &applyTo)
{
	for (UpgradeDiscount &d : m_upgradeDiscounts) // RW 0x6B2C67
	{
		if (d.sourceName == sourceName && d.percents == percents && d.applyTo == applyTo)
		{
			++d.count;
			recomputeDiscount(d);
			return;
		}
	}
	UpgradeDiscount d;
	d.sourceName = sourceName;
	d.count = 1;
	d.percents = percents;
	d.applyTo = applyTo;
	recomputeDiscount(d);
	m_upgradeDiscounts.push_back(std::move(d));
}

void Player::removeUpgradeDiscount(const std::string &sourceName, const std::vector<float> &percents, const std::vector<std::string> &applyTo)
{
	for (size_t i = 0; i < m_upgradeDiscounts.size(); ++i) // RW 0x6B1992
	{
		UpgradeDiscount &d = m_upgradeDiscounts[i];
		if (d.sourceName == sourceName && d.percents == percents && d.applyTo == applyTo)
		{
			if (d.count == 1)
			{
				m_upgradeDiscounts.erase(m_upgradeDiscounts.begin() + (std::ptrdiff_t)i);
			}
			else
			{
				--d.count;
				recomputeDiscount(d);
			}
			return;
		}
	}
}

float Player::getUpgradeCostChange(const std::string &upgradeName) const
{
	float sum = 0.0f;
	for (const UpgradeDiscount &d : m_upgradeDiscounts) // RW 0x6AE7CB / 0x6ADA3E; the sum is an x87 fadd of floats (PC24)
	{
		float v = 0.0f;
		bool named = d.applyTo.empty() || upgradeName.empty(); // RW 0x6ADA45: the empty name (AsciiString::TheEmptyString, RW 0xDC62B8) takes every entry
		for (const std::string &n : d.applyTo)
		{
			if (n == upgradeName)
			{
				named = true;
			}
		}
		if (named)
		{
			v = d.current;
		}
		sum = NumericState::pc24Add(v, sum);
	}
	return sum;
}

// lane CAMP-1H: RW 0x6AC6B0 (Player::setDefaultTeam): the team "team" + the player's name (RW 0x7A7147) becomes + 0x30C and is activated
void Player::setDefaultTeam(Team *team)
{
	m_defaultTeam = team;
	if (team)
	{
		team->setActive();
	}
}

// ---- lane UPGRADE-1 ---------------------------------------------------------------------------------------------------------------------
const UpgradeTemplate *Player::resolveUpgrade(const std::string &name, bool mustExist)
{
	if (!TheUpgradeCenter)
	{
		throw std::logic_error("upgrade '" + name + "' asked by name with no UpgradeCenter installed (TheUpgradeCenter)");
	}
	const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(name);
	if (!u && mustExist)
	{
		throw std::logic_error("'" + name + "' is not an Upgrade");
	}
	return u;
}

int Player::upgradeStatus(const UpgradeTemplate *upgrade) const
{
	for (const UpgradeRecord &r : m_upgradeRecords)
	{
		if (r.upgrade == upgrade)
		{
			return r.status;
		}
	}
	return UPGRADE_STATUS_INVALID;
}

void Player::addUpgrade(const UpgradeTemplate *upgrade, UpgradeStatus status, bool silent)
{
	if (!upgrade || upgrade->getMaskBit() < 0)
	{
		throw std::logic_error("Player::addUpgrade: no upgrade template");
	}
	UpgradeRecord *rec = nullptr; // RW 0x6AAA52 findUpgrade in the list
	for (UpgradeRecord &r : m_upgradeRecords)
	{
		if (r.upgrade == upgrade)
		{
			rec = &r;
			break;
		}
	}
	if (!rec)
	{
		m_upgradeRecords.insert(m_upgradeRecords.begin(), UpgradeRecord{ upgrade, UPGRADE_STATUS_INVALID }); // RW 0x6AEE6E: linked at the head
		rec = &m_upgradeRecords.front();
	}
	rec->status = status;
	const unsigned bit = (unsigned)upgrade->getMaskBit();
	if (status == UPGRADE_STATUS_IN_PRODUCTION)
	{
		m_upgradesInProgress.set(bit);
	}
	else if (status == UPGRADE_STATUS_COMPLETE)
	{
		m_upgradesInProgress.clear(bit);
		m_upgradesCompleted.set(bit);
		host().updateUpgradeModulesOfPlayer(*this); // RW 0x6AE483
		if (!silent)
		{
			host().reportUpgradeEva(*this, *upgrade, true);
		}
	}
}

void Player::removeUpgrade(const UpgradeTemplate *upgrade, bool silent)
{
	if (!upgrade || upgrade->getMaskBit() < 0)
	{
		throw std::logic_error("Player::removeUpgrade: no upgrade template");
	}
	if (upgrade->getUpgradeType() != UPGRADE_TYPE_OBJECT)
	{
		auto it = m_upgradeRecords.begin();
		while (it != m_upgradeRecords.end() && it->upgrade != upgrade)
		{
			++it;
		}
		if (it == m_upgradeRecords.end())
		{
			return; // RW 0x6AE620: no record
		}
		const int status = it->status;
		m_upgradeRecords.erase(it);
		m_upgradesInProgress.clear((unsigned)upgrade->getMaskBit());
		m_upgradesCompleted.clear((unsigned)upgrade->getMaskBit());
		if (status != UPGRADE_STATUS_COMPLETE)
		{
			return;
		}
	}
	host().removeUpgradeFromObjectsOfPlayer(*this, *upgrade); // RW 0x6AE546
	if (!silent)
	{
		host().reportUpgradeEva(*this, *upgrade, false);
	}
}

// ---- lane SAVE-4a: the player's snapshot ----------------------------------------------------------------------------------------
// DONOR ZH Money::xfer (Money.cpp): version 1, the amount. The player index is the game's (set at init), not saved.
void Money::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferUnsignedInt(&m_money);
}

namespace
{
void checkRelationship(Relationship r)
{
	if ((int)r < (int)ENEMIES || (int)r > (int)ALLIES)
	{
		throw XferException("a relationship of " + std::to_string((int)r) + " (ENEMIES 0, NEUTRAL 1, ALLIES 2)");
	}
}

// TARGET PlayerRelationMap::xfer RW 0x6AE9B7 (tier A, (1, 1)): the version, a u16 count, then per entry the player index (int) and the relationship
// (XferRelationship RW 0x707407: a 4-byte enum); the entries in key order (INFERENCE: retail iterates its map from begin, RW 0x5E0A39 / 0x6BFEFD)
void xferPlayerRelationMap(Xfer &x, std::map<int, Relationship> &m)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	std::uint16_t n = (std::uint16_t)m.size();
	if (m.size() > 0xFFFFu)
	{
		throw XferException("a player relation map of " + std::to_string(m.size()) + " entries (retail's count is a u16)");
	}
	x.xferUnsignedShort(&n);
	if (!x.isLoading())
	{
		for (auto &kv : m)
		{
			std::int32_t k = kv.first;
			x.xferInt(&k);
			x.xferEnum(&kv.second);
		}
		return;
	}
	m.clear();
	for (std::uint16_t i = 0; i < n; ++i)
	{
		std::int32_t k = 0;
		Relationship r = NEUTRAL;
		x.xferInt(&k);
		x.xferEnum(&r);
		checkRelationship(r);
		if (m.count(k)) // lane SAVE-4e (closed sets): a key once (the player index range is checked by Player::checkLoadedIndices)
		{
			throw XferException("a player relation map names player index " + std::to_string(k) + " twice");
		}
		m[k] = r;
	}
}

// TARGET TeamRelationMap::xfer RW 0x7A56B6 (tier A, (1, 1) through the version helper RW 0x404F15): the version, a u16 count, then per entry the team id
// (u32) and the relationship (4 bytes)
void xferTeamRelationMap(Xfer &x, std::map<std::uint32_t, Relationship> &m)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	if (m.size() > 0xFFFFu)
	{
		throw XferException("a team relation map of " + std::to_string(m.size()) + " entries (retail's count is a u16)");
	}
	std::uint16_t n = (std::uint16_t)m.size();
	x.xferUnsignedShort(&n);
	if (!x.isLoading())
	{
		for (auto &kv : m)
		{
			std::uint32_t k = kv.first;
			x.xferUnsignedInt(&k);
			x.xferEnum(&kv.second);
		}
		return;
	}
	m.clear();
	for (std::uint16_t i = 0; i < n; ++i)
	{
		std::uint32_t k = 0;
		Relationship r = NEUTRAL;
		x.xferUnsignedInt(&k);
		x.xferEnum(&r);
		checkRelationship(r);
		if (m.count(k)) // lane SAVE-4e: a team id once (a team gone since may stay named: retail keeps the entry)
		{
			throw XferException("a team relation map names team " + std::to_string(k) + " twice");
		}
		m[k] = r;
	}
}

void xferFloats(Xfer &x, std::vector<float> &v)
{
	x.xferVectorSize(v, 4);
	for (float &f : v)
	{
		x.xferReal(&f);
	}
}

void xferIds(Xfer &x, std::vector<ObjectID> &v)
{
	x.xferVectorSize(v, 4);
	for (ObjectID &id : v)
	{
		x.xferObjectID(&id);
	}
}
} // namespace

void xferRelationMaps(Xfer &x, std::map<std::uint32_t, Relationship> &teams, std::map<int, Relationship> &players)
{
	xferTeamRelationMap(x, teams);
	xferPlayerRelationMap(x, players);
}

// OpenBFME native layout (decision D3): retail's Player::DoXfer (RW 0x6B205F, tier B attempt, window (1, 10)) holds RotWK's whole player; this Player is the
// port's subset, split into sub-objects, so its block is native. The relation maps are retail's own sub-records (above). Every field crc hashes, plus the
// state crc leaves out that a later frame reads (the order mode); not saved: the economy host and the Eva count (not state), the tunnel network
// (obfme:TunnelTracker, SAVE-4b).
// lane SAVE-4e (closed sets): after the players loaded (PlayerList::xfer): the player relation keys and the score keeper's last attacker are player indices of
// this game (0 .. count - 1; the attacker -1 for none); the command points and score keeper name this player (or are unbound)
void Player::checkLoadedIndices(int playerCount) const
{
	for (const auto &kv : m_playerRelations)
	{
		if (kv.first < 0 || kv.first >= playerCount)
		{
			throw XferException("player '" + m_name + "': a relation to player index " + std::to_string(kv.first) + ", which this game lacks");
		}
	}
	// this player's index, or the default of one never bound (CommandPoints 0, ScoreKeeper -1)
	if ((m_commandPoints.getPlayerIndex() != m_index && m_commandPoints.getPlayerIndex() != 0) || (m_score.playerIndex() != m_index && m_score.playerIndex() != -1))
	{
		throw XferException("player '" + m_name + "': its command points name player index " + std::to_string(m_commandPoints.getPlayerIndex())
			+ " and its score keeper " + std::to_string(m_score.playerIndex()) + ", the player is " + std::to_string(m_index));
	}
	if (m_score.lastAttackerIndex() < -1 || m_score.lastAttackerIndex() >= playerCount)
	{
		throw XferException("player '" + m_name + "': its score keeper's last attacker is player index " + std::to_string(m_score.lastAttackerIndex()));
	}
}

void Player::xfer(Xfer &x, TeamFactory &teams)
{
	// lane LUMBER1: version 2 adds ResourceGatheringManager; LOCAL-94: version 3 adds AIPlayer repair queue when touched
	const std::uint8_t saveVer = !isAIPlayerRepairUntouched() ? 3 : 2;
	XferVersion v{ 1, (std::uint8_t)(x.isLoading() ? 3 : saveVer) };
	x.xferVersion(v);
	// the identity the START made: checked, never overwritten
	std::int32_t index = m_index;
	std::string name = m_name;
	std::string templateName = m_template ? m_template->getName() : std::string();
	x.xferInt(&index);
	x.xferAsciiString(&name);
	x.xferAsciiString(&templateName);
	if (x.isLoading() && (index != m_index || name != m_name || templateName != (m_template ? m_template->getName() : std::string())))
	{
		throw XferException("the snapshot's player " + std::to_string(index) + " '" + name + "' (" + templateName + ") is not this game's player "
			+ std::to_string(m_index) + " '" + m_name + "' (" + (m_template ? m_template->getName() : std::string()) + ")");
	}
	x.xferInt(&m_createAHeroSurcharge);
	x.xferAsciiString(&m_displayName);
	x.xferAsciiString(&m_side);
	x.xferEnum(&m_type);
	if (m_type != PLAYER_HUMAN && m_type != PLAYER_COMPUTER)
	{
		throw XferException("player '" + m_name + "': a player type of " + std::to_string((int)m_type));
	}
	x.xferBool(&m_skirmish);
	x.xferBool(&m_observer);
	x.xferBool(&m_defeated);
	x.xferUnsignedInt(&m_defeatFrame);
	x.xferReal(&m_bountyPercent);
	x.xferInt(&m_mpStartIndex);
	x.xferUnsignedInt(&m_color);
	x.xferUnsignedInt(&m_nightColor);
	x.xferInt(&m_skirmishDifficulty);
	x.xferBool(&m_colorExplicit);
	x.xferBool(&m_unitsShouldHunt);
	for (bool &b : m_attackedBy)
	{
		x.xferBool(&b);
	}
	x.xferUnsignedInt(&m_attackedFrame);
	m_money.xfer(x);
	m_score.xfer(x);
	m_commandPoints.xfer(x);
	m_science.xfer(x);
	m_heroes.xfer(x);
	// the upgrade records by name, newest first (RW + 0x9C), then both masks
	std::uint32_t n = (std::uint32_t)m_upgradeRecords.size();
	x.xferCount(&n, 1 + 4, sizeof(UpgradeRecord)); // a name, the status
	if (x.isLoading())
	{
		m_upgradeRecords.assign(n, UpgradeRecord{ nullptr, UPGRADE_STATUS_INVALID });
	}
	for (UpgradeRecord &r : m_upgradeRecords)
	{
		std::string upgrade = r.upgrade ? r.upgrade->getUpgradeName() : std::string();
		x.xferAsciiString(&upgrade);
		x.xferInt(&r.status);
		if (x.isLoading()) // lane SAVE-4e: the record's status set
		{
			SnapshotXfer::checkRange(r.status, UPGRADE_STATUS_INVALID, UPGRADE_STATUS_COMPLETE, "an upgrade record's status");
		}
		if (x.isLoading())
		{
			r.upgrade = resolveUpgrade(upgrade, true);
		}
	}
	for (UpgradeMaskType *mask : { &m_upgradesInProgress, &m_upgradesCompleted })
	{
		for (std::uint32_t &w : mask->words)
		{
			x.xferUnsignedInt(&w);
		}
		if (x.isLoading())
		{
			checkLoadedUpgradeMask(*mask, "player '" + m_name + "': its upgrades"); // lane SAVE-4e: only this game's upgrades
		}
	}
	x.xferVectorSize(m_costModifiers, 2 + 4 + 4 + 1); // an absent filter, no percents, the source, the flag
	for (CostModifier &c : m_costModifiers)
	{
		ObjectFilterMatch::xfer(x, c.filter);
		xferFloats(x, c.percents);
		x.xferObjectID(&c.source);
		x.xferBool(&c.slaughter);
	}
	x.xferVectorSize(m_upgradeDiscounts, 1 + 4 + 4 + 4 + 4); // a name, the count, no percents, no names, the current value
	for (UpgradeDiscount &d : m_upgradeDiscounts)
	{
		x.xferAsciiString(&d.sourceName);
		x.xferInt(&d.count);
		xferFloats(x, d.percents);
		x.xferVectorSize(d.applyTo, 1);
		for (std::string &s : d.applyTo)
		{
			x.xferAsciiString(&s);
		}
		x.xferReal(&d.current);
	}
	std::uint32_t defaultTeam = m_defaultTeam ? m_defaultTeam->getID() : (std::uint32_t)TEAM_ID_INVALID;
	x.xferUnsignedInt(&defaultTeam);
	if (x.isLoading())
	{
		m_defaultTeam = teams.findTeamByID(defaultTeam);
		if (defaultTeam != TEAM_ID_INVALID && !m_defaultTeam)
		{
			throw XferException("player '" + m_name + "': its default team " + std::to_string(defaultTeam) + " does not exist");
		}
	}
	xferPlayerRelationMap(x, m_playerRelations);
	xferTeamRelationMap(x, m_teamRelations);
	x.xferBool(&m_canBuildUnits);
	x.xferBool(&m_canBuildBase);
	n = (std::uint32_t)m_disabledTemplateIds.size();
	x.xferCount(&n, 2, sizeof(unsigned short) + Xfer::kNodeBytes);
	if (!x.isLoading())
	{
		for (unsigned short id : m_disabledTemplateIds)
		{
			std::uint16_t t = id;
			x.xferUnsignedShort(&t);
		}
	}
	else
	{
		m_disabledTemplateIds.clear();
		for (std::uint32_t i = 0; i < n; ++i)
		{
			std::uint16_t t = 0;
			x.xferUnsignedShort(&t);
			m_disabledTemplateIds.insert(t);
		}
	}
	for (std::vector<ScienceType> *list : { &m_disabledSciences, &m_hiddenSciences })
	{
		x.xferVectorSize(*list, 4);
		for (ScienceType &st : *list)
		{
			x.xferInt(&st);
			if (x.isLoading() && TheScienceStore && !TheScienceStore->isValidScience(st)) // lane SAVE-4e: a science of this game's data
			{
				throw XferException("player '" + m_name + "': a disabled or hidden science " + std::to_string(st) + " is not one of this game's data");
			}
		}
	}
	xferIds(x, m_selection);
	for (std::vector<ObjectID> &squad : m_hotkeySquads)
	{
		xferIds(x, squad);
	}
	x.xferInt(&m_orderMode);
	if (v.current >= 2)
	{
		m_resourceGathering.xfer(x); // lane LUMBER1 (RW 0x8FAD17)
	}
	else if (x.isLoading())
	{
		m_resourceGathering.reset(); // a version 1 snapshot predates the supply centers' registration
	}
	if (v.current >= 3)
	{
		x.xferObjectID(&m_structuresToRepair[0]);
		x.xferObjectID(&m_structuresToRepair[1]);
		x.xferObjectID(&m_repairDozer);
		x.xferReal(&m_repairDozerOrigin.x);
		x.xferReal(&m_repairDozerOrigin.y);
		x.xferReal(&m_repairDozerOrigin.z);
		x.xferInt(&m_structuresInQueue);
		x.xferBool(&m_dozerQueuedForRepair);
		x.xferBool(&m_dozerIsRepairing);
		x.xferInt(&m_bridgeTimer);
		if (x.isLoading() && (m_structuresInQueue < 0 || m_structuresInQueue > kMaxStructuresToRepair))
		{
			throw XferException("player '" + m_name + "': invalid structuresInQueue " + std::to_string(m_structuresInQueue));
		}
	}
	else if (x.isLoading())
	{
		resetAIPlayerRepair();
	}
}

// ---- LOCAL-94: AIPlayer structure repair queue & superweapon target computation ----------------------------------------------------

void Player::resetAIPlayerRepair()
{
	m_structuresToRepair = { INVALID_ID, INVALID_ID };
	m_repairDozer = INVALID_ID;
	m_repairDozerOrigin = Coord3D{ 0.0f, 0.0f, 0.0f };
	m_structuresInQueue = 0;
	m_dozerQueuedForRepair = false;
	m_dozerIsRepairing = false;
	m_bridgeTimer = 0;
}

bool Player::isAIPlayerRepairUntouched() const
{
	return m_structuresInQueue == 0 && m_structuresToRepair[0] == INVALID_ID && m_structuresToRepair[1] == INVALID_ID &&
		m_repairDozer == INVALID_ID && m_repairDozerOrigin.x == 0.0f && m_repairDozerOrigin.y == 0.0f && m_repairDozerOrigin.z == 0.0f &&
		!m_dozerQueuedForRepair && !m_dozerIsRepairing && m_bridgeTimer == 0;
}

// RW 0x6AA671 (Player::repairStructure -> m_ai->repairStructure) -> RW 0x8F78EB (DONOR BFME2 0x004F2F66, AIPlayer::repairStructure):
// queues up to MAX_STRUCTURES_TO_REPAIR (2) damaged (getDamageState() != BODY_PRISTINE) structure IDs when not already in m_structuresToRepair.
void Player::repairStructure(GameLogic &logic, ObjectID structureID)
{
	if (m_type != PLAYER_COMPUTER)
	{
		return;
	}
	Object *structureObj = logic.findObjectByID(structureID);
	if (!structureObj)
	{
		return;
	}
	const BodyModuleInterface *body = structureObj->getBodyModule();
	if (!body || body->getDamageState() == BODY_PRISTINE)
	{
		return;
	}
	for (int i = 0; i < m_structuresInQueue; ++i)
	{
		if (m_structuresToRepair[(size_t)i] == structureID)
		{
			return;
		}
	}
	if (m_structuresInQueue < kMaxStructuresToRepair)
	{
		m_structuresToRepair[(size_t)m_structuresInQueue++] = structureID;
	}
}

// RW 0x8F55FD (DONOR BFME2 0x004F0F7C, AIPlayer::findDozer): walks logic.getFirstObject() (RW 0x97338F), keeps this player's DOZER objects with a
// DozerAIUpdate that are not forced into busy/wanting supply state when idle, not m_repairDozer, and have no pending DOZER_TASK_BUILD; prefers the
// closest idle (!isAnyTaskPending) dozer in 2D distance squared to `pos`, falling back to the last non-building dozer.
Object *Player::findDozerForRepair(GameLogic &logic, const Coord3D &pos) const
{
	static const int kDozer = ObjectTemplateInfoBuilder::kindOfIndex("DOZER");
	if (kDozer < 0)
	{
		return nullptr;
	}
	Object *anyDozer = nullptr;
	Object *bestIdleDozer = nullptr;
	float minDistSqr = 0.0f;
	for (Object *obj = logic.getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != this || !obj->isKindOf((unsigned)kDozer))
		{
			continue;
		}
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		DozerAIUpdate *dozerAI = dynamic_cast<DozerAIUpdate *>(ai);
		if (!dozerAI)
		{
			continue;
		}
		if (!dozerAI->isAnyTaskPending())
		{
			if (const SupplyTruckAIInterface *supplyTruckAI = ai->getSupplyTruckAIInterface())
			{
				if (supplyTruckAI->isForcedIntoBusyState() || supplyTruckAI->isForcedIntoWantingState())
				{
					continue;
				}
			}
		}
		if (obj->getID() == m_repairDozer || dozerAI->isTaskPending(DozerAIUpdate::DOZER_TASK_BUILD))
		{
			continue;
		}
		anyDozer = obj;
		if (!dozerAI->isAnyTaskPending())
		{
			const Coord3D *p = obj->getPosition();
			const float dx = SimMath::subf32(p->x, pos.x);
			const float dy = SimMath::subf32(p->y, pos.y);
			const float distSqr = SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy));
			if (!bestIdleDozer || minDistSqr > distSqr)
			{
				bestIdleDozer = obj;
				minDistSqr = distSqr;
			}
		}
	}
	return bestIdleDozer ? bestIdleDozer : anyDozer;
}

// RW 0x8F9705 (AIPlayer::update, jumped to by AISkirmishPlayer::update RW 0x8F3F5C) -> RW 0x8F8F68 (DONOR BFME2 0x004F418A, AIPlayer::updateBridgeRepair):
// skipped when TheSkirmishAIManager->findAI(this) is non-null (RW 0x8F9711 .. 0x8F9718); otherwise ticks m_bridgeTimer (reset to LOGICFRAMES_PER_SECOND = 5),
// drops destroyed queue entries, assigns a dozer via findDozerForRepair (RW 0x8F55FD) and sends CMD_FROM_AI repair (RW 0x66BBF1 -> RW 0x88E552), then once
// the structure reaches BODY_PRISTINE and the dozer has no task pending shifts the queue and moves the dozer back to m_repairDozerOrigin (RW 0x6F3C87 / 0x66C4CA).
void Player::updateAIPlayerRepair(GameLogic &logic)
{
	if (m_type != PLAYER_COMPUTER || m_structuresInQueue == 0 || logic.skirmishAI().findAI(m_index) != nullptr)
	{
		return;
	}
	--m_bridgeTimer;
	if (m_bridgeTimer > 0)
	{
		return;
	}
	m_bridgeTimer = LOGICFRAMES_PER_SECOND;
	while (m_structuresInQueue > 0 && !logic.findObjectByID(m_structuresToRepair[0]))
	{
		for (int i = 0; i < m_structuresInQueue - 1; ++i)
		{
			m_structuresToRepair[(size_t)i] = m_structuresToRepair[(size_t)(i + 1)];
		}
		m_structuresToRepair[(size_t)(m_structuresInQueue - 1)] = INVALID_ID;
		--m_structuresInQueue;
	}
	if (m_structuresInQueue == 0)
	{
		return;
	}
	Object *bridgeObj = logic.findObjectByID(m_structuresToRepair[0]);
	const BodyDamageType bridgeState = bridgeObj && bridgeObj->getBodyModule() ? bridgeObj->getBodyModule()->getDamageState() : BODY_PRISTINE;
	if (m_repairDozer == INVALID_ID)
	{
		m_dozerIsRepairing = false;
		if (m_dozerQueuedForRepair)
		{
			return;
		}
		Object *dozer = findDozerForRepair(logic, *bridgeObj->getPosition());
		if (dozer)
		{
			m_repairDozer = dozer->getID();
			m_repairDozerOrigin = *dozer->getPosition();
			if (DozerAIUpdate *dozerAI = dynamic_cast<DozerAIUpdate *>(dozer->getAIUpdateInterface()))
			{
				if (dozerAI->acceptCommand(CMD_FROM_AI, 0x13, bridgeObj))
				{
					dozerAI->repair(*bridgeObj);
				}
			}
			m_dozerIsRepairing = true;
			return;
		}
		m_dozerQueuedForRepair = true;
		return;
	}
	Object *dozer = logic.findObjectByID(m_repairDozer);
	if (!dozer)
	{
		m_repairDozer = INVALID_ID;
		m_bridgeTimer = 0;
		return;
	}
	DozerAIUpdate *dozerAI = dynamic_cast<DozerAIUpdate *>(dozer->getAIUpdateInterface());
	if (!dozerAI)
	{
		return;
	}
	if (m_dozerIsRepairing)
	{
		if (!dozerAI->isAnyTaskPending())
		{
			if (bridgeState == BODY_PRISTINE)
			{
				for (int i = 0; i < m_structuresInQueue - 1; ++i)
				{
					m_structuresToRepair[(size_t)i] = m_structuresToRepair[(size_t)(i + 1)];
				}
				m_structuresToRepair[(size_t)(m_structuresInQueue - 1)] = INVALID_ID;
				--m_structuresInQueue;
				m_dozerIsRepairing = false;
				if (m_structuresInQueue == 0)
				{
					Coord3D pos = m_repairDozerOrigin;
					if (dozerAI->world().mapReady())
					{
						dozerAI->world().pathfinder().adjustToPossibleDestination(dozerAI->adapter(), dozerAI->locomotorInfo(), &pos); // RW 0x6F3C87
					}
					dozerAI->aiMoveToPosition(pos, CMD_FROM_AI); // RW 0x66C4CA
					return;
				}
			}
		}
		else
		{
			return;
		}
	}
	if (dozerAI->acceptCommand(CMD_FROM_AI, 0x13, bridgeObj))
	{
		dozerAI->repair(*bridgeObj);
	}
	m_dozerIsRepairing = true;
}

namespace
{
// RW 0x8F6511 (DONOR BFME2 0x004F1B8C, AIPlayer::getPlayerStructureBounds): 2D min/max bounds of the player's team members with KindOf STRUCTURE.
void getPlayerStructureBounds(GameLogic &logic, int playerIndex, Coord2D &lo, Coord2D &hi)
{
	lo = Coord2D{ 0.0f, 0.0f };
	hi = Coord2D{ 0.0f, 0.0f };
	const Player *player = logic.players().getNthPlayer(playerIndex);
	if (!player)
	{
		return;
	}
	static const int kStructure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	if (kStructure < 0)
	{
		return;
	}
	Coord2D curLo{ 1000000.0f, 1000000.0f };
	Coord2D curHi{ -1000000.0f, -1000000.0f };
	bool found = false;
	for (const auto &proto : logic.players().teams().prototypes())
	{
		if (proto->getControllingPlayer() != player)
		{
			continue;
		}
		for (Team *team : proto->teams())
		{
			for (Object *o = team->getFirstMember(); o; o = o->friend_teamNext())
			{
				if (!o->isKindOf((unsigned)kStructure))
				{
					continue;
				}
				const Coord3D *p = o->getPosition();
				found = true;
				if (p->x < curLo.x) curLo.x = p->x;
				if (p->y < curLo.y) curLo.y = p->y;
				if (p->x > curHi.x) curHi.x = p->x;
				if (p->y > curHi.y) curHi.y = p->y;
			}
		}
	}
	if (found)
	{
		lo = curLo;
		hi = curHi;
	}
}

// RW 0x8F5BDB (DONOR BFME2 0x004F1256, AIPlayer::getPlayerSuperweaponValue): evaluates the build-cost-weighted density of enemy team members within
// max(searchRadius, 40.0f) in 2D, skipping airborne AIRCRAFT (RW 0x70C55D), scaling COMMANDCENTER by 0.1f and costs > 3000.0f by 0.1f.
int getPlayerSuperweaponValue(GameLogic &logic, const Coord3D &searchCenter, int playerIndex, float searchRadius)
{
	if (searchRadius < 40.0f)
	{
		searchRadius = 40.0f; // RW 0xBDD28C
	}
	const float radSqr = SimMath::mulf32(searchRadius, searchRadius);
	const Player *player = logic.players().getNthPlayer(playerIndex);
	if (!player)
	{
		return 0;
	}
	static const int kAircraft = ObjectTemplateInfoBuilder::kindOfIndex("AIRCRAFT");
	static const int kCommandCenter = ObjectTemplateInfoBuilder::kindOfIndex("COMMANDCENTER");
	float cash = 0.0f;
	for (const auto &proto : logic.players().teams().prototypes())
	{
		if (proto->getControllingPlayer() != player)
		{
			continue;
		}
		for (Team *team : proto->teams())
		{
			for (Object *o = team->getFirstMember(); o; o = o->friend_teamNext())
			{
				if (kAircraft >= 0 && o->isKindOf((unsigned)kAircraft) && (o->getAIUpdateInterface() && o->getAIUpdateInterface()->adapter().isSignificantlyAboveTerrain())) // RW 0x70C55D
				{
					continue;
				}
				const Coord3D *p = o->getPosition();
				const float dx = SimMath::subf32(searchCenter.x, p->x);
				const float dy = SimMath::subf32(searchCenter.y, p->y);
				const float d2 = SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy));
				if (d2 >= radSqr)
				{
					continue;
				}
				const float dist = (float)SimMath::sqrtd(d2);
				const float factor = SimMath::subf32(1.0f, SimMath::divf32(dist, SimMath::mulf32(2.0f, searchRadius)));
				float cost = o->getTemplate() ? SimMath::sseFromInt32(BuildAssistant::calcCostToBuild(*o->getTemplate(), player, nullptr, -1)) : 0.0f;
				if (kCommandCenter >= 0 && o->isKindOf((unsigned)kCommandCenter))
				{
					cost = SimMath::mulf32(cost, 0.1f); // RW 0xBD83D4
				}
				if (cost > 3000.0f) // RW 0xC1EE14
				{
					cost = SimMath::mulf32(cost, 0.1f);
				}
				cash = SimMath::addf32(cash, SimMath::mulf32(factor, cost));
			}
		}
	}
	return SimMath::cvttss2si(cash);
}
} // namespace

// RW 0x6AA5CA (Player::computeSuperweaponTarget) -> AISkirmishPlayer::computeSuperweaponTarget (RW 0x8F481C, DONOR BFME2 0x004EFFD2) /
// AIPlayer::computeSuperweaponTarget (RW 0x8F66BD, DONOR BFME2 0x004F1D38).
void Player::computeSuperweaponTarget(GameLogic &logic, const SpecialPowerTemplate *power, Coord3D &retPos, int enemyPlayerIndex, float weaponRadius) const
{
	if (m_type != PLAYER_COMPUTER)
	{
		return;
	}
	if (m_skirmish && power && power->getName() == "SuperweaponClusterMines")
	{
		// RW 0x8F4845 .. 0x8F4994: picks "Center" / "Flank" / "Backdoor" + (m_mpStartIndex + 1), finds closest waypoint on that path to m_baseCenter
		// ({0,0,0}) or falls back to enemy structure bounds center, then offsets by m_baseRadius (10.0f, RW 0xBD83D8).
		const int which = logic.random().getValue(0, 2, "AISkirmishPlayer.cpp", 0x35F);
		const std::string pathName = std::string(which == 0 ? "Center" : which == 1 ? "Flank" : "Backdoor") + std::to_string(m_mpStartIndex + 1);
		const Waypoint *bestWay = nullptr;
		float minDist = 0.0f;
		if (const TerrainLogic *terrain = logic.terrain())
		{
			for (const Waypoint &w : terrain->waypoints())
			{
				if (w.label1 == pathName || w.label2 == pathName || w.label3 == pathName)
				{
					const float d2 = SimMath::addf32(SimMath::mulf32(w.location.x, w.location.x), SimMath::mulf32(w.location.y, w.location.y));
					if (!bestWay || minDist > d2)
					{
						bestWay = &w;
						minDist = d2;
					}
				}
			}
		}
		Coord3D targetPos{ 0.0f, 0.0f, 0.0f };
		if (bestWay)
		{
			targetPos = bestWay->location;
		}
		else
		{
			Coord2D lo{}, hi{};
			getPlayerStructureBounds(logic, enemyPlayerIndex, lo, hi);
			targetPos.x = SimMath::mulf32(SimMath::addf32(lo.x, hi.x), 0.5f);
			targetPos.y = SimMath::mulf32(SimMath::addf32(lo.y, hi.y), 0.5f);
		}
		Coord2D offset{ targetPos.x, targetPos.y };
		const float len = (float)SimMath::sqrtd(SimMath::addf32(SimMath::mulf32(offset.x, offset.x), SimMath::mulf32(offset.y, offset.y)));
		if (len > 0.0f)
		{
			offset.x = SimMath::divf32(offset.x, len);
			offset.y = SimMath::divf32(offset.y, len);
		}
		retPos.x = SimMath::mulf32(offset.x, 10.0f);
		retPos.y = SimMath::mulf32(offset.y, 10.0f);
		retPos.z = logic.getGroundHeight(retPos.x, retPos.y);
		return;
	}
	// RW 0x8F66BD (AIPlayer::computeSuperweaponTarget): coarse grid scan (up to 10x10 at radius 2*r) over enemy structure bounds shrunk by r, then
	// fine 11x11 scan (step 0.1*r at radius r) averaging ties for the maximum superweapon value.
	Coord2D lo{}, hi{};
	getPlayerStructureBounds(logic, enemyPlayerIndex, lo, hi);
	if (weaponRadius < 1.0f)
	{
		weaponRadius = 1.0f; // RW 0xBD19D8
	}
	lo.x = SimMath::addf32(lo.x, weaponRadius);
	hi.x = SimMath::subf32(hi.x, weaponRadius);
	if (hi.x < lo.x)
	{
		lo.x = hi.x = SimMath::mulf32(SimMath::addf32(hi.x, lo.x), 0.5f);
	}
	lo.y = SimMath::addf32(lo.y, weaponRadius);
	hi.y = SimMath::subf32(hi.y, weaponRadius);
	if (hi.y < lo.y)
	{
		lo.y = hi.y = SimMath::mulf32(SimMath::addf32(hi.y, lo.y), 0.5f);
	}
	int xCount = (int)std::ceil((double)SimMath::divf32(SimMath::subf32(hi.x, lo.x), weaponRadius)) + 1;
	int yCount = (int)std::ceil((double)SimMath::divf32(SimMath::subf32(hi.y, lo.y), weaponRadius)) + 1;
	if (xCount > 10) xCount = 10;
	if (yCount > 10) yCount = 10;
	Coord3D bestPos{ 0.0f, 0.0f, 0.0f };
	int cash = -1;
	for (int i = 0; i < xCount; ++i)
	{
		for (int j = 0; j < yCount; ++j)
		{
			Coord3D pos{
				SimMath::addf32(lo.x, SimMath::divf32(SimMath::mulf32(SimMath::subf32(hi.x, lo.x), SimMath::sseFromInt32(i)), SimMath::sseFromInt32(xCount))),
				SimMath::addf32(lo.y, SimMath::divf32(SimMath::mulf32(SimMath::subf32(hi.y, lo.y), SimMath::sseFromInt32(j)), SimMath::sseFromInt32(yCount))),
				0.0f,
			};
			const int curCash = getPlayerSuperweaponValue(logic, pos, enemyPlayerIndex, SimMath::mulf32(2.0f, weaponRadius));
			if (curCash > cash)
			{
				cash = curCash;
				bestPos = pos;
			}
		}
	}
	Coord3D veryBestPos{ 0.0f, 0.0f, 0.0f };
	int count = 1;
	cash = -1;
	for (int i = 0; i < 11; ++i)
	{
		for (int j = 0; j < 11; ++j)
		{
			Coord3D pos{
				SimMath::addf32(bestPos.x, SimMath::mulf32(SimMath::sseFromInt32(i - 5), SimMath::mulf32(weaponRadius, 0.1f))),
				SimMath::addf32(bestPos.y, SimMath::mulf32(SimMath::sseFromInt32(j - 5), SimMath::mulf32(weaponRadius, 0.1f))),
				0.0f,
			};
			const int curCash = getPlayerSuperweaponValue(logic, pos, enemyPlayerIndex, weaponRadius);
			if (curCash > cash)
			{
				cash = curCash;
				veryBestPos = pos;
				count = 1;
			}
			else if (curCash == cash)
			{
				veryBestPos.x = SimMath::addf32(veryBestPos.x, pos.x);
				veryBestPos.y = SimMath::addf32(veryBestPos.y, pos.y);
				++count;
			}
		}
	}
	if (count > 1)
	{
		veryBestPos.x = SimMath::divf32(veryBestPos.x, SimMath::sseFromInt32(count));
		veryBestPos.y = SimMath::divf32(veryBestPos.y, SimMath::sseFromInt32(count));
	}
	veryBestPos.z = logic.getGroundHeight(veryBestPos.x, veryBestPos.y);
	retPos = veryBestPos;
}
