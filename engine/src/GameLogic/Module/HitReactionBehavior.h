// OpenBFME. GPL-3.0.
//
// HitReactionBehavior (lane MODULES-2; 190 retail templates): a unit that loses health while idle plays a hit reaction (model conditions HIT_REACTION and
// HIT_LEVEL_1 / 2 / 3) for a while. RotWK only; ported from the binary (caveat S-001).
//
// TARGET FACTS (RotWK game.dat):
//   * data (table RW 0xC57920): HitReactionLifeTimer1 / 2 / 3 (+8 / + 0xC / + 0x10, parseDurationUnsignedInt: the retail values 45 / 90 / 60 are written as 30 fps
//     frames but parse as milliseconds, so they are 1 logic frame each), HitReactionThreshold1 / 2 / 3 (+ 0x14 / + 0x18 / + 0x1C, parseReal: health lost),
//     FastHitsResetReaction (+ 0x20), HitsParalyze (+ 0x21).
//   * the module (constructor RW 0x85C296): + 0x20 the reaction countdown (0), + 0x24 the last health (0.0); wake next frame. update RW 0x85C30F, every frame
//     (UPDATE_SLEEP_NONE; forever when the object is dead or has no AI, no drawable or no body):
//       1. a running countdown goes down; at 0, or when the object died or its AI is not idle (AI slot 0x1B8), the reaction ends: when the drawable's flag says
//          it is shown, HIT_REACTION and HIT_LEVEL_1 .. 3 are cleared (each that is set, RW 0x68B53C after each), then the countdown is 0. Without
//          FastHitsResetReaction nothing else happens while a countdown runs;
//       2. a hit: the AI idle or HitsParalyze, and the health below the last health. A shown reaction is cleared first; the loss picks the level (>= Threshold3:
//          3, >= Threshold2: 2, >= Threshold1: 1, else none); the countdown becomes that level's LifeTimer; with a countdown above 0 and the flag clear,
//          HIT_REACTION and HIT_LEVEL_n are set, RW 0x68C409's interface is told (slot 8, 0), and HitsParalyze disables the object PARALYZED (4) until now +
//          the countdown (RW 0x6907F1);
//       3. the last health becomes the health.
//   * THE DRAWABLE STATUS BIT (the brief's question, answered): the flag is Drawable + 0x26C bit 12. The drawable keeps a copy of the object's 19 model condition
//     words at + 0x258 (RW 0x679512 copies them on every Object model condition notify RW 0x68B53C), so + 0x26C bit 12 is word 5 bit 12 = model condition 172,
//     HIT_REACTION, as the drawable last saw it. Every model condition write of the logic is followed by RW 0x68B53C, so the copy equals the object's own bit:
//     the port reads the object's HIT_REACTION.
//
// NOT PORTED / INFERENCE (stop S-1024): the interface RW 0x68C409 finds (the first module answering behavior slot 0x84) and its slot 8(0) after a reaction are
// not identified (noted when a reaction starts); the drawable refresh RW 0x67449C(0) is the client's; the drawable exists for every object (RW 0x628882 makes
// one for each), so the "no drawable" exit never applies in the logic; the drawable's flag is read as the object's HIT_REACTION (the synced copy above).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>
#include <vector>

class ModuleFactory;
class StateHasher;

class HitReactionBehaviorModuleData : public ModuleData
{
public:
	unsigned m_lifeTimer[3] = { 0, 0, 0 };     ///< + 8 / + 0xC / + 0x10 (frames)
	float m_threshold[3] = { 0.0f, 0.0f, 0.0f }; ///< + 0x14 / + 0x18 / + 0x1C
	bool m_fastHitsResetReaction = false;      ///< + 0x20
	bool m_hitsParalyze = false;               ///< + 0x21
	static void buildFieldParse(MultiIniFieldParse &p);
};

class HitReactionBehavior : public UpdateModule
{
public:
	HitReactionBehavior(Thing *thing, const HitReactionBehaviorModuleData *data); ///< RW 0x85C296
	UpdateSleepTime update() override;                                           ///< RW 0x85C30F
	void crc(StateHasher &h) const override;
	int countdown() const { return m_countdown; }
	float lastHealth() const { return m_lastHealth; }
	static void registerClass(ModuleFactory &modules);
	static std::vector<std::string> stopLines();

private:
	void clearReaction(Object &obj); ///< the four model conditions, each set one cleared
	const HitReactionBehaviorModuleData *m_data;
	int m_countdown = 0;        ///< + 0x20
	float m_lastHealth = 0.0f;  ///< + 0x24
};
