// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Include/GameLogic/ExperienceTracker.h).
//
// ExperienceTracker (lane XP-1): the experience and veterancy level of one object. RotWK replaced ZH's four veterancy levels and template arrays with
// the ExperienceLevel chains of GameLogic/ExperienceLevels.h; the class keeps ZH's name and role.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * the object's tracker is made in the Object constructor (RW 0x69A606: 0x40 bytes, RW 0x79D5E7) on the base constructor RW 0x79D3DA(template):
//     +0x08 the current level's name, +0x0C its name key, +0x10 the experience (float 0), +0x14 the experience value (the level's ExperienceAward, 0),
//     +0x18 ExperienceAwardOwnGuysDie (-1), +0x1C the experience scalar (1.0), +0x20 "gained a veterancy level" (false), +0x24 the rank (0), +0x28 the level
//     cap (0 = none), +0x2C the rank scalar helper (RW 0x79D34E: factor 1.0, base rank 1, the template's ExperienceScalarTable through RW 0x6891DA),
//     +0x30 the name key of the template's name, +0x34 the object, +0x38 the experience sink (0), +0x3C the default feedback flag (1);
//   * initObject (RW 0x693F27): gainLevel(feedback false) (RW 0x79D7AD) then the helper's base rank = the rank (RW 0x79D764);
//   * getExperienceValue (RW 0x79D1DB, killer, flag): 0 when the killer is an ally of the object and the flag is clear, else +0x14;
//     the own-guys-die value (RW 0x79D201, killer): 0 for an ally, else +0x18 with -1 read as 0;
//   * isAcceptingExperiencePoints (RW 0x79D322): false for a TEMPORARILY_DEFECTED (status 62) object, else IsTrainable (template + 0x5F7, default TRUE:
//     RW 0x740063) or a sink;
//   * the rank scalar (RW 0x79D4B9): the value unchanged when GameLogic + 0x114 (the game kind) is 3 or the object is a STRUCTURE, else value * the helper's
//     factor (x87 fmul under PC24); the factor is table[clamp(rank - base, 0, size - 1)] (RW 0x79D3B0 / 0x79D663; 1.0 for an empty table), recomputed when
//     the rank or the base changes (RW 0x79D717, 0x79D700);
//   * addExperiencePoints (RW 0x79D833, value, scaleByRank, scaleByScalar, feedback, unused): while there is a sink object, value *= this scalar and the
//     sink's tracker takes it with feedback true, unchanged scaling flags, and the last flag false; the multiplayer multiplier is GameData MultiPlayUnitXPMult (or
//     MultiPlayBuildingXPMult for a STRUCTURE) at the live playable count (RW 0x6A8630) in a multiplayer game (RW 0x625456), else 1.0; then RW 0x79D68D: not
//     trainable or the rank at the level cap (cap > 0): nothing; x = value, or scalar * value * multiplier when scaleByScalar (SSE single, that order);
//     scaleByRank: x = rankScalar(x); experience += x (SSE); then the level check (RW 0x79D141);
//   * the level check (RW 0x79D141, feedback): with the template's level list, t = (int)trunc(experience); while the next level (ExperienceLevelSystem::nextLevel)
//     has RequiredExperience <= t, onLevelReached(level, feedback) (vslot 0x10). Returns the rank of the last level reached, else the rank;
//   * onLevelReached (object tracker vslot 0x10, RW 0x79DA9D): TheDelayedExperienceLevelGrantSystem.queue(level, object, feedback) (RW 0x8215C2), the
//     drawable's selection decal (client), the level's name, setRank(level.Rank) (RW 0x79D72A: a non-zero rank that differs from the current one); a rank
//     above 1 on an object that is not a STRUCTURE and never leveled before sets +0x20 and counts a hero (KindOf HERO) or unit (not HORDE_MEMBER) level-up
//     in the owner's score keeper (RW 0x79DB50 / 0x79DB6B, only while scores are kept);
//   * the level grant (RW 0x821319, delayed: see ExperienceWorld): every AttributeModifiers list through Object::addAttributeModifier(name, -1), every
//     Upgrades entry through Object::giveUpgrade, the Create-A-Hero rank (not ported), with feedback and logic frame >= 10 the LevelUpFx / LevelUpOCL
//     (RW 0x8211EC) and the special model condition LEVELED (210) for 3 * LOGICFRAMES_PER_SECOND frames, then setLevelTemplate (RW 0x79D804: name, award,
//     own-guys-die value, rank), the ModelConditionState flags, InformUpdateModule (the BannerCarrierUpdate module, RW 0x89A468: not ported), EmotionType;
//   * applyLevel (RW 0x79D76D, level, feedback, setBase): onLevelReached, experience = (float)RequiredExperience, setRank, and with setBase the base rank;
//     gainLevel (RW 0x79D7AD) applies the next level (setBase false); gainExpForLevel (RW 0x79DA0A, n, feedback, unused) adds, n times, the experience to
//     the next level (RW 0x68A274) as addExperiencePoints(x, false, false, feedback, unused) until there is none;
//   * setExperienceAndLevel (RW 0x79D8EF, value, setBase): through the sinks, trainable only: experience = value, the level check with +0x3C, and with
//     setBase a level that differs from the base becomes the base;
//   * reset (RW 0x79DA66): no sink, experience 0, scalar 1, no level, +0x20 false, gainLevel(feedback true).
// DONOR FACTS (ZH ExperienceTracker.cpp): the sink indirection, isTrainable, the edge-triggered level change.

#pragma once

#include "GameLogic/ObjectTypes.h"

#include <string>
#include <vector>

class Object;
class StateHasher;
struct ExperienceLevelTemplate;
struct ExperienceScalarTable;

class ExperienceTracker
{
public:
	explicit ExperienceTracker(Object *parent); ///< RW 0x79D5E7

	int getExperienceValue(const Object &killer, bool evenForAllies) const; ///< RW 0x79D1DB
	int getOwnGuysDieValue(const Object &killer) const;                    ///< RW 0x79D201
	bool isTrainable() const;                                              ///< RW 0x79D2CD
	bool isAcceptingExperiencePoints() const;                              ///< RW 0x79D322
	float scaleForRank(float value) const;                                 ///< RW 0x79D4B9

	void addExperiencePoints(float value, bool scaleByRank, bool scaleByScalar, bool feedback, bool unused = false); ///< RW 0x79D833
	void setExperienceAndLevel(float value, bool setBase);                 ///< RW 0x79D8EF
	bool gainExpForLevel(int levels, bool feedback, bool unused = false);   ///< RW 0x79DA0A
	void gainLevel(bool feedback);                                         ///< RW 0x79D7AD
	void gainLevels(int levels, bool feedback);                            ///< RW 0x79D7E5
	void applyLevel(const ExperienceLevelTemplate &level, bool feedback, bool setBase); ///< RW 0x79D76D
	int experienceForNextLevel(int *nextRank) const;                       ///< RW 0x68A274
	void setLevelTemplate(const ExperienceLevelTemplate &level);           ///< RW 0x79D804 (the delayed grant)
	void resetBaseToRank();                                                ///< RW 0x79D764
	void reset();                                                          ///< RW 0x79DA66
	void restoreBaseRank(int rank) { setBaseRank(rank); }                  ///< lane HERO-1: RW 0x79D745 called by the revive (RW 0x781574)
	void setExperienceSink(ObjectID sink) { m_sink = sink; }
	void setExperienceScalar(float scalar) { m_scalar = scalar; }
	void setLevelCap(int cap) { m_levelCap = cap; }

	float getExperience() const { return m_experience; }
	// lane HERO-2: +0x3C written directly by ToggleMountedSpecialAbilityUpdate's replacement (RW 0x8B14C8 / 0x8B14EC: 0 around the copy, then 1)
	void setDefaultFeedback(bool on) { m_defaultFeedback = on; }
	// lane HERO-2, RW 0x80AA8A: a Create-a-Hero's tracker lists the levels of "CreateAHero_<unique id>" (its level chain, RW 0x80AA38) instead of its template's
	void setLevelTargetName(const std::string &name) { m_templateName = name; }
	const std::string &levelTargetName() const { return m_templateName; }
	int getRank() const { return m_rank; }
	int getLevelCap() const { return m_levelCap; }
	float getExperienceScalar() const { return m_scalar; }
	const std::string &getLevelName() const { return m_levelName; }
	bool hasGainedLevel() const { return m_leveled; }
	ObjectID getExperienceSink() const { return m_sink; }
	float getRankFactor() const { return m_rankFactor; }
	int getBaseRank() const { return m_baseRank; }
	void crc(StateHasher &hasher) const;

private:
	int checkLevels(bool feedback);                                         ///< RW 0x79D141
	friend struct XpTestAccess; // lane XP-1 tests: isolated state hash mutations
	friend struct ExperienceTrackerTestAccess; // lane INTEG-1 (Horde2TestUtil.h): the rank of a fixture without ExperienceLevel data (RW 0x79D72A setRank)
	void onLevelReached(const ExperienceLevelTemplate &level, bool feedback); ///< RW 0x79DA9D
	void addInternal(float value, bool scaleByRank, bool scaleByScalar, bool feedback, float multiplier); ///< RW 0x79D68D
	void setRank(int rank);                                                 ///< RW 0x79D72A
	void setBaseRank(int rank);                                             ///< RW 0x79D745
	void setHelperBase(int rank);                                           ///< RW 0x79D700
	float factorFor(int rank) const;                                        ///< RW 0x79D663
	bool multiplayerGame() const;
	const ExperienceLevelTemplate *nextLevel() const;

	Object *m_parent;
	std::string m_templateName;                  ///< +0x30 (its name key in RW)
	std::string m_levelName;                     ///< +0x08 / +0x0C
	float m_experience = 0.0f;                   ///< +0x10
	int m_experienceValue = 0;                   ///< +0x14
	int m_ownGuysDieValue = -1;                  ///< +0x18
	float m_scalar = 1.0f;                       ///< +0x1C
	bool m_leveled = false;                      ///< +0x20
	int m_rank = 0;                              ///< +0x24
	int m_levelCap = 0;                          ///< +0x28
	float m_rankFactor = 1.0f;                   ///< helper + 0x08
	int m_baseRank = 1;                          ///< helper + 0x0C
	const ExperienceScalarTable *m_table = nullptr; ///< helper + 0x10
	ObjectID m_sink = INVALID_ID;                ///< +0x38
	bool m_defaultFeedback = true;               ///< +0x3C
};
