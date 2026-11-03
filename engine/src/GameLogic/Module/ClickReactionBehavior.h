// OpenBFME. GPL-3.0.
// Ported from RotWK 2.01 (game.dat) with the BFME2 decomp as the shape (decomp-progress bfme2 Code/GameEngine/Source/GameLogic/Object/Behavior/
// ClickReactionBehavior.cpp: xfer, the update Rva0045969F and the click Rva004596F9; BFME1 decomp ClickReactionBehaviorRva001F7CC0.cpp: the clear). ZH has no
// such class.
//
// ClickReactionBehavior (lane MODCOMPAT-MOD1; RotWK 2.01: the mountain trolls, the creeps' trolls): an idle unit its owner selects plays a "react" animation
// (model conditions REACT_1 .. REACT_5); selecting it again and again fast enough plays the angrier REACT_4 / REACT_5.
//
// SIDE: SIMULATION. Retail runs the click in the logic, from the player's MSG_CREATE_SELECTED_GROUP (GameLogic::logicMessageDispatcher RW 0x77A0FA ->
// selectObject RW 0x625685), with a GetGameLogicRandomValue draw, and the reaction is the object's own model condition state (hashed, saved).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden: create RW 0x64AB88, data RW 0x64ABC0, table RW 0xC57B28, mask 1):
//   * data: create RW 0x64ABC0 (0x20 bytes), constructor RW 0x85C655 (vtable RW 0xC07A30): ClickReactionTimer +0x08 = 600, ReactionFrames1 .. 5 +0x0C ..
//     +0x1C = 0. Table RW 0xC57B28: the six rows, each INI::parseInt (RW 0x42EC5E -> 0x42E9D7: plain frames, no ms conversion).
//   * module: create RW 0x64AB88 (0x2C bytes), constructor RW 0x85C682 over UpdateModule RW 0x653114: vtables primary RW 0xC57A8C (xfer slot 3 RW 0x85C5DE),
//     behaviour interface + 0x0C RW 0xC579D0 (slot 0x84 RW 0x8A18E0 hands out + 0x20), update + 0x10 RW 0xC579C4, the click interface + 0x20 RW 0xC579B8
//     (slot 1 onClick RW 0x85C75E, slot 2 clearReaction(bool) RW 0x85C8C9); + 0x24 the click meter (0), + 0x28 the reaction countdown (0); the update wakes
//     next frame (RW 0x850C32(obj, 1)).
//   * Object's click interface RW 0x68C409: the first module whose behaviour interface slot 0x84 answers. Callers: selectObject RW 0x625685 (slot 1, when the
//     selecting player controls the object, RW 0x68B678), HitReactionBehavior's update RW 0x85C55A (slot 2 with false, after it set HIT_REACTION).
//   * onClick RW 0x85C75E: nothing without a drawable (RW 0x70E013), with weapon set flag RAMPAGE (Object + 0x38C bit 8, RW 0x68BE7D), with model condition
//     RAMPAGE_ANIMATION_ONLY (162: + 0x120 bit 2) or HIT_REACTION (172: bit 12), without an AI (+ 0x260) or an AI not idle (slot 0x1B8), or with
//     ClickReactionTimer <= 0. Then meter += timer, meter = min(meter, timer * 5); a running countdown (> 0) returns; reaction = meter / timer (idiv),
//     <= 3: GetGameLogicRandomValue(1, 3) (RW 0x6D328E, ClickReactionBehavior.cpp line 116); reaction 1 .. 5 sets REACT_n (164 .. 168, each only when
//     clear, RW 0x68B53C after), another value REACT_5 the same way; the drawable's flush RW 0x67449C(0); countdown = ReactionFrames[reaction - 1] when
//     reaction - 1 is 0 .. 4.
//   * clearReaction RW 0x85C8C9(apply): nothing without a drawable; countdown = 0; REACT_1 .. REACT_6 (164 .. 169, mask 0x3F0 of word 5) cleared
//     (clearAndSetModelConditionFlags RW 0x5E3B79 with an empty set); apply: the drawable's flush RW 0x67449C(0).
//   * update RW 0x85C704: no drawable or no AI: sleep forever. Meter > 0: meter - 1. Countdown > 0: countdown - 1; when it reached 0, or the AI is not idle:
//     clearReaction(true). Returns 1 (every frame).
//   * xfer RW 0x85C5DE: version (1, 1), UpdateModule (RW 0x850C60), + 0x24, + 0x28 (int).
//   * selectObject RW 0x625685(object, force, player mask, select sound): runs for an object RW 0x6937C0 calls selectable (not a soldier of a HORDE producer
//     (+ 0x78) with status UNSELECTABLE (3), Object::isSelectable RW 0x68DE58, not KindOf STRUCTURE (7)) or when `force` (the message's createNewGroup and
//     its first object, RW 0x77A0D7 .. 0x77A0E5); for each player of the mask: ... the click when that player controls the object.
// WHAT IS INFERENCE (OpenBFME):
//   * the drawable exists for every logic object (RW 0x628882 makes one per object, as S-1024 / S-1041 take it): the no-drawable exits never apply here;
//     the flush RW 0x67449C is the client's (Object::flushDrawableModelConditions);
//   * the first module answering slot 0x84 is taken to be a ClickReactionBehavior (the only class found answering it);
//   * the engine's selection message keeps its own selection rules (GameLogicDispatch::createSelectedGroup); the click runs for each object argument that
//     passes RW 0x6937C0's test (or is the forced first) and that the sending player controls.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

class ModuleFactory;
class Object;

class ClickReactionBehaviorModuleData : public ModuleData
{
public:
	int m_clickReactionTimer = 600;            ///< +0x08
	int m_reactionFrames[5] = { 0, 0, 0, 0, 0 }; ///< +0x0C .. +0x1C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ClickReactionBehavior : public UpdateModule
{
public:
	OBFME_MODULE_XFER(ClickReactionBehavior)
	ClickReactionBehavior(Thing *thing, const ClickReactionBehaviorModuleData *data); // RW 0x85C682
	UpdateSleepTime update() override;                                                 // RW 0x85C704
	void crc(StateHasher &hasher) const override;

	void onClick();                    // click interface slot 1: RW 0x85C75E
	void clearReaction(bool apply);    // click interface slot 2: RW 0x85C8C9

	int clickMeter() const { return m_clicks; }    ///< + 0x24
	int countdown() const { return m_countdown; }  ///< + 0x28

	// Object's click interface (RW 0x68C409): the object's first ClickReactionBehavior, or null
	static ClickReactionBehavior *find(Object &obj);
	// selectObject RW 0x625685's object test RW 0x6937C0 (the selection message's click gate)
	static bool selectableForClick(const Object &obj);

	static void registerModule(ModuleFactory &modules);

private:
	const ClickReactionBehaviorModuleData *m_data;
	int m_clicks = 0;    ///< + 0x24
	int m_countdown = 0; ///< + 0x28
};
