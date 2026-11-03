// OpenBFME. GPL-3.0.
//
// The War of the Ring score keepers (LivingWorldScoreKeeper, LivingWorldPlayer + 0x348: ctor RW 0x8F352E registers eight listeners; WOTR2 milestone 5,
// spec P 2.1 / 2.2). RotWK 2.01, caveat S-001; BFME2 twins in brackets.
//
// TARGET FACTS (RotWK, read from the disassembly):
//   * collect(turn) RW 0x8F3888 [collectEndOfTurnStats 0x8EEE38]: once per turn (+0x7C); per ConcurrentRegionBonus (region campaign order) whether it
//     is held (RW 0x60EE5E: every region of it held by the player or an ally, at least one by the player): newly held -> the turn joins +0x34, no
//     longer held -> +0xF0 + 1; then one PerTurnStats: the player's regions, the completed buildings in them (RW 0x7F3EFC), its armies' summary
//     ENTRIES (not quantities); the shorts wrap at 16 bits. Called by the turn listener (RW 0x8F3D4A, phase 6's advance before the turn grows),
//     a defeat (RW 0x8F3CFB: the defeat turn + 0x78, then the keeper detaches) and the war's end (RW 0x8F3D59);
//   * a building completed (RW 0x8F31EE): of the owner, a FORTRESS's turn joins +0x4C, +0x80[Type] + 1; destroyed (RW 0x8F2A4D) after its
//     completion: the owner's +0x94 + 1 (the destroyer's +0x98 + 1);
//   * an autoresolve (RW 0x8F2C55): a player of the battle wins (+0xE0) unless its side lost (+0xE4); per kill event a victim of the player is a
//     structure (+0x94), a hero (+0xAC) or a unit (+0xA8), a killer's victim a structure (+0x98) or a unit (+0xB0);
//   * a real-time battle completed (RW 0x8F2AA1): won (+0xD8) or lost (+0xDC); a region's owner changed (RW 0x8F2AEE): the old owner lost one
//     (+0xEC), else the new owner conquered one (+0xE8); a SpawnArmy nugget's army (RW 0x8F323D): each entry's count joins +0x9C by template.
// INFERENCE [S-1331]: the turn is retail's (from 0: m_turn - 1); a building unit of the autoresolve (a CreateUnitDuringAutoResolve building's or an
// auto fort's) is a structure casualty (the kill list's + 0xC4); the destroyer of a building outside the autoresolve is not known (no +0x98 there).

#include "GameLogic/LivingWorld/LivingWorldLogic.h"

#include "GameLogic/LivingWorld/LivingWorldAutoResolveBattle.h"

#include <algorithm>

namespace LivingWorld
{

void LivingWorldLogic::scoreCollect(int player, int turn)
{
	ScoreKeeper &sk = m_players[(size_t)player].score;
	if (sk.lastTurn == turn)
	{
		return;
	}
	sk.lastTurn = turn;
	const std::vector<ConcurrentRegionBonus> &rules = m_regionCampaign->concurrentRegionBonuses;
	sk.rulesHeld.resize(rules.size(), false);
	for (size_t i = 0; i < rules.size(); ++i)
	{
		const bool was = sk.rulesHeld[i];
		bool held = !rules[i].regions.empty();
		int mine = 0;
		for (const std::string &name : rules[i].regions)
		{
			const int r = m_regionCampaign->regionIndex(name);
			const int o = r >= 0 ? m_regions[(size_t)r].owner : -1;
			if (o < 0 || (o != player && hostile(o, player))) // RW 0x6E1EC4: the owner -1 is nobody's ally
			{
				held = false;
			}
			mine += o == player ? 1 : 0;
		}
		held = held && mine != 0;
		sk.rulesHeld[i] = held;
		if (!was && held)
		{
			sk.regionUnifiedTurns.push_back(turn);
		}
		if (was && !held)
		{
			++sk.regionsLost;
		}
	}
	ScoreKeeper::PerTurn s;
	for (const RegionState &r : m_regions)
	{
		if (r.owner != player)
		{
			continue;
		}
		s.territories = (std::int16_t)(s.territories + 1);
		for (const Building &b : r.buildings)
		{
			s.structures = (std::int16_t)(s.structures + (b.turnsLeft == 0 ? 1 : 0));
		}
	}
	for (const Army &a : m_armies)
	{
		if (a.owner == player)
		{
			s.units = (std::int16_t)(s.units + (int)a.members.size());
		}
	}
	sk.perTurn.push_back(s);
}

void LivingWorldLogic::scoreCollectAll(int turn)
{
	for (int p = 0; p < (int)m_players.size(); ++p)
	{
		if (!m_players[(size_t)p].score.detached)
		{
			scoreCollect(p, turn);
		}
	}
}

void LivingWorldLogic::scoreDefeated(int player)
{
	ScoreKeeper &sk = m_players[(size_t)player].score;
	if (sk.detached)
	{
		return;
	}
	sk.defeatTurn = retailTurn();
	scoreCollect(player, retailTurn());
	sk.detached = true; // RW 0x8F31DA: off every list
}

void LivingWorldLogic::scoreBuildingCompleted(int region, const Building &b)
{
	const int owner = m_regions[(size_t)region].owner;
	const BuildingTemplate *t = buildingTemplate(b);
	if (owner < 0 || !t || m_players[(size_t)owner].score.detached)
	{
		return;
	}
	ScoreKeeper &sk = m_players[(size_t)owner].score;
	if (t->type == BUILDING_FORTRESS)
	{
		sk.fortressTurns.push_back(retailTurn());
	}
	if (t->type >= 0 && t->type < 5)
	{
		++sk.buildingsBuilt[t->type];
	}
}

void LivingWorldLogic::scoreBuildingDestroyed(int region, const Building &b)
{
	const int owner = m_regions[(size_t)region].owner;
	if (b.turnsLeft > 0 || owner < 0 || m_players[(size_t)owner].score.detached)
	{
		return; // + 0x20 == -1: never completed
	}
	++m_players[(size_t)owner].score.structuresLost;
}

void LivingWorldLogic::scoreAutoResolve(const AutoResolveBattle &fight, const std::vector<AutoResolvePlayerSpec> &specs, const Battle &battle)
{
	for (const Battle::Side &side : battle.sides)
	{
		for (const Battle::SidePlayer &sp : side.players)
		{
			ScoreKeeper &sk = m_players[(size_t)sp.player].score;
			if (sk.detached)
			{
				continue;
			}
			const bool lost = battle.winningSide < 0 || &side != &battle.sides[(size_t)battle.winningSide];
			++(lost ? sk.autoResolveLost : sk.autoResolveWon);
		}
	}
	for (const AutoResolveBattle::KillEvent &k : fight.killEvents())
	{
		const int victim = specs[(size_t)k.victimRecord].player;
		const int killer = specs[(size_t)k.killerRecord].player;
		const AutoResolveUnitSpec &v = k.victim;
		const bool structure = v.buildingSlot >= 0 || (m_units && m_units(v.thingTemplate).autoResolveUnitType == AR_FORTRESS);
		// each keeper reads the list for itself (RW 0x8F2C55 runs once per keeper): its victims first, else its kills
		if (!m_players[(size_t)victim].score.detached)
		{
			ScoreKeeper &sk = m_players[(size_t)victim].score;
			++(structure ? sk.structuresLost : (m_units && m_units(v.thingTemplate).hero ? sk.heroesLostAutoResolve : sk.unitsLostAutoResolve));
		}
		if (killer != victim && !m_players[(size_t)killer].score.detached)
		{
			ScoreKeeper &sk = m_players[(size_t)killer].score;
			++(structure ? sk.structuresDestroyed : sk.unitsKilledAutoResolve);
		}
	}
}

void LivingWorldLogic::scoreRtsBattle(const Battle &battle)
{
	// the battle's players as phase 2 recorded them (review wotr2m5: an attacker whose armies were all destroyed still lost)
	std::vector<int> players;
	for (const Battle::Side &side : battle.sides)
	{
		for (const Battle::SidePlayer &sp : side.players)
		{
			players.push_back(sp.player);
		}
	}
	for (int p : players)
	{
		ScoreKeeper &sk = m_players[(size_t)p].score;
		if (sk.detached)
		{
			continue;
		}
		const bool won = battle.winner >= 0 && (battle.winner == p || !hostile(battle.winner, p));
		++(won ? sk.rtsBattlesWon : sk.rtsBattlesLost);
	}
}

void LivingWorldLogic::scoreRecruited(int player, const PlayerArmy &pa)
{
	ScoreKeeper &sk = m_players[(size_t)player].score;
	if (sk.detached)
	{
		return;
	}
	for (const ArmyEntry &e : pa.entries)
	{
		sk.recruited[e.thingTemplate] += e.quantity;
	}
}

void LivingWorldLogic::setRegionOwner(int region, int owner)
{
	RegionState &rs = m_regions[(size_t)region];
	const int old = rs.owner;
	rs.owner = owner;
	rs.ownerSinceTurn = m_turn;
	if (old == owner)
	{
		return;
	}
	if (old >= 0 && !m_players[(size_t)old].score.detached)
	{
		++m_players[(size_t)old].score.territoriesLost;
	}
	if (owner >= 0 && !m_players[(size_t)owner].score.detached)
	{
		++m_players[(size_t)owner].score.territoriesConquered;
	}
}

} // namespace LivingWorld
