// OpenBFME. GPL-3.0. See BattleResultCapture.h.

#include "GameLogic/LivingWorld/BattleResultCapture.h"

#include "Common/PlayerHeroList.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/EndGame.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/VictoryConditions.h"

#include <algorithm>

using namespace LivingWorld;

// WOTR2 milestone 6: the battle's end as GameLogic::LivingWorldTacticalBattleComplete RW 0x626662 reads it.
//   * the survivors (ArmySummarySystem::Save RW 0x811E1F): every live KindOf ARMY_SUMMARY object of a battle player, in object-list order, under its army:
//     a spawned object's, else [S-1329 inference] its player's garrison (retail tags each made object with its maker's army id; what a porter or a region
//     building makes is the garrison's), with its experience (tracker + 0x10), level (+ 0x24) and upgrade mask as names (Object::fillEntry RW 0x6917BB);
//   * the victor (GetLivingWorldTacticalVictor RW 0x6265D2): the first player, in side order, that hasAchievedVictory accepts; -1 none.
bool ReadBattleResult(const GameLogic &logic, const std::vector<BattleSeat> &battleSeats, const Player *left, BattleResult &out, std::string *error)
{
	out = BattleResult();
	if (!TheUpgradeCenter)
	{
		if (error)
		{
			*error = "no upgrade store";
		}
		return false;
	}
	const VictoryConditions &victory = logic.victory();
	struct Seat
	{
		const Player *player = nullptr;
		int lw = -1;
		int garrison = 0;
	};
	std::vector<Seat> seats;
	for (const BattleSeat &b : battleSeats)
	{
		Seat st;
		st.player = logic.players().findPlayerWithName(b.name);
		st.lw = b.player;
		st.garrison = b.garrison;
		if (!st.player)
		{
			if (error)
			{
				*error = "the battle player " + b.name + " is not in the game";
			}
			return false;
		}
		seats.push_back(st);
	}
	auto allies = [](const Player *a, const Player *b) {
		return a == b || (a->getRelationship(b->getDefaultTeam()) == ALLIES && b->getRelationship(a->getDefaultTeam()) == ALLIES);
	};
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isEffectivelyDead() || o->isDestroyed() || !o->isKindOfName("ARMY_SUMMARY"))
		{
			continue;
		}
		const Player *owner = o->getControllingPlayer();
		const Seat *seat = nullptr;
		for (const Seat &st : seats)
		{
			seat = st.player == owner ? &st : seat;
		}
		if (!seat || (left && owner == left))
		{
			continue;
		}
		BattleSurvivor sv;
		// WOTR2 milestone 7: the object's army tag (Object + 0x47C / + 0x480, LivingWorldBattleTags); an untagged object made in the battle: its player's garrison
		const LivingWorldBattleTags::Tag *tag = logic.livingWorldTags().object(o->getID());
		sv.armyId = tag && tag->armyId != 0 ? tag->armyId : seat->garrison;
		sv.spawnId = tag ? tag->spawnId : std::string();
		sv.thingTemplate = o->getTemplate()->getName();
		const ExperienceTracker *xp = o->getExperienceTracker();
		sv.experience = xp ? xp->getExperience() : 0.0f;
		sv.level = xp ? xp->getRank() : 0;
		for (const UpgradeTemplate *u : TheUpgradeCenter->templates())
		{
			if (u && u->getMaskBit() >= 0 && o->getUpgradeMask().test((unsigned)u->getMaskBit()))
			{
				sv.upgrades.push_back(u->getUpgradeName());
			}
		}
		std::sort(sv.upgrades.begin(), sv.upgrades.end());
		out.survivors.push_back(sv);
	}
	if (!left)
	{
		for (const Seat &st : seats)
		{
			if (victory.hasAchievedVictory(st.player))
			{
				out.winner = st.lw;
				break;
			}
		}
	}
	else
	{
		const Seat *first = nullptr;
		bool oneAlliance = true;
		for (const Seat &st : seats)
		{
			if (st.player == left || victory.hasBeenDefeated(st.player))
			{
				continue;
			}
			if (allies(st.player, left))
			{
				oneAlliance = false; // the leaver's side fights on
			}
			if (first && !allies(first->player, st.player))
			{
				oneAlliance = false;
			}
			first = first ? first : &st;
		}
		out.winner = first && oneAlliance ? first->lw : -1;
	}
	// WOTR2 milestone 7: the HERO + ARMY_SUMMARY records left in each battle player's hero list (UnitRevivalTracker::retrieveHeroesForArmySummary RW 0x78100E,
	// template + 0x113 bit 4 and + 0x118 bit 0) with their army / spawn ids (a record the battle player got from the war: its garrison), and the region
	// buildings that died (RW 0x6991F3)
	for (const Seat &st : seats)
	{
		for (const HeroRecord &rec : st.player->heroes().records())
		{
			const ThingTemplate *tt = logic.things().findTemplate(rec.templateName);
			if (!tt || !logic.economy().templateKindOf(*tt, "HERO") || !logic.economy().templateKindOf(*tt, "ARMY_SUMMARY"))
			{
				continue;
			}
			const LivingWorldBattleTags::Tag *t = logic.livingWorldTags().hero(st.player->getPlayerIndex(), rec.templateName);
			BattleHeroRecord h;
			h.player = st.lw;
			h.thingTemplate = rec.templateName;
			h.armyId = t && t->armyId != 0 ? t->armyId : st.garrison;
			h.spawnId = t ? t->spawnId : std::string();
			h.experience = rec.experience;
			h.rank = rec.rank;
			h.dead = rec.dead;
			out.heroes.push_back(h);
		}
	}
	// WOTR2 milestone 7 (step 2): ArmySummarySystem::Save's tail per battle player (RW 0x811E1F): earned spell points, sciences, lasting upgrades, skill points
	for (const Seat &st : seats)
	{
		if (left && st.player == left)
		{
			continue; // [S-1329 inference] the leaver's war keeps what it had
		}
		const PlayerScience &sc = st.player->science();
		BattleResult::PlayerBack pb;
		pb.player = st.lw;
		pb.earnedSpellPoints = sc.earnedSpellPoints();
		pb.skillPoints = sc.getSkillPoints();
		for (ScienceType t : sc.sciences())
		{
			pb.sciences.push_back(TheScienceStore ? TheScienceStore->getInternalNameForScience(t) : std::string());
		}
		for (const UpgradeTemplate *u : TheUpgradeCenter->templates())
		{
			// RW 0x6E281B: the completed PLAYER upgrades (+ 4 == 0) that persist (+ 0x78)
			if (u && u->getMaskBit() >= 0 && u->getUpgradeType() == UPGRADE_TYPE_PLAYER && u->persistsInCampaign() &&
				st.player->getCompletedUpgradeMask().test((unsigned)u->getMaskBit()))
			{
				pb.upgrades.push_back(u->getUpgradeName());
			}
		}
		out.players.push_back(pb);
	}
	for (const LivingWorldBattleTags::Reinforcement &r : logic.livingWorldTags().reinforcements) // WOTR2 milestone 7 (step 5)
	{
		if (r.state != 4)
		{
			out.undeployedArmies.push_back(r.armyId);
		}
	}
	for (int id : logic.livingWorldTags().destroyedBuildings)
	{
		out.destroyedBuildings.push_back(id);
	}
	out.collected = true;
	return true;
}

void BattleResultCapture::arm(GameLogic &logic, const std::vector<BattleSeat> &seats)
{
	m_state = std::make_shared<State>();
	m_state->seats = seats;
	const std::shared_ptr<State> state = m_state;
	GameLogic *lg = &logic;
	logic.addEndOfFrameHook([state, lg](UnsignedInt frame) {
		{
			const std::lock_guard<std::mutex> lock(state->mutex);
			if (state->done)
			{
				return;
			}
		}
		const long long end = EndGameController::victoryTimerEndFrame(lg->victory().events());
		if (end < 0 || (long long)frame < end)
		{
			return;
		}
		BattleResult r;
		std::string e;
		bool ok = true;
		if ((long long)frame != end)
		{
			ok = false; // the hook runs every frame: only a capture armed after T_end gets here
			e = "the battle's end frame " + std::to_string(end) + " ran before the capture was armed (frame " + std::to_string(frame) + ")";
		}
		else
		{
			ok = ReadBattleResult(*lg, state->seats, nullptr, r, &e);
		}
		const std::lock_guard<std::mutex> lock(state->mutex);
		state->done = true;
		state->ok = ok;
		state->frame = (long long)frame;
		state->result = std::move(r);
		state->error = std::move(e);
	});
}

bool BattleResultCapture::done() const
{
	if (!m_state)
	{
		return false;
	}
	const std::lock_guard<std::mutex> lock(m_state->mutex);
	return m_state->done;
}

bool BattleResultCapture::result(BattleResult &out, long long *frame, std::string *error) const
{
	if (!m_state)
	{
		if (error)
		{
			*error = "the battle's result capture was not armed";
		}
		return false;
	}
	const std::lock_guard<std::mutex> lock(m_state->mutex);
	if (frame)
	{
		*frame = m_state->frame;
	}
	if (!m_state->done)
	{
		if (error)
		{
			*error = "the battle's end frame (T_end) has not run";
		}
		return false;
	}
	if (!m_state->ok)
	{
		if (error)
		{
			*error = m_state->error;
		}
		return false;
	}
	out = m_state->result;
	return true;
}
