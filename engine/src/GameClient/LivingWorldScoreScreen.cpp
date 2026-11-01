// OpenBFME. GPL-3.0.
// See GameClient/LivingWorldScoreScreen.h (WOTR2 milestone 5).

#include "GameClient/LivingWorldScoreScreen.h"

#include "GameLogic/LivingWorld/LivingWorldLogic.h"

using namespace LivingWorld;

ScoreScreenData buildWarScoreScreen(const LivingWorldLogic &logic, const WarScoreScreenInput &in)
{
	ScoreScreenData d;
	d.type = 6;
	d.valid = true;
	for (int k = 0; k < 3; ++k)
	{
		d.warMultipliers[k] = in.multipliers[k];
	}
	const std::vector<PlayerState> &players = logic.players();
	std::vector<int> order;
	if (in.localPlayer >= 0 && in.localPlayer < (int)players.size())
	{
		order.push_back(in.localPlayer);
	}
	for (int p = 0; p < (int)players.size(); ++p)
	{
		if (p != in.localPlayer)
		{
			order.push_back(p);
		}
	}
	const int now = logic.turn() - 1; // retail's turn (from 0)
	for (int p : order)
	{
		const PlayerState &ps = players[(size_t)p];
		const LivingWorld::ScoreKeeper &sk = ps.score;
		ScoreScreenData::Entry e;
		e.playerIndex = p;
		e.name = ps.name;
		e.color = p < (int)in.colors.size() ? in.colors[(size_t)p] : 0;
		e.side = p < (int)in.sides.size() ? in.sides[(size_t)p] : std::string();
		e.local = p == in.localPlayer;
		// RW 0x92723E: 1 defeated, 3 still at war, else 0
		const bool atWar = !logic.finished() || (logic.winningTeam() >= 0 ? ps.team != logic.winningTeam() : true);
		e.result = ps.defeated ? 1 : (atWar ? 3 : 0);
		for (const LivingWorld::ScoreKeeper::PerTurn &t : sk.perTurn)
		{
			e.warTurns.push_back({ t.territories, t.units, t.structures });
		}
		e.fortressMarks = sk.fortressTurns;
		e.territoryMarks = sk.regionUnifiedTurns;
		int recruited = 0;
		for (const auto &r : sk.recruited)
		{
			recruited += r.second;
		}
		std::array<std::int32_t, 27> &v = e.warStats;
		v[0] = 0; // the real-time battles' frames / 5: not ported
		v[1] = e.local ? (std::int32_t)in.strategicSeconds : 0;
		v[2] = (sk.defeatTurn != -1 ? sk.defeatTurn : now) + 1;
		v[5] = sk.structuresLost;
		v[7] = sk.structuresDestroyed;
		v[8] = sk.buildingsBuilt[1];
		v[9] = sk.buildingsBuilt[3];
		v[10] = sk.buildingsBuilt[2];
		v[11] = sk.buildingsBuilt[4];
		v[12] = recruited;
		v[13] = sk.unitsLostAutoResolve + sk.heroesLostAutoResolve;
		v[15] = sk.unitsLostAutoResolve + sk.heroesLostAutoResolve;
		v[17] = sk.unitsKilledAutoResolve;
		v[18] = sk.rtsBattlesWon;
		v[19] = sk.rtsBattlesLost;
		v[21] = sk.autoResolveWon;
		v[22] = sk.autoResolveLost;
		v[23] = sk.territoriesConquered;
		v[24] = sk.territoriesLost;
		v[25] = (std::int32_t)sk.regionUnifiedTurns.size();
		v[26] = sk.regionsLost;
		if (e.local && (e.result == 0 || e.result == 1))
		{
			const bool won = e.result == 0;
			const Campaign &c = logic.campaign();
			const Scenario &sc = *c.scenario;
			d.strategicEnd = true;
			d.winLossTitleLabel = won ? "APT:EndVictorious" : "APT:EndDefeat";
			d.scenarioNameLabel = sc.displayName;
			if (sc.useMpRulesVictoryCondition && !c.victoryTypes.empty())
			{
				const int vt = logic.victoryType();
				const VictoryType &t = c.victoryTypes[(size_t)(vt >= 0 && vt < (int)c.victoryTypes.size() ? vt : 0)]; // RW 0x932BF7
				d.conditionLabel = won ? t.displayVictoriousText : t.displayDefeatedText;
			}
			else
			{
				d.conditionLabel = won ? sc.displayVictoriousText : sc.displayDefeatedText;
			}
			const bool isEvil = p < (int)in.evil.size() && in.evil[(size_t)p];
			d.winLossImage = std::string("StrategicEndGame") + (isEvil ? "Evil" : "Good") + (won ? "Win" : "Lose");
		}
		d.entries.push_back(e);
	}
	return d;
}
