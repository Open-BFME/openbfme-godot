// OpenBFME. GPL-3.0.
// See Common/Player.h.

#include "Common/Player.h"
#include "Common/NumericState.h"
#include "GameLogic/SimMath.h"

#include "Common/PlayerEconomyHost.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
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
	m_colorExplicit = false;
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
	h.addBool(m_colorExplicit);
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
	h.addU32((std::uint32_t)m_upgradeRecords.size()); // UPGRADE-1: the records in list order, then both masks
	for (const UpgradeRecord &r : m_upgradeRecords)
	{
		h.addI32(r.upgrade ? r.upgrade->getMaskBit() : -1);
		h.addI32(r.status);
	}
	for (std::uint32_t w : m_upgradesInProgress.words)
	{
		h.addU32(w);
	}
	for (std::uint32_t w : m_upgradesCompleted.words)
	{
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
			++m_upgradeEvaNotPlayed; // RW 0x6AE4CA .. 0x6AE53C: the local / allied / enemy gain Eva event (TheEva, RW 0x5DD9EE): audio lane
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
		++m_upgradeEvaNotPlayed; // RW 0x6AE59B: the lose Eva event
	}
}
