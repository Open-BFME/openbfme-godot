// OpenBFME. GPL-3.0.
// See GameLogic/Economy.h.

#include "GameLogic/Economy.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/EconomyStops.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <stdexcept>
#include <variant>

namespace
{
const size_t kIncomeLogLimit = 8192;

struct Bits
{
	int selectable, structure, horde, armyOfDead, mine;
	int underConstruction, pendingConstruction, sold, temporarilyDefected;
	Bits()
		: selectable(ObjectTemplateInfoBuilder::kindOfIndex("SELECTABLE"))
		, structure(ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE"))
		, horde(ObjectTemplateInfoBuilder::kindOfIndex("HORDE"))
		, armyOfDead(ObjectTemplateInfoBuilder::kindOfIndex("ARMY_OF_DEAD"))
		, mine(ObjectTemplateInfoBuilder::kindOfIndex("MINE"))
		, underConstruction(ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION"))
		, pendingConstruction(ObjectTemplateInfoBuilder::objectStatusIndex("PENDING_CONSTRUCTION"))
		, sold(ObjectTemplateInfoBuilder::objectStatusIndex("SOLD"))
		, temporarilyDefected(ObjectTemplateInfoBuilder::objectStatusIndex("TEMPORARILY_DEFECTED"))
	{
	}
};
const Bits &bits()
{
	static const Bits b;
	return b;
}
bool kindOf(const Object &o, int bit)
{
	return bit >= 0 && o.isKindOf((unsigned)bit);
}
bool status(const Object &o, int bit)
{
	return bit >= 0 && o.testStatus((unsigned)bit);
}
} // namespace

Economy::Economy(GameLogic &logic)
	: m_logic(logic)
	, m_resources(logic)
{
	logic.players().setEconomyHost(this);
}

Economy::~Economy()
{
	m_logic.players().setEconomyHost(nullptr);
}

void Economy::reset()
{
	m_resources.clearAll();
	m_incomeLog.clear();
	m_droppedExperience = 0;
	m_incomePayments = 0;
}

void Economy::setScoring(bool on)
{
	m_context.scoring = on;
	PlayerList &pl = m_logic.players();
	for (int i = 0; i < pl.getPlayerCount(); ++i)
	{
		pl.getNthPlayer(i)->getScoreKeeper().setEnabled(on);
	}
}

long long Economy::templateInt(const ThingTemplate &tt, const char *field)
{
	const FieldValue *v = tt.getFinalOverride()->findField(field);
	if (!v)
	{
		return 0;
	}
	if (const long long *i = std::get_if<long long>(v))
	{
		return *i;
	}
	throw std::logic_error(std::string("ThingTemplate ") + tt.getName() + " field " + field + " is not an integer");
}

bool Economy::templateKindOf(const ThingTemplate &tt, const char *kindOfName) const
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(kindOfName);
	if (bit < 0)
	{
		return false;
	}
	const KindOfMaskType &mask = m_logic.templateInfo(tt.getFinalOverride()).kindOf;
	return MaskTest(mask, (unsigned)bit);
}

int Economy::livePlayableCount(bool withTerritoryCounters) const
{
	// RW 0x6A8630: 20 slots; the PlayerList of this port is dense, the retail slots past the last player are empty
	int count = 0;
	PlayerList &pl = m_logic.players();
	for (int i = 0; i < pl.getPlayerCount() && i < 20; ++i)
	{
		const Player *p = pl.getNthPlayer(i);
		const PlayerTemplate *pt = p ? p->getPlayerTemplate() : nullptr;
		if (!p || !pt || !pt->m_playableSide || p->isObserver() || p->isDefeated())
		{
			continue;
		}
		if (withTerritoryCounters)
		{
			count = CpMath::add(count, CpMath::add(p->commandPoints().getTerritoryEvil(), p->commandPoints().getTerritoryGood())); // RW + 0x1C + + 0x18
		}
		count = CpMath::add(count, 1);
	}
	return count;
}

float Economy::multiPlayMoneyMult(int livePlayers) const
{
	const int i = CpMath::sub(livePlayers, 1); // RW 0x642002: dec eax; js; cmp 0x14; jge -> 1.0
	if (i < 0 || i >= 20)
	{
		return 1.0f;
	}
	return m_settings.multiPlayMoneyMult[i];
}

bool Economy::isMultiplayerGame() const
{
	// RW 0x625456: the game mode is LAN, internet or skirmish (the replay clause of RW's reads the recorded game's mode, not ported: S-254)
	const int m = m_context.gameMode;
	return m == EconomyContext::MODE_LAN || m == EconomyContext::MODE_INTERNET || m == EconomyContext::MODE_SKIRMISH;
}

bool Economy::isMultiplayerCommandPointGame() const
{
	// RW 0x6254A1: a Living World game is not one; otherwise the same modes as isMultiplayerGame
	if (m_context.livingWorld)
	{
		return false;
	}
	return isMultiplayerGame();
}

int Economy::applyIncomeMultipliers(const Player &owner, float amountF) const
{
	if (isMultiplayerGame())
	{
		const float mult = multiPlayMoneyMult(livePlayableCount(false));
		amountF = NumericState::fstpDword(NumericState::pc24MulW((double)mult, (double)amountF)); // fld mult; fmul amount; fstp dword
	}
	return applyHandicapMoney(owner, NumericState::cvttss2si(amountF));
}

int Economy::awardBounty(Player &killerOwner, Object *killer, const Object &victim)
{
	if (!killer)
	{
		return 0; // RW 0x6AC07F
	}
	const Bits &b = bits();
	if (status(victim, b.underConstruction))
	{
		return 0; // RW 0x6AC09B: status bit 2
	}
	// RW 0x68DDF1 Object::getBountyValue
	const ThingTemplate &tt = *victim.getTemplate();
	int value = (int)templateInt(tt, "BountyValue");
	if (templateKindOf(tt, "HERO"))
	{
		int num = 0, den = 0;
		if (m_heroBountyScale && m_heroBountyScale(victim, num, den) && den != 0)
		{
			double x = NumericState::pc24DivW((double)num, (double)den); // fild n; fidiv d
			x = NumericState::pc24MulW(x, (double)value);                 // fmul value
			x = NumericState::pc24AddW(x, (double)value);                 // fadd value
			value = (int)NumericState::ftol2Low32(x);                     // _ftol2
		}
		else
		{
			++m_unscaledHeroBounties;
		}
	}
	// RW 0x6AC0B5 .. 0x6AC0D9: the killer's BOUNTY_PERCENTAGE sum, the player's own percent when it is 0.0 (an unordered value counts as non-zero: the jp of RW 0x6AC0D1)
	float percent = attributeModifierSum(*killer, ATTRIBUTE_MODIFIER_BOUNTY_PERCENTAGE);
	if (percent == 0.0f)
	{
		percent = killerOwner.getBountyPercent();
	}
	// RW 0x6AC0E3 .. 0x6AC109: fild dword; (a negative value) fadd [2^32] AT 24 BITS before the fmul (NumericState::fildU32 keeps that rounding), fmul percent, the MSVCR71
	// ceil, fstp dword, fistp
	const double wide = NumericState::pc24MulW(NumericState::fildU32((std::uint32_t)value), (double)percent);
	int amount = NumericState::fistp32((double)NumericState::fstpDword(NumericState::ceilD(wide)));
	if (isMultiplayerGame())
	{
		// RW 0x6AC115 .. 0x6AC155
		const float mult = multiPlayMoneyMult(livePlayableCount(false));
		// RW 0x6AC13C .. 0x6AC14C: fild amount; fadd 2^32 (PC24) when negative; fmul mult; _ftol2
		amount = (int)NumericState::ftol2Low32(NumericState::pc24MulW(NumericState::fildU32((std::uint32_t)amount), (double)mult));
	}
	amount = applyHandicapMoney(killerOwner, amount);
	if (amount == 0)
	{
		return 0;
	}
	killerOwner.depositMoney((std::uint32_t)amount, true);
	noteIncome(*killer, killerOwner, amount);
	return amount;
}

int Economy::applyHandicapMoney(const Player &, int amount) const
{
	// RW 0x6AA858: the handicap acts when GameLogic + 0x114 != 3 or the Living World flag is set (RW 0x602E64); the slot's percentage (RW 0x6E1F2F) is a campaign
	// difficulty value that this build has no source for (stop S-254). In the skirmish game (kind 3, no Living World) the retail function is the identity.
	if (m_context.gameKind == 3 && !m_context.livingWorld)
	{
		return amount;
	}
	return amount; // reported by report(): the campaign handicap is not applied
}

// ---- command points ---------------------------------------------------------------------------------------------------------------
void Economy::initCommandPoints(Player &player)
{
	const int index = player.getPlayerIndex();
	if (index < 0 || index >= 20)
	{
		return; // RW 0x6A7C9A: the index must be 0 .. 19
	}
	if (!m_settings.loaded)
	{
		throw std::logic_error("Economy::initCommandPoints: GameData's economy values are not loaded (EconomySettings::load)");
	}
	const bool evil = player.getPlayerTemplate() ? player.getPlayerTemplate()->m_evil : false; // RW 0x6AA432: template + 0x1BC
	const bool kind3 = m_context.gameKind == 3;
	const bool mp = isMultiplayerCommandPointGame();
	CommandPointsSource src;
	if (kind3 && mp)
	{
		// RW 0x6A7E11 .. 0x6A7ECC: the MPn pair of the live player count (with the territory counters), n >= 8 -> MP8, ... n == 3 -> MP3, else MP2
		const int n = livePlayableCount(true);
		const int slot = n >= 8 ? 6 : n >= 7 ? 5 : n >= 6 ? 4 : n >= 5 ? 3 : n == 4 ? 2 : n == 3 ? 1 : 0;
		const EconomySettings::Pair &good = m_settings.goodMP[slot];
		const EconomySettings::Pair &bad = m_settings.evilMP[slot];
		const EconomySettings::Pair &chosen = evil ? bad : good;
		src.start = chosen.start;
		src.cap = chosen.cap;
		src.territoryEvilTerm = CpMath::mul(player.commandPoints().getTerritoryEvil(), bad.start);
		src.territoryGoodTerm = CpMath::mul(player.commandPoints().getTerritoryGood(), good.start);
	}
	else if (!kind3 && mp)
	{
		// RW 0x6A7CEB: the Living World branch chooses the MP2 pair and adds the world's own terms (not ported: S-254)
		const EconomySettings::Pair &chosen = evil ? m_settings.evilMP[0] : m_settings.goodMP[0];
		src.start = chosen.start;
		src.cap = chosen.cap;
	}
	else
	{
		// RW 0x6A7ED1: a human player of a campaign game, or a computer player
		const EconomySettings::Pair &chosen = player.getPlayerType() == PLAYER_HUMAN ? (evil ? m_settings.evilSolo : m_settings.goodSolo) : (evil ? m_settings.evilAI : m_settings.goodAI);
		src.start = chosen.start;
		src.cap = chosen.cap;
	}
	// RW 0x6A7DE2: TheGameInfo exists and the kind is 3: the lobby's percentage scales the cap
	if (m_context.gameInfoPresent && kind3)
	{
		src.applyLobbyPercent = true;
		src.lobbyPercent = m_context.lobbyCommandPointPercent;
	}
	player.commandPoints().init(index, src);
}

void Economy::initAllCommandPoints()
{
	PlayerList &pl = m_logic.players();
	for (int i = 0; i < pl.getPlayerCount() && i < 20; ++i)
	{
		initCommandPoints(*pl.getNthPlayer(i));
	}
}

int Economy::commandPointUsageOf(const Object &obj) const
{
	// RW 0x6A7FAA
	const Bits &b = bits();
	if (!kindOf(obj, b.selectable) || kindOf(obj, b.structure) || kindOf(obj, b.horde))
	{
		return 0;
	}
	return (int)templateInt(*obj.getTemplate(), "CommandPoints");
}

int Economy::commandPointBonusOf(const Object &obj) const
{
	// RW 0x6A7C01: CommandPointBonus + the COMMAND_POINT_BONUS attribute modifier (a float sum), cvttss2si
	int bonus = (int)templateInt(*obj.getTemplate(), "CommandPointBonus");
	if (m_attributeSums)
	{
		float sum = 0.0f;
		if (m_attributeSums(obj, ATTRIBUTE_MODIFIER_COMMAND_POINT_BONUS, sum))
		{
			bonus = NumericState::cvttss2si(NumericState::sseAdd(NumericState::sseFromInt32(bonus), sum));
		}
	}
	return bonus;
}

void Economy::objectGained(Player &player, const Object &obj)
{
	// RW 0x6AA56D
	if (templateInt(*obj.getTemplate(), "CommandPointBonus") > 0)
	{
		player.commandPoints().addBonus(commandPointBonusOf(obj));
	}
	else
	{
		player.commandPoints().addUsage(commandPointUsageOf(obj));
	}
}

void Economy::objectLost(Player &player, const Object &obj)
{
	// RW 0x6AA590
	if (templateInt(*obj.getTemplate(), "CommandPointBonus") > 0)
	{
		player.commandPoints().removeBonus(commandPointBonusOf(obj));
	}
	else
	{
		player.commandPoints().removeUsage(commandPointUsageOf(obj));
	}
}

int Economy::commandPointLimit(const Player &player) const
{
	return player.commandPoints().getLimit([&](const ObjectFilter &f) { return hasObjectMatching(player, f, true); });
}

bool Economy::canAffordCommandPoints(const Player &player, const ThingTemplate &tt) const
{
	const int cp = (int)templateInt(tt, "CommandPoints");
	if (cp == 0)
	{
		return true; // RW 0x6A7F7F: test edi, edi; je -> true (before the limit is even computed)
	}
	if (!player.commandPoints().initialized())
	{
		throw std::logic_error("command points of player " + player.getPlayerName() + " were never initialised (Economy::initCommandPoints)");
	}
	return player.commandPoints().canAfford(cp, templateKindOf(tt, "ARMY_OF_DEAD"), commandPointLimit(player)); // RW 0x6A7F99: ARMY_OF_DEAD = KindOf bit 151 (tt + 0x11A & 0x80)
}

// ---- what the players ask the world ---------------------------------------------------------------------------------------------
void Economy::updateUpgradeModulesOfPlayer(const Player &player)
{
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &player)
		{
			o->updateUpgradeModules();
		}
	}
}

void Economy::removeUpgradeFromObjectsOfPlayer(const Player &player, const UpgradeTemplate &upgrade)
{
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &player)
		{
			o->removeUpgrade(&upgrade);
		}
	}
}

bool Economy::hasObjectMatching(const Player &player, const ObjectFilter &filter, bool completedOnly) const
{
	// RW 0x6ABD0B -> 0x7A19AB -> 0x7A075A over the player's teams' members
	const Bits &b = bits();
	for (const Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != &player)
		{
			continue;
		}
		if (o->isEffectivelyDead() || o->isDestroyed() || kindOf(*o, b.mine)) // RW 0x7A075A skips the effectively dead ([object + 0x458] bit 0), the destroyed and the mines
		{
			continue;
		}
		if (completedOnly && kindOf(*o, b.structure) && (status(*o, b.underConstruction) || status(*o, b.pendingConstruction)))
		{
			continue; // retail tests the AWAITING_CONSTRUCTION / PARTIALLY_CONSTRUCTED model conditions: approximated by the construction statuses (S-253)
		}
		if (ObjectFilterMatch::allows(m_logic, filter, *o, nullptr))
		{
			return true;
		}
	}
	return false;
}

int Economy::countOwnedObjectsAllowedBy(const Player &player, const ObjectFilter &filter) const
{
	const Bits &b = bits();
	int n = 0;
	for (const Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != &player || status(*o, b.underConstruction))
		{
			continue;
		}
		if (ObjectFilterMatch::allows(m_logic, filter, *o, nullptr))
		{
			++n;
		}
	}
	return n;
}

float Economy::productionCostChange(const Player &player, const ThingTemplate &tt, bool slaughter) const
{
	// RW 0x6AD8A7: walk the player's CostModifierUpgrade entries in order; every entry that applies overwrites the value with the percentage at the index of the
	// number of applying entries so far
	const Bits &b = bits();
	size_t index = 0;
	float value = 0.0f;
	for (const Player::CostModifier &entry : player.costModifiers())
	{
		const Object *source = m_logic.findObjectByID(entry.source);
		if (!source)
		{
			continue;
		}
		// RW 0x6AD8F3: model condition 0x44 (PARTIALLY_CONSTRUCTED), approximated by the construction status; 0x6AD900: status SOLD
		if (status(*source, b.underConstruction) || status(*source, b.sold))
		{
			continue;
		}
		if (!slaughter || !entry.slaughter)
		{
			// RW 0x6AD917: the filter decides; the module data's default filter is NONE (RW 0x8B9E08), which allows nothing; an unset handle would be the default (ALL)
			const ObjectFilter unset;
			const bool allowed = ObjectFilterMatch::allows(m_logic, entry.filter ? *entry.filter : unset, &tt, nullptr, nullptr);
			if (!allowed)
			{
				continue;
			}
		}
		if (index < entry.percents.size())
		{
			value = entry.percents[index];
		}
		++index;
	}
	return NumericState::pc24Add(value, 1.0f); // fld value; fadd 1.0
}

// ---- seams -----------------------------------------------------------------------------------------------------------------------
float Economy::attributeModifierProduct(const Object &obj, int type) const
{
	float product = 1.0f; // RW 0x805007
	if (m_attributeModifiers)
	{
		m_attributeModifiers(obj, type, product);
	}
	return product;
}

bool Economy::objectHasModelCondition(const Object &obj, const char *modelCondition) const
{
	return m_modelConditions ? m_modelConditions(obj, modelCondition) : false;
}

float Economy::attributeModifierSum(const Object &obj, int type) const
{
	float sum = 0.0f;
	if (m_attributeSums)
	{
		m_attributeSums(obj, type, sum);
	}
	return sum;
}

void Economy::grantBuildingExperience(Object &obj, float points)
{
	if (m_experience)
	{
		m_experience(obj, points);
	}
	else
	{
		++m_droppedExperience;
	}
}

void Economy::noteIncome(Object &obj, Player &owner, int amount)
{
	++m_incomePayments;
	IncomeEvent e;
	e.object = obj.getID();
	e.playerIndex = owner.getPlayerIndex();
	e.amount = amount;
	e.frame = m_logic.getFrame();
	m_incomeLog.push_back(e);
	if (m_incomeLog.size() > kIncomeLogLimit)
	{
		m_incomeLog.pop_front();
	}
	if (m_incomeListener)
	{
		m_incomeListener(e);
	}
}

void Economy::crc(StateHasher &h) const
{
	m_settings.crc(h);
	m_context.crc(h);
	m_resources.crc(h);
}

std::vector<std::string> Economy::report() const
{
	std::vector<std::string> lines = EconomyStops::lines();
	if (!hasAttributeModifierProvider())
	{
		lines.push_back("[S-253] no AttributeModifier provider is installed: no PRODUCTION / COMMAND_POINT_BONUS / BOUNTY_PERCENTAGE modifier can be active");
	}
	if (m_droppedExperience != 0)
	{
		lines.push_back("[S-256] " + std::to_string(m_droppedExperience) + " experience grants of resource buildings were dropped (no ExperienceTracker hook installed)");
	}
	if (m_unscaledHeroBounties != 0)
	{
		lines.push_back("[S-261] " + std::to_string(m_unscaledHeroBounties) + " bounties of HERO victims were paid at the unscaled BountyValue (no experience level scale hook installed)");
	}
	return lines;
}
