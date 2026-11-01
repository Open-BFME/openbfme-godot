// OpenBFME. GPL-3.0.
//
// LivingWorldLogic's save and load (lane WOTR2 milestone 3): the strategic game as the snapshot block "obfme:LivingWorld" (LivingWorldLogic.h).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp's byte-matched sources are tier A for each):
//   * the snapshot CHUNK_LivingWorldLogic is RW 0x6BC653 (BFME2 0x6BC653's twin, LivingWorldLogicSnapshotRva002BC653.cpp, version 6): the selected
//     army / building / build plot / spawn / player ids, then the game's save RW 0x6BB5DD (version 3) or its load RW 0x6BBA45, the campaign manager,
//     the flag + 0xA9, the turn phase object, GameLogic's part, the LivingWorldManager and the LivingWorld object, the counter + 0xF0.
//   * the game's shared state RW 0x6BBD55 (LivingWorldLogicXferStateRva002BB2B7.cpp, version 7): the region id + 0xB8, the planning flags, the game
//     difficulty, the counters + 0x100 / + 0x104 (the phase-3 counter, canAdvance RW 0x6B5F78), then the players (XferPlayers), the owned armies
//     (+ 0x124), the two module vectors + 0x10C / + 0x118, the turn phase + 0xF4 and the previous phase + 0xF8, the battle flags + 0x108 / + 0x109,
//     the delayed region victories and the region manager (RW 0x6B9CFC).
// OPENBFME: the model (LivingWorldLogic.h) holds the same state in other shapes (players, regions with their buildings and queues, armies with their
// members, battles, by index), so the block is the native dialect and keeps retail's order where the model has the field: the game's numbers first
// (turn, phase, the phase-3 counter), then the players, the armies, the regions and the battles. Everything the state hash covers is saved, plus
// the event log (the HUD's notices); the data the world rebuilds (the ranks, the RTS templates' rank fields, ResourceMultiplierLimit) is not.

#include "GameLogic/LivingWorld/LivingWorldLogic.h"

#include "GameLogic/LivingWorld/LivingWorldAI.h"

#include "Common/GameState.h"
#include "Common/Snapshot.h"
#include "Common/Xfer.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace LivingWorld
{

namespace
{

void setError(std::string *error, const std::string &text)
{
	if (error)
	{
		*error = text;
	}
}

void xferIntField(Xfer &x, int *v)
{
	std::int32_t w = x.isLoading() ? 0 : (std::int32_t)*v;
	x.xferInt(&w);
	*v = (int)w;
}

// a vector's size (Xfer::xferCount's rule: the fewest bytes one entry encodes to); on load the vector is emptied and resized
template <typename T>
void xferSize(Xfer &x, std::vector<T> &v, size_t minEntryBytes)
{
	x.xferVectorSize(v, minEntryBytes);
}

// WOTR2 milestone 5: a score keeper (LivingWorldScoreKeeper's xfer RW 0x8F3A89, its order where the model has the field; the wall clock and the
// real-time battles' statistics are the host's)
void xferIntVector(Xfer &x, std::vector<int> &v)
{
	std::uint32_t n = (std::uint32_t)v.size();
	x.xferCount(&n, 4, sizeof(int));
	if (x.isLoading())
	{
		v.assign(n, 0);
	}
	for (int &i : v)
	{
		xferIntField(x, &i);
	}
}

void xferScoreKeeper(Xfer &x, ScoreKeeper &sk)
{
	xferSize(x, sk.perTurn, 6);
	for (ScoreKeeper::PerTurn &t : sk.perTurn)
	{
		x.xferShort(&t.territories);
		x.xferShort(&t.units);
		x.xferShort(&t.structures);
	}
	xferIntVector(x, sk.regionUnifiedTurns);
	xferIntVector(x, sk.fortressTurns);
	std::uint32_t n = (std::uint32_t)sk.rulesHeld.size();
	x.xferCount(&n, 1, 1);
	if (x.isLoading())
	{
		sk.rulesHeld.assign(n, false);
	}
	for (size_t i = 0; i < sk.rulesHeld.size(); ++i)
	{
		bool b = sk.rulesHeld[i];
		x.xferBool(&b);
		sk.rulesHeld[i] = b;
	}
	xferIntField(x, &sk.defeatTurn);
	xferIntField(x, &sk.lastTurn);
	for (int &b : sk.buildingsBuilt)
	{
		xferIntField(x, &b);
	}
	xferIntField(x, &sk.structuresLost);
	xferIntField(x, &sk.structuresDestroyed);
	std::uint32_t m = (std::uint32_t)sk.recruited.size();
	x.xferCount(&m, 1 + 4, 1 + 4);
	if (x.isLoading())
	{
		sk.recruited.clear();
		for (std::uint32_t i = 0; i < m; ++i)
		{
			std::string name;
			int count = 0;
			x.xferAsciiString(&name);
			xferIntField(x, &count);
			if (!sk.recruited.emplace(name, count).second)
			{
				throw XferException("a score keeper lists the recruited template '" + name + "' twice");
			}
		}
	}
	else
	{
		for (auto &e : sk.recruited)
		{
			std::string name = e.first;
			x.xferAsciiString(&name);
			xferIntField(x, &e.second);
		}
	}
	xferIntField(x, &sk.unitsLostAutoResolve);
	xferIntField(x, &sk.heroesLostAutoResolve);
	xferIntField(x, &sk.unitsKilledAutoResolve);
	xferIntField(x, &sk.autoResolveWon);
	xferIntField(x, &sk.autoResolveLost);
	xferIntField(x, &sk.territoriesConquered);
	xferIntField(x, &sk.territoriesLost);
	xferIntField(x, &sk.regionsLost);
	xferIntField(x, &sk.rtsBattlesWon);
	xferIntField(x, &sk.rtsBattlesLost);
	x.xferBool(&sk.detached);
}

class LivingWorldSnapshot : public Snapshot
{
public:
	explicit LivingWorldSnapshot(LivingWorldLogic &logic) : m_logic(logic) {}
	void crc(Xfer *xfer) override { m_logic.xfer(*xfer); }
	void xfer(Xfer *xfer) override { m_logic.xfer(*xfer); }
	void loadPostProcess() override {}

private:
	LivingWorldLogic &m_logic;
};

// the fewest bytes of each entry (its fixed fields, every string and nested vector empty: a string is a 1-byte length, a vector a 4-byte count)
constexpr size_t kRevivalMin = 1 + 1 + 4 + 1;
constexpr size_t kPlayerMin = 1 + 1 + 1 + 1 + 4 * 8 + 1 + 1 + 4 + 4 + 4 + 4 + 4 * 3 + (4 * 4 + 4 * 2 + 4 * 5 + 4 * 2 + 4 + 4 * 10 + 1);
constexpr size_t kMemberMin = 1 + 1 + 4 + 4 + 4 * 2 + 4 + 4 * 2;
constexpr size_t kArmyMin = 4 * 5 + 5 + 1 + 1 + 4 + 4;
constexpr size_t kBuildingMin = 1 + 4 * 4 + 1 + 4;
constexpr size_t kOrderMin = 4 * 2;
constexpr size_t kRegionMin = 4 * 2 + 1 + 4 + 4;
constexpr size_t kBattleMin = 4 * 6 + 4 + 4 + 4 + 1;
constexpr size_t kSideMin = 4 + 4;
constexpr size_t kSidePlayerMin = 4 * 5;
constexpr size_t kEventMin = 4 * 2 + 1;

} // namespace

void LivingWorldLogic::xfer(Xfer &x)
{
	XferVersion v{ 5, 7 }; // 2 (WOTR2 milestone 4): building ids, the entry keys and homes, the retreat list; 3: the command point counters;
	                       // 4 (milestone 5): the entries' upgrade sets, ranks and marks, the armories' counts; 5 (milestone 7): the players' sciences
	                       // and upgrades (LivingWorldPlayer + 0x1F8 / + 0x204); 6 (milestone 8 step 4): the network clock L + 0x100 / + 0x104 (a LAN
	                       // war's save, retail's mode 6). A version-5 save loads as before: the clock keeps the values a fresh logic has (0, 0);
	                       // 7 (Sol wotr2m8s4 follow-up 1): the save request's serial (a version-6 save loads with serial 0 and no pending request)
	x.xferVersion(v);

	std::string campaign = m_campaign ? m_campaign->name : std::string();
	x.xferAsciiString(&campaign);
	if (x.isLoading())
	{
		m_campaign = m_stores.findCampaign(campaign);
		if (!m_campaign || !m_campaign->scenario)
		{
			throw XferException("no LivingWorldCampaign with a Scenario named '" + campaign + "'");
		}
		m_regionCampaign = m_stores.findRegionCampaign(m_campaign->scenario->regionCampaign);
		if (!m_regionCampaign)
		{
			throw XferException("the scenario's RegionCampaign '" + m_campaign->scenario->regionCampaign + "' is not defined");
		}
	}
	x.xferUnsignedInt(&m_seed);
	xferIntField(x, &m_victoryType);
	// the shared state's numbers (RW 0x6BBD55: the counters + 0x100 / + 0x104 before the players; the phases + 0xF4 / + 0xF8 come later in retail)
	xferIntField(x, &m_turn);
	xferIntField(x, &m_phase);
	xferIntField(x, &m_settleCounter);
	xferIntField(x, &m_nextArmyId);
	xferIntField(x, &m_nextBuildingId);
	if (v.current >= 6)
	{
		// RW 0x6BBD55 writes the counters + 0x100 / + 0x104 with the shared numbers: the strategic frame and the frame the phase began (the phase timer
		// of a resumed LAN war goes on from where it was saved)
		x.xferUnsignedInt(&m_frame);
		x.xferUnsignedInt(&m_phaseStart);
		x.xferUnsignedInt(&m_refusedOrders); // hashed in a network war: a resumed war's hash check holds it
		// the save request (GameLogic + 0x11D) and the leave-for-now vote: kept with the clock (a save is ordered when the request is answered, so a
		// LAN war's own save holds none pending; a vote, though, is open in it)
		xferIntField(x, &m_saveAsker);
		std::uint32_t n = (std::uint32_t)m_saveName.size();
		x.xferCount(&n, 2, sizeof(char16_t));
		if (n > kSaveNameMax)
		{
			throw XferException("the save name is " + std::to_string(n) + " characters (at most " + std::to_string(kSaveNameMax) + ")");
		}
		if (x.isLoading())
		{
			m_saveName.assign(n, u'\0');
		}
		for (char16_t &c : m_saveName)
		{
			std::uint16_t u = (std::uint16_t)c;
			x.xferUnsignedShort(&u);
			c = (char16_t)u;
		}
		std::string why;
		if (x.isLoading() && !m_saveName.empty() && !saveNameValid(m_saveName, &why))
		{
			throw XferException(why); // the request's name as the message check takes it
		}
		x.xferBool(&m_leaveVoteOpen);
		std::uint32_t votes = (std::uint32_t)m_leaveVotes.size();
		x.xferCount(&votes, 4, sizeof(int));
		if (votes > 64)
		{
			throw XferException("the leave vote has " + std::to_string(votes) + " players");
		}
		if (x.isLoading())
		{
			m_leaveVotes.assign(votes, -1);
		}
		for (int &v : m_leaveVotes)
		{
			xferIntField(x, &v);
		}
		x.xferBool(&m_warEndAgreed);
		if (x.isLoading() && (m_saveAsker < -1 || m_saveAsker >= 64 || (m_leaveVoteOpen && votes == 0)))
		{
			throw XferException("the save request / leave vote fields are out of range (asker " + std::to_string(m_saveAsker) + ")");
		}
		if (v.current >= 7)
		{
			x.xferUnsignedInt(&m_saveRequestSerial); // not hashed: a version-6 LAN save (its state hash without it) still resumes
		}
		else if (x.isLoading())
		{
			// a version-6 save: its request has no serial, so a pending one is dropped (a LAN war's own save holds none: it is written on the Yes); its
			// leave vote stays as saved (the resume closes it after the hash check, resumeAfterLanLoad) and follows serial 0
			m_saveRequestSerial = 0;
			if (m_saveAsker >= 0)
			{
				m_saveAsker = -1;
				m_saveName.clear();
			}
		}
	}
	{
		std::uint32_t n = (std::uint32_t)m_retreatList.size();
		x.xferCount(&n, 4, sizeof(int));
		if (x.isLoading())
		{
			m_retreatList.assign(n, 0);
		}
		for (int &id : m_retreatList)
		{
			xferIntField(x, &id);
		}
	}
	x.xferBool(&m_started);
	x.xferBool(&m_finished);
	xferIntField(x, &m_winningTeam);
	m_random.xfer(x);

	xferSize(x, m_players, kPlayerMin);
	for (PlayerState &p : m_players)
	{
		x.xferAsciiString(&p.name);
		x.xferAsciiString(&p.faction);
		x.xferAsciiString(&p.lwTemplate);
		x.xferBool(&p.human);
		xferIntField(x, &p.aiDifficulty);
		xferIntField(x, &p.team);
		xferIntField(x, &p.color);
		xferIntField(x, &p.handicap);
		xferIntField(x, &p.capital);
		xferIntField(x, &p.treasuryAccum);
		xferIntField(x, &p.lastIncome);
		xferIntField(x, &p.powerPoints);
		x.xferBool(&p.defeated);
		x.xferBool(&p.endedTurn);
		if (v.current >= 6)
		{
			// (Sol wotr2m8s4 #3) the network war's battle vote (+ 0x334): hashed in a network war, so a save made while some humans have voted holds it
			xferIntField(x, &p.battleVote);
			xferIntField(x, &p.battleVoteFor);
		}
		x.xferReal(&p.experience);
		xferIntField(x, &p.tacticalCPFromBuildings); // the player's xfer RW 0x6E445E: + 0x30C, + 0x310, + 0x314
		xferIntField(x, &p.worldCPFromBuildings);
		xferIntField(x, &p.queuedCommandPoints);
		for (std::vector<std::string> *names : { &p.sciences, &p.upgrades }) // WOTR2 milestone 7: + 0x1F8 / + 0x204 (as names)
		{
			xferSize(x, *names, 4);
			for (std::string &n : *names)
			{
				x.xferAsciiString(&n);
			}
		}
		xferScoreKeeper(x, p.score); // the player's xfer (RW 0x6E445E) holds its score keeper
		xferSize(x, p.buildableHeroes, kRevivalMin);
		for (RevivalUnit &r : p.buildableHeroes)
		{
			x.xferAsciiString(&r.spawnId);
			x.xferAsciiString(&r.thing);
			x.xferReal(&r.experience);
			x.xferBool(&r.dead);
		}
	}

	xferSize(x, m_armies, kArmyMin);
	for (Army &a : m_armies)
	{
		xferIntField(x, &a.id);
		xferIntField(x, &a.owner);
		xferIntField(x, &a.region);
		xferIntField(x, &a.destination);
		xferIntField(x, &a.movedFrom);
		x.xferAsciiString(&a.scriptingName);
		x.xferAsciiString(&a.playerArmy);
		x.xferAsciiString(&a.heroTemplate);
		x.xferAsciiString(&a.icon);
		x.xferAsciiString(&a.banner);
		x.xferBool(&a.garrison);
		x.xferBool(&a.retreating);
		xferIntField(x, &a.nextKey);
		xferSize(x, a.members, kMemberMin);
		for (ArmyMember &m : a.members)
		{
			x.xferAsciiString(&m.thingTemplate);
			x.xferAsciiString(&m.spawnId);
			xferIntField(x, &m.quantity);
			x.xferReal(&m.experience);
			xferIntField(x, &m.key);
			xferIntField(x, &m.homeArmy);
			// the CarryoverUnit xfer RW 0x780303: the set as a count and the upgrade names, the rank; the entry's mark (RW 0x80EE5A)
			xferSize(x, m.upgrades, 1);
			for (std::string &u : m.upgrades)
			{
				x.xferAsciiString(&u);
			}
			xferIntField(x, &m.rank);
			xferIntField(x, &m.upgradeBuilding);
		}
	}

	xferSize(x, m_regions, kRegionMin);
	for (RegionState &r : m_regions)
	{
		xferIntField(x, &r.owner);
		xferIntField(x, &r.ownerSinceTurn);
		x.xferBool(&r.enabled);
		xferSize(x, r.buildings, kBuildingMin);
		for (Building &b : r.buildings)
		{
			x.xferAsciiString(&b.templateName);
			xferIntField(x, &b.turnsLeft);
			xferIntField(x, &b.slot);
			xferIntField(x, &b.turnsSpent);
			xferIntField(x, &b.id);
			x.xferBool(&b.cpApplied); // the IncreaseCommandPoints nugget's xfer RW 0x9D38C7
			xferIntField(x, &b.upgradesUsed); // the UpgradeTroops nugget's xfer RW 0x9D3C76
		}
		xferSize(x, r.production, kOrderMin);
		for (ProductionOrder &o : r.production)
		{
			xferIntField(x, &o.buildingSlot);
			xferIntField(x, &o.armyIndex);
		}
	}

	xferSize(x, m_battles, kBattleMin);
	for (Battle &b : m_battles)
	{
		xferIntField(x, &b.region);
		xferIntField(x, &b.attacker);
		xferIntField(x, &b.defender);
		xferIntField(x, &b.resolution);
		xferIntField(x, &b.winner);
		xferIntField(x, &b.autoResolveRounds);
		std::uint32_t n = (std::uint32_t)b.armies.size();
		x.xferCount(&n, 4, sizeof(int));
		if (x.isLoading())
		{
			b.armies.assign(n, 0);
		}
		for (int &id : b.armies)
		{
			xferIntField(x, &id);
		}
		// the battle's sides and their result counters (LivingWorldBattle + 0x18 / + 0x38)
		xferSize(x, b.sides, kSideMin);
		for (Battle::Side &side : b.sides)
		{
			xferIntField(x, &side.team);
			xferSize(x, side.players, kSidePlayerMin);
			for (Battle::SidePlayer &p : side.players)
			{
				xferIntField(x, &p.player);
				xferIntField(x, &p.veterancy);
				xferIntField(x, &p.unitsLost);
				xferIntField(x, &p.enemyDestroyed);
				xferIntField(x, &p.heroesLost);
			}
		}
		xferIntField(x, &b.winningSide);
		x.xferBool(&b.resultsCounted);
	}

	xferSize(x, m_events, kEventMin);
	for (StrategicEvent &e : m_events)
	{
		xferIntField(x, &e.turn);
		xferIntField(x, &e.phase);
		x.xferAsciiString(&e.text);
	}
}

bool LivingWorldLogic::reattachAi(std::string *error)
{
	m_ai = std::make_unique<LivingWorldAI>(*this); // OWNER 2026-10-26, keep retail: the AI is not saved, so pending garrison orders are dropped (loadPostProcess RW 0x6E1A1E)
	return m_ai->attachAll(true, error);
}

void LivingWorldLogic::reseedAfterLoad()
{
	m_random.initGameLogicRandom(0, -1); // InitRandom(0) [S-080: the strategic game's own logic generator, as `start` seeds it]
}

void LivingWorldLogic::resumeAfterLanLoad()
{
	reseedAfterLoad();
	m_leaveVoteOpen = false;
	m_leaveVotes.clear();
	m_warEndAgreed = false;
}

bool LivingWorldLogic::saveState(std::vector<std::uint8_t> &out, std::string *error) const
{
	if (!m_started)
	{
		setError(error, "the War of the Ring game was not started");
		return false;
	}
	GameState gs;
	gs.addOwnedSnapshotBlock(kSnapshotToken, std::make_unique<LivingWorldSnapshot>(const_cast<LivingWorldLogic &>(*this)), 1u << SNAPSHOT_SAVELOAD,
		"LivingWorldLogic", {});
	try
	{
		gs.saveBlocks({ kSnapshotToken }, out);
	}
	catch (const std::exception &e)
	{
		setError(error, e.what());
		return false;
	}
	return true;
}

bool LivingWorldLogic::loadState(const GameSetup &data, const std::vector<std::uint8_t> &bytes, std::string *error, bool battleSave)
{
	if (m_started)
	{
		setError(error, "a saved game loads into a new LivingWorldLogic only");
		return false;
	}
	GameState gs;
	gs.addOwnedSnapshotBlock(kSnapshotToken, std::make_unique<LivingWorldSnapshot>(*this), 1u << SNAPSHOT_SAVELOAD, "LivingWorldLogic", {});
	try
	{
		gs.loadBlocks({ kSnapshotToken }, bytes);
	}
	catch (const std::exception &e)
	{
		setError(error, std::string("the War of the Ring save does not load: ") + e.what());
		m_started = false;
		return false;
	}
	m_ranks = data.ranks;
	m_rankTemplates = data.rankTemplates;
	m_resourceMultiplierLimit = data.resourceMultiplierLimit;

	// every name and index the state refers to (a save of other data, or a damaged one, is refused here, before the game runs on it)
	auto bad = [&](const std::string &what) {
		setError(error, "the War of the Ring save does not match this game's data: " + what);
		m_started = false;
		return false;
	};
	if (!m_started)
	{
		return bad("it holds no started game");
	}
	const int players = (int)m_players.size();
	const int regions = (int)m_regionCampaign->regions.size();
	const Scenario &sc = *m_campaign->scenario;
	if (players < sc.minPlayers || players > sc.maxPlayers)
	{
		return bad(std::to_string(players) + " players, the scenario takes " + std::to_string(sc.minPlayers) + ".." + std::to_string(sc.maxPlayers));
	}
	if ((int)m_regions.size() != regions)
	{
		return bad(std::to_string(m_regions.size()) + " regions, the region campaign '" + m_regionCampaign->name + "' has " + std::to_string(regions));
	}
	if (m_phase < 0 || m_phase >= PHASE_COUNT)
	{
		return bad("the turn phase " + std::to_string(m_phase));
	}
	if (m_turn < 1 || m_settleCounter < 0 || m_nextArmyId < 1)
	{
		return bad("the turn " + std::to_string(m_turn) + " / counters");
	}
	auto playerOk = [&](int p, bool none) { return (none && p == -1) || (p >= 0 && p < players); };
	auto finite = [](float f) { return std::isfinite(f); };
	auto regionOk = [&](int r, bool none) { return (none && r == -1) || (r >= 0 && r < regions); };
	for (int i = 0; i < players; ++i)
	{
		const PlayerState &p = m_players[(size_t)i];
		if (!m_stores.findPlayerTemplate(p.lwTemplate))
		{
			return bad("player " + std::to_string(i) + "'s LivingWorldPlayerTemplate '" + p.lwTemplate + "' is not defined");
		}
		if (!regionOk(p.capital, false))
		{
			return bad("player " + std::to_string(i) + "'s start region " + std::to_string(p.capital));
		}
		// review (Sol wotr2m3): every value the logic, the battle start or the HUD uses as an index or in a comparison is in range
		if (p.color < -1 || p.color >= 64 || !finite(p.experience))
		{
			return bad("player " + std::to_string(i) + "'s colour or experience");
		}
		// the battle vote: a method 0 .. 3 (MSG 0x6A7's check; 0 none) and the battle it was for, -1 none (a vote for a battle no longer listed counts
		// for none: the tally compares the index)
		if (p.battleVote < 0 || p.battleVote > 3 || p.battleVoteFor < -1)
		{
			return bad("player " + std::to_string(i) + "'s battle vote " + std::to_string(p.battleVote) + " for battle " + std::to_string(p.battleVoteFor));
		}
		for (const RevivalUnit &r : p.buildableHeroes)
		{
			if (!finite(r.experience))
			{
				return bad("player " + std::to_string(i) + "'s buildable hero '" + r.thing + "'");
			}
		}
	}
	for (int r = 0; r < regions; ++r)
	{
		const RegionState &st = m_regions[(size_t)r];
		if (!playerOk(st.owner, true))
		{
			return bad("region " + std::to_string(r) + "'s owner " + std::to_string(st.owner));
		}
		const int spots = (int)m_regionCampaign->regions[(size_t)r].buildingSpots.size();
		for (size_t bi = 0; bi < st.buildings.size(); ++bi)
		{
			const Building &b = st.buildings[bi];
			if (!m_stores.findBuilding(b.templateName))
			{
				return bad("the LivingWorldBuilding '" + b.templateName + "' is not defined");
			}
			if (b.slot < 0 || b.slot >= spots || b.turnsLeft < 0 || b.turnsSpent < 0)
			{
				return bad("region " + std::to_string(r) + "'s building in slot " + std::to_string(b.slot) + " (" + std::to_string(spots) + " spots)");
			}
			for (size_t bj = 0; bj < bi; ++bj)
			{
				if (st.buildings[bj].slot == b.slot)
				{
					return bad("region " + std::to_string(r) + " has two buildings in slot " + std::to_string(b.slot));
				}
			}
		}
		for (const ProductionOrder &o : st.production)
		{
			const Building *producer = nullptr;
			for (const Building &b : st.buildings)
			{
				producer = b.slot == o.buildingSlot ? &b : producer;
			}
			if (!producer)
			{
				return bad("region " + std::to_string(r) + " queues for the empty building slot " + std::to_string(o.buildingSlot));
			}
			const BuildingNugget *nug = m_stores.findBuilding(producer->templateName)->findNugget(BuildingNugget::SPAWN_ARMY);
			if (!nug || o.armyIndex < 0 || o.armyIndex >= (int)nug->armiesToSpawn.size())
			{
				return bad("region " + std::to_string(r) + " queues army " + std::to_string(o.armyIndex) + " at a '" + producer->templateName +
					"', which does not offer it");
			}
		}
	}
	// review r2 (Sol wotr2m3): a member's count is expanded unit by unit (a fought battle's start, StartingBase; the autoresolve's records):
	// far above any army the command points allow, a hostile count would stall or exhaust the game. INFERENCE: the bounds are OpenBFME's
	// (retail's counts are limited by the command points the logic enforces, not by the save)
	constexpr long long kMaxMemberUnits = 1000, kMaxGameUnits = 100000;
	long long units = 0;
	for (const Army &a : m_armies)
	{
		for (const ArmyMember &m : a.members)
		{
			if (m.quantity > kMaxMemberUnits)
			{
				return bad("army " + std::to_string(a.id) + "'s member '" + m.thingTemplate + "' counts " + std::to_string(m.quantity) +
					" units (at most " + std::to_string(kMaxMemberUnits) + ")");
			}
			units += m.quantity > 0 ? m.quantity : 0;
		}
	}
	if (units > kMaxGameUnits)
	{
		return bad("the armies count " + std::to_string(units) + " units (at most " + std::to_string(kMaxGameUnits) + ")");
	}
	// coordinator follow-up: retail has no army maximum (XferOwningLivingWorldArmyVec, RW 0x71C1DB, reads the count as an int and creates that many
	// armies unchecked; the regions keep them in per-hero-slot vectors). INFERENCE: OpenBFME's bound, far above any war the command points allow, so
	// a hostile count cannot exhaust memory; the duplicate id check below sorts (n log n), never compares every pair
	constexpr size_t kMaxArmies = 4096;
	if (m_armies.size() > kMaxArmies)
	{
		return bad(std::to_string(m_armies.size()) + " armies (at most " + std::to_string(kMaxArmies) + ")");
	}
	{
		std::vector<int> ids;
		ids.reserve(m_armies.size());
		for (const Army &a : m_armies)
		{
			ids.push_back(a.id);
		}
		std::sort(ids.begin(), ids.end());
		const auto dup = std::adjacent_find(ids.begin(), ids.end());
		if (dup != ids.end())
		{
			return bad("two armies have the id " + std::to_string(*dup));
		}
	}
	for (const Army &a : m_armies)
	{
		if (!playerOk(a.owner, false) || !regionOk(a.region, false) || !regionOk(a.destination, true) || !regionOk(a.movedFrom, true))
		{
			return bad("army " + std::to_string(a.id) + "'s owner or regions");
		}
		if (a.id <= 0 || a.id >= m_nextArmyId)
		{
			return bad("the army id " + std::to_string(a.id) + " (the next id is " + std::to_string(m_nextArmyId) + ")");
		}
		std::vector<int> keys;
		for (const ArmyMember &m : a.members)
		{
			if (m.quantity < 0 || !finite(m.experience) || m.key <= 0 || m.key >= a.nextKey || m.homeArmy < 0 || m.rank < 0 || m.upgradeBuilding < 0 ||
				m.upgradeBuilding >= m_nextBuildingId || !std::is_sorted(m.upgrades.begin(), m.upgrades.end()))
			{
				return bad("army " + std::to_string(a.id) + "'s member '" + m.thingTemplate + "'");
			}
			keys.push_back(m.key);
		}
		std::sort(keys.begin(), keys.end());
		if (std::adjacent_find(keys.begin(), keys.end()) != keys.end())
		{
			return bad("army " + std::to_string(a.id) + " has two entries with one key");
		}
	}
	{
		// WOTR2 milestone 4: building ids are unique and below the next id; the retreat list names armies of the game
		std::vector<int> ids;
		for (const RegionState &st : m_regions)
		{
			for (const Building &b : st.buildings)
			{
				if (b.upgradesUsed < 0)
				{
					return bad("building " + std::to_string(b.id) + "'s upgrade count");
				}
				if (b.id <= 0 || b.id >= m_nextBuildingId)
				{
					return bad("the building id " + std::to_string(b.id) + " (the next id is " + std::to_string(m_nextBuildingId) + ")");
				}
				ids.push_back(b.id);
			}
		}
		std::sort(ids.begin(), ids.end());
		if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
		{
			return bad("two buildings have one id");
		}
		for (int id : m_retreatList)
		{
			if (!findArmy(id))
			{
				return bad("the retreat list names the missing army " + std::to_string(id));
			}
		}
	}
	int fought = 0;
	for (const Battle &b : m_battles)
	{
		if (!regionOk(b.region, false) || !playerOk(b.attacker, true) || !playerOk(b.defender, true) || !playerOk(b.winner, true))
		{
			return bad("a battle's region or players");
		}
		// a map save holds no battle being fought in real time; a battle's save (WOTR2 milestone 6) exactly one
		if (b.resolution != Battle::PENDING && b.resolution != Battle::RESOLVED && !(battleSave && b.resolution == Battle::RTS_REQUESTED))
		{
			return bad("a battle's resolution " + std::to_string(b.resolution));
		}
		fought += b.resolution == Battle::RTS_REQUESTED ? 1 : 0;
		for (int id : b.armies)
		{
			if (!findArmy(id) && b.resolution != Battle::RESOLVED)
			{
				return bad("an unresolved battle names the missing army " + std::to_string(id));
			}
		}
	}
	if (battleSave && (fought != 1 || m_phase != PHASE_BATTLES))
	{
		return bad("a battle's save holds " + std::to_string(fought) + " battles fought in real time in the turn phase " + std::to_string(m_phase) +
			" (one, in the battles phase)");
	}
	// the players' loadPostProcess re-attaches the AI (RW 0x6E1A1E -> attach RW 0x90002D; the AI object is not in the save): every AI plays at level 5,
	// no order of the saved turn is pending, the region distances are built again (RW 0x6BD055)
	if (!reattachAi(error))
	{
		m_started = false;
		return false;
	}
	return true;
}

} // namespace LivingWorld
