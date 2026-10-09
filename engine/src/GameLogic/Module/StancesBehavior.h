// OpenBFME. GPL-3.0.
//
// StancesBehavior and the StanceTemplate store (lane INTEG-1, stop S-585): a unit's stance (Battle, Aggressive, HoldGround, Porcupine, HoldGroundMoving) is a
// ModifierList in the unit's attribute modifier pool (lane XP-1) plus, for a horde, the melee behaviour its HordeContain runs (lane HORDE-2).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read with capstone, the Ghidra server was down):
//   * the stance names (RW 0xC53660, parseIndexList order): Uninitialized, Battle, Aggressive, HoldGround, Porcupine, HoldGroundMoving;
//   * block StanceTemplate <name> (RW 0x835967, INI block table RW 0xDADB64): the name key; a name already in the store (TheStanceTemplateStore, RW 0xDE8C80 + 0xC,
//     a map by name key; RW 0x6B4E57) throws "%s(%d) : Stance %s already defined" (RW 0xC5373C); else a template (0x34 bytes: the key, six 8-byte entries, one
//     per stance, RW 0x835602) is parsed with the one-field table { Stance, RW 0x83555A } and inserted (RW 0x83587A);
//   * Stance <name> (RW 0x83555A): the next token through parseIndexList over the names (RW 0x42B999), then the entry's sub-block to End with the table
//     { AttributeModifier: parseAsciiString (RW 0x548990) at +0, MeleeBehavior: RW 0x86C30A (HordeContain's MeleeBehavior grammar) at +4 };
//   * module data (field table RW 0xC58CC8): StanceTemplate, parseAsciiString at +8;
//   * the module (ctor RW 0x861FD9): stance +0x30 = 0 (Uninitialized), a listener list +0x20; update (interface slot 0, RW 0x8622E9): IS_LEAVING_FACTORY (status 0x5A)
//     sleeps one frame; else a stance of 0 becomes Battle (setStance(1)); then sleeps forever (0x3FFFFFFF);
//   * setStance (RW 0x8620DD, new): equal to the current: false. The horde interface (contain slot 0x7C): when the current stance is Porcupine and the contain is a
//     porcupine formation (slot 0xF0, RW 0x872BB8: data + 0x238) that can toggle (slot 0x5C, RW 0x8700AC), it toggles back (slot 0x60, RW 0x86C7F6; the swap
//     itself sets Battle, RW 0x87629F) and the interface is looked up again. New HoldGround: the AI's idle command from the player (AI + 0x20, RW 0x5E821A(0):
//     command 5, ZH AICMD_IDLE). Without a StanceTemplate name, or a template of that name (RW 0x83585B): false. The current entry's AttributeModifier and the new
//     one's differ: Object::removeAttributeModifier(current) (RW 0x68F259) when set, Object::addAttributeModifier(new, -1) (RW 0x68F1A8) when set. The horde gets
//     the new entry's MeleeBehavior (slot 0x260, RW 0x86C40D: that data, else the contain's own MeleeBehavior, else Swarm). Then the stance is stored; when the old
//     stance was 0 or its class (RW 0x861D8E: Aggressive 2; HoldGround, Porcupine, HoldGroundMoving 3; else 1) differs from the new one's: the listeners are told
//     (RW 0x861F97 with RW 0x9F9E76: this, new class, old class) and, when the AI is idle (AI slot 0x1B8): new Battle: RW 0x662A21(0); new HoldGround or
//     Porcupine: RW 0x662A21(1); else, old Battle or HoldGround: the AI's idle command from the AI (RW 0x5E821A(2)). True;
//   * onMoveStart (RW 0x862275): HoldGround -> setStance(HoldGroundMoving), Porcupine -> setStance(Battle); onMoveEnd (RW 0x86228E): HoldGroundMoving ->
//     setStance(HoldGround); then a Battle / HoldGround / Porcupine unit that is not HIDDEN (status 0x10) with an AI: RW 0x662A21(HoldGround or Porcupine);
//   * applyMeleeBehavior (RW 0x861E17, stance): the horde gets that stance's MeleeBehavior (slot 0x260) without a stance change (HordeContain RW 0x878421);
//   * MSG_CHANGE_STANCE (1128, dispatcher RW 0x77BC59): the player's group; argument 0 is the stance; with GameData + 0x11CA the group manager gets it (RW 0x7575A0),
//     else AIGroup::setStance (RW 0x76FF27): every member's StancesBehavior (found by the class name key, RW 0x861E6E / 0x68BDA5) setStance(stance);
//   * the state (xfer RW 0x861DDF): the stance, 4 bytes ("StanceEnum").
// NOT PORTED (stop S-585, counted): RW 0x662A21 (the AI + 0x4C mode with the unit's position / angle at AI + 0x58 / + 0x1A0: the AI side of hold ground), the
// listener notice RW 0x861F97 (no listener registration identified), the callers of onMoveStart / onMoveEnd (RW 0x66A948 in RW 0x66A61A, RW 0x743882) are not
// identified without the Ghidra server, so a HoldGround unit keeps its stance while it moves; the group manager branch of MSG_CHANGE_STANCE (S-223: the port takes
// AIGroup::setStance); the scripted stance (RW 0x94D4EB) and the map object's stance (RW 0x695B93); the removal side effects of a ModifierList (S-633).
// The horde's melee behaviour runtime (RW deletes and rebuilds the behaviour object, RW 0x86C40D) is rebuilt in HordeAIUpdate::resetMeleeRuntime (Sol review).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

class GameLogicDispatch;
class ModuleFactory;
class Object;
struct MeleeBehaviorModuleData;

enum StanceType
{
	STANCE_UNINITIALIZED = 0,
	STANCE_BATTLE,
	STANCE_AGGRESSIVE,
	STANCE_HOLD_GROUND,
	STANCE_PORCUPINE,
	STANCE_HOLD_GROUND_MOVING,
	STANCE_COUNT
};

extern const char *const TheStanceNames[]; ///< RW 0xC53660, null-terminated

struct StanceTemplate
{
	struct Entry
	{
		std::string attributeModifier;                       ///< +0
		std::shared_ptr<MeleeBehaviorModuleData> meleeBehavior; ///< +4
	};
	std::string name;
	std::array<Entry, STANCE_COUNT> entries;
};

class StanceTemplateStore
{
public:
	void registerBlock(INIBlockRegistry &registry);
	void parseStanceTemplate(INI *ini); ///< RW 0x835967
	const StanceTemplate *find(const std::string &name) const; ///< RW 0x83585B
	size_t size() const { return m_templates.size(); }

private:
	std::map<std::string, std::unique_ptr<StanceTemplate>> m_templates;
};

// the store the StanceTemplate block and the modules use (RetailObjectWorld installs its own); nullptr when none
extern thread_local StanceTemplateStore *TheStanceTemplateStore;

class StancesBehaviorModuleData : public ModuleData
{
public:
	std::string m_stanceTemplate; ///< +8
	static void buildFieldParse(MultiIniFieldParse &p);
};

class StancesBehavior : public UpdateModule
{
public:
	StancesBehavior(Thing *thing, const StancesBehaviorModuleData *data);
	static void registerClass(ModuleFactory &modules);
	// MSG_CHANGE_STANCE (RW 0x77BC59 -> AIGroup::setStance RW 0x76FF27)
	static void registerHandlers(GameLogicDispatch &d);
	static StancesBehavior *of(Object &obj); ///< RW 0x861E6E / 0x68BDA5: the module by its class name

	UpdateSleepTime update() override; ///< RW 0x8622E9
	bool setStance(StanceType stance);  ///< RW 0x8620DD
	void onMoveStart();                 ///< RW 0x862275
	void onMoveEnd();                   ///< RW 0x86228E
	bool applyMeleeBehavior(StanceType stance); ///< RW 0x861E17
	StanceType getStance() const { return m_stance; }
	static int stanceClass(StanceType stance); ///< RW 0x861D8E
	void crc(StateHasher &hasher) const override;

	struct Stats
	{
		// SMOOTH-1 review r4 fix 3: process-wide diagnostics that separate logic owners (worker threads of different games) bump; relaxed atomics
		std::atomic<unsigned long long> aiHoldModeNotSet{ 0 }; ///< RW 0x662A21 calls (S-585)
		std::atomic<unsigned long long> listenerNotices{ 0 };  ///< RW 0x861F97 calls (S-585)
	};
	static Stats &stats();
	static std::vector<std::string> stopLines();

private:
	const StancesBehaviorModuleData *m_data;
	StanceType m_stance = STANCE_UNINITIALIZED; ///< +0x30
};
