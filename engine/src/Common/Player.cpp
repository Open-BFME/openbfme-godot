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
#include "Common/Xfer.h"
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
	XferVersion v{ 1, 2 }; // lane LUMBER1: version 2 adds the ResourceGatheringManager (RW + 0x304) at the end
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
}
