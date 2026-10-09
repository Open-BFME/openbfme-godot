// OpenBFME. GPL-3.0.
// See GameLogic/ExperienceWorld.h.

#include "GameLogic/ExperienceWorld.h"

#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/FXEvents.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "GameLogic/Economy.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

namespace
{
const int kStatusPhantomStructure = 88; // RW 0x821595 (TheObjectStatusNames[88])
const int kModelConditionLeveled = 210; // RW 0x8213D4 (TheModelConditionNames[210] LEVELED)
} // namespace

ExperienceWorld::ExperienceWorld(GameLogic &logic)
	: m_logic(logic)
{
	// the experience of a resource building's income (TerrainResourceBehavior RW 0x88573C, AutoDepositUpdate RW 0x89DD1F): when the object has a tracker that
	// accepts experience, addExperiencePoints(points, true, true, true, false) (closes S-256's seam)
	m_logic.economy().setExperienceHook([](Object &obj, float points) {
		ExperienceTracker *t = obj.getExperienceTracker();
		if (t && t->isAcceptingExperiencePoints())
		{
			t->addExperiencePoints(points, true, true, true, false);
		}
	});
}

void ExperienceWorld::reset()
{
	m_pending.clear();
	m_suspended = false;
	m_counters = Counters();
}

void ExperienceWorld::queueLevelGrant(const ExperienceLevelTemplate &level, Object &obj, bool feedback)
{
	if (m_suspended) // RW 0x8215C8
	{
		return;
	}
	Pending p;
	p.id = obj.getID();
	p.level = &level;
	p.feedback = feedback;
	m_pending.push_back(p);
}

void ExperienceWorld::update()
{
	// RW 0x821567: the list is walked by index so a grant that queues another (a level reached by an upgrade) is applied in this pass, like the walk of the
	// retail list to its end
	for (size_t i = 0; i < m_pending.size(); ++i)
	{
		const Pending p = m_pending[i];
		Object *obj = m_logic.findObjectByID(p.id);
		if (!obj)
		{
			continue;
		}
		if (obj->isEffectivelyDead() && !obj->testStatus((unsigned)kStatusPhantomStructure))
		{
			continue;
		}
		grantLevel(*p.level, *obj, p.feedback);
	}
	m_pending.clear();
}

void ExperienceWorld::grantLevel(const ExperienceLevelTemplate &level, Object &obj, bool feedback)
{
	++m_counters.levelsGranted;
	for (const std::string &name : level.m_attributeModifiers) // RW 0x82133D
	{
		obj.addAttributeModifier(name, -1);
	}
	for (const UpgradeTemplate *u : level.m_upgrades) // RW 0x821368
	{
		obj.giveUpgrade(u);
	}
	if (obj.isKindOfName("CREATE_A_HERO")) // RW 0x821383 .. 0x8213A9: the record of the object builds the command set of the level's Rank (RW 0x809FFB)
	{
		++m_counters.createAHeroRanks;
		m_logic.createAHeroes().buildCommandSet(obj, level.m_rank); // lane HERO-2
	}
	if (feedback && m_logic.getFrame() >= 10) // RW 0x8213AE .. 0x8213DB
	{
		for (const ExperienceLevelTemplate::LevelUpFx &fx : level.m_levelUpFx) // RW 0x8213C6 -> 0x8211EC
		{
			if (fx.fxList.empty())
			{
				continue; // "None": no list, the call plays nothing
			}
			FXEvent e = FXEventLog::objectEvent(FXEvent::OBJECT_FX, "LevelUpFx", m_logic.getFrame(), fx.fxList, obj);
			e.bone = fx.bone.empty() ? nullptr : &fx.bone; // RW 0x821217: an empty bone takes doFXObj
			m_logic.fxEvents().emit(e);
			++m_counters.levelUpFxEvents;
		}
		if (!level.m_levelUpOCL.empty())
		{
			++m_counters.levelUpOclNotCreated;
		}
		obj.setSpecialModelConditionState(kModelConditionLeveled, 3u * LOGICFRAMES_PER_SECOND);
	}
	if (ExperienceTracker *t = obj.getExperienceTracker()) // RW 0x8213E7
	{
		t->setLevelTemplate(level);
	}
	bool anyCondition = false; // RW 0x8213F1: any bit set
	for (std::uint32_t w : level.m_modelConditionState)
	{
		anyCondition = anyCondition || w != 0;
	}
	if (anyCondition)
	{
		obj.clearAndSetModelConditionFlags(Object::ModelConditionBits{}, level.m_modelConditionState);
	}
	if (level.m_informUpdateModule)
	{
		++m_counters.informUpdateModuleNotRun;
	}
	if (level.m_emotionType != -1)
	{
		++m_counters.emotionsNotRun;
	}
}

void PlayerExperienceAwardSink::addSkillPoints(Player &player, float amount)
{
	player.science().addSkillPoints(amount, true); // RW 0x782AA4(amount, 1) on Player + 8
}

void PlayerExperienceAwardSink::scoreSkillPoints(Player &player, float amount)
{
	player.getScoreKeeper().addSkillPointsEarned(amount); // RW 0x79DBA1: + 0xF8 (addss)
}

void ExperienceWorld::award(Player &player, float value)
{
	++m_counters.skillPointAwards;
	m_counters.skillPointsAwarded = SimMath::addf32(m_counters.skillPointsAwarded, value);
	if (!m_sink)
	{
		++m_counters.skillPointsDropped;
		return;
	}
	if (m_logic.economy().context().scoring) // RW 0x79DBA1: only while scores are kept (GameLogic + 0x98)
	{
		m_sink->scoreSkillPoints(player, value);
	}
	m_sink->addSkillPoints(player, value); // RW 0x782AA4(value, 1)
}

bool ExperienceWorld::awardSkillPointsForDamage(Object &attacker, Object &victim, float fraction)
{
	Player *player = attacker.getControllingPlayer(); // RW 0x68BE1B
	if (!player)
	{
		return false;
	}
	if (victim.isUnderConstruction()) // RW 0x6AAFE3
	{
		return false;
	}
	auto attacks = [](const Object &o) { return o.isKindOfName("CAN_ATTACK") || o.isKindOfName("SUPPORT"); };
	bool qualifies = false;
	if (attacks(attacker) && !attacker.isKindOfName("STRUCTURE") && (!attacker.isKindOfName("MONSTER") || !attacker.isKindOfName("SUMMONED")))
	{
		qualifies = true;
	}
	else
	{
		const Object *horde = attacker.getHordeObject(false); // RW 0x6AB025
		if (horde && attacks(*horde))
		{
			qualifies = true;
		}
		else if (attacker.isKindOfName("HERO"))
		{
			qualifies = true;
		}
	}
	if (!qualifies)
	{
		return false;
	}
	if (victim.getControllingPlayer() == player) // RW 0x6AB071
	{
		return false;
	}
	const ExperienceTracker *t = victim.getExperienceTracker();
	if (!t)
	{
		return false;
	}
	const float value = SimMath::mulf32((float)t->getExperienceValue(attacker, false), fraction); // RW 0x6AB083 .. 0x6AB08C
	if (!(value > 0.0f))
	{
		return false;
	}
	award(*player, value);
	return true;
}

bool ExperienceWorld::awardSkillPointsForLoss(Player &victimPlayer, Object *killer, Object &victim)
{
	if (!killer)
	{
		return false;
	}
	if (victim.isUnderConstruction()) // RW 0x6AB0E4
	{
		return false;
	}
	float value = 0.0f;
	if (victim.getControllingPlayer() == &victimPlayer) // RW 0x6AB100
	{
		if (const ExperienceTracker *t = victim.getExperienceTracker())
		{
			value = (float)t->getOwnGuysDieValue(*killer);
		}
	}
	award(victimPlayer, value);
	return true;
}

std::vector<std::string> ExperienceWorld::report() const
{
	const Counters &c = m_counters;
	std::vector<std::string> out;
	out.push_back("[S-630] experience: the ExperienceTracker, the level chains, the kill awards (Object::scoreTheKill RW 0x6955BC -> RW 0x695475), the horde pools "
		"(HordeContain RW 0x873A02) and the delayed level grants (RW 0x821567) run; " + std::to_string(c.levelsGranted) + " levels granted, " +
		std::to_string(c.unitLevelUps) + " unit and " + std::to_string(c.heroLevelUps) + " hero level-ups counted for the score keeper (not hashed, S-631)");
	out.push_back("[S-631] skill points: " + std::to_string(c.skillPointAwards) + " awards (" + std::to_string(c.skillPointsDropped) +
		" dropped with the sink removed; the default sink adds them to the player's rank points, Player + 8 RW 0x782AA4, and its ScoreKeeper, RW 0x79DBA1), "
		"the score keeper's level-up counts are kept here, not in a ScoreKeeper");
	out.push_back("[S-634] level grant (RW 0x821319): " + std::to_string(c.levelUpFxEvents) + " LevelUpFx calls emitted to the FXEventLog (RW 0x8211EC; "
		"played by LiveFX); not ported: " +
		std::to_string(c.levelUpOclNotCreated) + " LevelUpOCL not created, " + std::to_string(c.informUpdateModuleNotRun) +
		" InformUpdateModule calls (BannerCarrierUpdate RW 0x89A468), " + std::to_string(c.emotionsNotRun) + " EmotionType emotions (RW 0x68F37F), " +
		"the selection decal (client, RW 0x672C32); " + std::to_string(c.createAHeroRanks) + " Create-A-Hero ranks built their command sets (RW 0x809FFB, lane HERO-2)");
	out.push_back("[S-635] experience details not identified: the attacker's skill point veto (RW 0x8A4ED8: module slot 0x68, vslot 0x20) is not evaluated; the experience "
		"listener module of a kill (RW 0x88305B: slot 0xA4) is ShareExperienceBehavior (lane HERO-2; " + std::to_string(c.listenerCalls) + " shares)");
	return out;
}

void ExperienceWorld::crc(StateHasher &hasher) const
{
	hasher.addU32((std::uint32_t)m_pending.size());
	for (const Pending &p : m_pending)
	{
		hasher.addU32(p.id);
		hasher.addString(p.level ? p.level->m_name : std::string());
		hasher.addBool(p.feedback);
	}
	hasher.addBool(m_suspended);
}
