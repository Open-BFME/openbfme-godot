// OpenBFME. GPL-3.0.
//
// ExperienceWorld (lane XP-1): the per-game experience state outside the objects: TheDelayedExperienceLevelGrantSystem (the level grants the trackers queue
// and the logic frame applies), the player side of the experience awards (skill points), and the run report of what is not ported.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * TheDelayedExperienceLevelGrantSystem is RW 0xDE8AC0 (constructor RW 0x8214C1: the list +0x10, the "suspended" byte +0x14 false). queue (RW 0x8215C2,
//     level, object, feedback): unless suspended, { object id, level, feedback } is appended. Its update (vslot 0x28, RW 0x821567) runs in the logic frame's
//     phase 5 after the destroy list (RW 0x62EBD9, after three other subsystems and before TheSkirmishAIManager RW 0x62EBEF): every entry in order whose object
//     still exists and is not effectively dead (Object + 0x458 bit 0) unless it is a PHANTOM_STRUCTURE (status 88) is granted (RW 0x821319, the steps in
//     GameLogic/Object/ExperienceTracker.h), then the list is cleared (RW 0x862535);
//   * the skill points of a damaging hit (ActiveBody::attemptDamage RW 0x8C46B3 .. 0x8C46EA): fraction = actualDamageClipped / the victim's maximum health
//     (SSE), capped at 1.0; when > 0 the attacker's player gets (RW 0x68BE18 -> Player RW 0x6AAFC3, attacker, victim, fraction): nothing for a victim under
//     construction (status 2); the attacker qualifies when (CAN_ATTACK or SUPPORT) and not STRUCTURE and (not MONSTER or not SUMMONED), else when its horde
//     (RW 0x693A1A) is CAN_ATTACK or SUPPORT, else when it is a HERO; a qualifying attacker can still be vetoed by the first module with the slot 0x68
//     interface whose vslot 0x20 answers true (RW 0x8A4ED8, not identified: stop S-635); then, when the victim's player is not the attacker's player, value =
//     (float)victim.getExperienceValue(attacker, false) * fraction (SSE); value > 0: the score keeper's skill point total += value (RW 0x79DBA1, only while
//     scores are kept) and Player + 8 .addSkillPoints(value, true) (RW 0x782AA4);
//   * the skill points of a loss (Object::scoreTheKill RW 0x695758 -> RW 0x6AB0D0 on the VICTIM's player when its template is a playable side): for a
//     victim of that player not under construction, value = (float)victim.getOwnGuysDieValue(killer); the score keeper += value and addSkillPoints(value,
//     true) (also when 0).
// The player's rank / skill points (Player + 8, RW 0x782AA4) are lane SPELL-1's: this lane calls them through ExperienceAwardSink; the default sink
// (PlayerExperienceAwardSink, lane INTEG-1) forwards to Player::science() and the player's ScoreKeeper. With the sink removed the awards are counted and dropped.

#pragma once

#include "GameLogic/ObjectTypes.h"

#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class StateHasher;
struct ExperienceLevelTemplate;

// the player side of the awards: Player + 8 RW 0x782AA4(amount, true) and the score keeper's total (RW 0x79DBA1)
class ExperienceAwardSink
{
public:
	virtual ~ExperienceAwardSink() = default;
	virtual void addSkillPoints(Player &player, float amount) = 0;      ///< RW 0x782AA4(amount, 1) on Player + 8
	virtual void scoreSkillPoints(Player &player, float amount) = 0;    ///< RW 0x79DBA1 on Player + 0x3DC (the caller checks that scores are kept)
};

// lane INTEG-1: the retail sink, installed by default (S-631 / S-527 resolved): Player + 8 is the player's PlayerScience (lane SPELL-1; RW 0x782AA4 =
// PlayerScience::addSkillPoints, applyScalar = 1) and the score keeper's skill point total (RW 0x79DBA1, + 0xF8) is ScoreKeeper::addSkillPointsEarned
class PlayerExperienceAwardSink final : public ExperienceAwardSink
{
public:
	void addSkillPoints(Player &player, float amount) override;
	void scoreSkillPoints(Player &player, float amount) override;
};

class ExperienceWorld
{
public:
	explicit ExperienceWorld(GameLogic &logic);
	void reset();

	// ---- TheDelayedExperienceLevelGrantSystem ----
	void queueLevelGrant(const ExperienceLevelTemplate &level, Object &obj, bool feedback); ///< RW 0x8215C2
	void update();                                                                           ///< RW 0x821567
	void setSuspended(bool on) { m_suspended = on; }
	size_t pendingGrants() const { return m_pending.size(); }
	// RW 0x821319: the grant itself (also for tests)
	void grantLevel(const ExperienceLevelTemplate &level, Object &obj, bool feedback);

	// ---- the player side ----
	void setAwardSink(ExperienceAwardSink *sink) { m_sink = sink; } ///< nullptr drops the awards (counted)
	ExperienceAwardSink *awardSink() const { return m_sink; }
	// RW 0x68BE18 -> 0x6AAFC3
	bool awardSkillPointsForDamage(Object &attacker, Object &victim, float fraction);
	// RW 0x6AB0D0 (on the victim's player)
	bool awardSkillPointsForLoss(Player &victimPlayer, Object *killer, Object &victim);

	struct Counters
	{
		unsigned long long levelsGranted = 0;
		unsigned long long levelUpFxEvents = 0;        ///< LevelUpFx calls emitted to the FXEventLog (RW 0x8211EC, lane INTEG-1)
		unsigned long long levelUpOclNotCreated = 0;
		unsigned long long informUpdateModuleNotRun = 0; ///< InformUpdateModule (BannerCarrierUpdate RW 0x89A468)
		unsigned long long emotionsNotRun = 0;         ///< EmotionType (RW 0x68F37F)
		unsigned long long createAHeroRanks = 0;       ///< RW 0x809FFB
		unsigned long long skillPointAwards = 0;
		unsigned long long skillPointsDropped = 0;     ///< no sink installed
		unsigned long long unitLevelUps = 0, heroLevelUps = 0; ///< the score keeper's counts (RW 0x79DB6B / 0x79DB50)
		unsigned long long listenerCalls = 0;          ///< RW 0x88305B: a module's experience listener (slot 0xA4) not identified
		float skillPointsAwarded = 0.0f;
	};
	Counters &counters() { return m_counters; }
	const Counters &counters() const { return m_counters; }
	std::vector<std::string> report() const;

	// ---- the level-up FX calls (lane INTEG-1, S-634) ----
	// RW 0x8211EC (called by the grant RW 0x8213C6 with feedback from logic frame 10 on): for every LevelUpFx entry in order, FXList::doFXObj(fx, obj, null)
	// (RW 0x4B1B5A) when the entry has no bone or the object has no drawable (RW 0x70E013), else FXList::doFXPos(fx, the bone's position, its transform, 0,
	// null) (RW 0x494615) with the transform from the drawable's draw modules (RW 0x672B5B: the first that knows the bone; none: the identity it started from).
	// They are client calls: each goes to the logic's FXEventLog (lane FX-2) as an OBJECT_FX of site "LevelUpFx" carrying the entry's bone; LiveFX plays it.
	void crc(StateHasher &hasher) const;

private:
	friend struct XpTestAccess; // lane XP-1 tests: isolated state hash mutations
	struct Pending
	{
		ObjectID id = INVALID_ID;
		const ExperienceLevelTemplate *level = nullptr;
		bool feedback = false;
	};
	void award(Player &player, float value);

	GameLogic &m_logic;
	std::vector<Pending> m_pending;
	bool m_suspended = false;
	PlayerExperienceAwardSink m_playerSink;
	ExperienceAwardSink *m_sink = &m_playerSink; // tests may install their own
	Counters m_counters;
};
