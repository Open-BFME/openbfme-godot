// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// AISpecialPowerUpdate (lane MOD-4, QA-1 U2): a computer player's units use their powers on their own. A BFME class (ZH has none); the decision objects
// live in the binary's SkirmishAI\AISpecialPowers\AISPecialPower*.cpp (RW 0xC87E90 / 0xC910D0 name the files). Ported from the RotWK binary (caveat S-001);
// Open-BFME-2 has no matching source for these functions.
//
// TARGET FACTS (each read from the disassembly):
//   * the class (create RW 0x64FAD8, size 0x28, constructor RW 0x8B703A over the UpdateModule constructor RW 0x653114; vtables RW 0xC6DA14 / behaviour
//     interface RW 0xC6D958 / update interface RW 0xC6D948). Module fields: + 0x20 active, + 0x21 initialized, + 0x24 the decision object.
//   * the data (createData RW 0x64FB10, constructor RW 0x8B70DD, field table RW 0xC6DAD0): CommandButtonName + 0x08 (AsciiString), SpecialPowerAIType + 0x0C
//     (RW 0x992693: the index of the token in the 53 names of RW 0xDB84F8, -1 when none matches; the constructor does not set it), SpecialPowerRadius + 0x10 and
//     SpecialPowerRange + 0x14 (reals, -1.0 RW 0xBD19DC), RandomizeTargetLocation + 0x18 and SpellMakesAStructure + 0x19 (bools, false).
//   * init (RW 0x8B714F): the first of the 33 buttons of the object's command set (RW 0x69156B / 0x71EFA2 / 0x80C837) whose name equals CommandButtonName
//     (RW 0x4065AA). Found: active := 1; with a power on the button (+ 0x44) the power's type (+ 0x1C of its final override, RW 0x688D3C) is kept and the
//     object's first special power update module answering vslot 4 whose power has that type (RW 0x68C461 / 0x851D10) gives the range: its module data's
//     StartAbilityRange (+ 0x4C, RW 0x8513DD). Not found: a debug message only (active is not cleared). Then, when active: the decision object is made from
//     SpecialPowerAIType once (RW 0x992724; an invalid type clears active), it gets the button (+ 0xC), the range (+ 0x14) when above 0, SpecialPowerRadius
//     (+ 0x18) when above 0, SpecialPowerRange (+ 0x14) when above 0, the power type (+ 0x8). Then initialized := 1.
//   * update (RW 0x8B73B7): init unless initialized; inactive: sleep forever. The controlling player defeated (Player + 0x754, RW 0x6AAC4B): active := 0,
//     sleep 1. No decision object, or the player has no skirmish AI (RW 0x6A950B on TheSkirmishAIManager): sleep forever. No button: init again. Then the
//     decision object's slot 1 (RW 0x993019: the counter + 0x10 grows by one) with the object; no button: sleep forever. Ready (no power on the button, or RW
//     0x8B708B: not IS_LEAVING_FACTORY (status 90), canUseSpecialPower RW 0x7B1D79, the power's module RW 0x68C26D (none: active := 0) and its isReady) and
//     RW 0x993055 (GameLogicRandomValueReal(0, 1) "AISPecialPower.cpp" line 0x71 above 0.5 (RW 0xBD869C): the two data flags go to + 0x1D / + 0x1E and the
//     decision slot 0x1C answers) and the difficulty's chance (RW 0x993307 on TheSkirmishAIManager: SpecialPowerActivationProbability of the player's
//     difficulty (RW 0x6AA61B), num / den as floats >= 1.0 (RW 0xBD1908) or GameLogicRandomValue(0, den - 1) "AIDifficulty.cpp" line 0x40 < num): RW
//     0x99302D: when the counter reaches slot 0xC (5 * LOGICFRAMES_PER_SECOND, RW 0x993024) the counter clears, + 0x1C := 1 and slot 0x18 runs the button.
//     The sleep is the decision object's slot 0x10 (1 for every type, RW 0x490AC4).
//   * the decision object base (RW 0x992FD2): + 0x4 its kind, + 0x8 the power type, + 0xC the button, + 0x10 the counter, + 0x14 the range, + 0x18 the radius,
//     + 0x1C fired, + 0x1D / + 0x1E the data flags. Three ways to run the button (slot 0x18): on the object itself (RW 0x993006: RW 0x696FD2(button, 1, 0)),
//     at the target object + 0x20 (RW 0xA02806: RW 0x697890(button, target, 1, 0), then the target cleared) or at the location + 0x20 (RW 0xA01C54: RW
//     0x6979D9(button, location, 1, 0)). Source 1 is CMD_FROM_SCRIPT: SPECIAL_POWER (24) / SPELL_BOOK (38) buttons do the power with the button's options |
//     0x40000 and force = (source == 1), so canUseSpecialPower is not asked again; SET_STANCE (58) sets the button's first stance on the object's
//     StancesBehavior (RW 0x8620DD); a disabled object does nothing (RW 0x9325B4, Object + 0xA0 bit 6 aside).
//   * the decisions (slot 0x1C) ported here, with the AI's goal object (RW 0x668303: AIUpdate + 0x40 by id, the current victim: DONOR ZH SlavedUpdate's call site):
//       BASIC_SELF_BUFF (0, RW 0x9E9488): a goal; the health ratio r (a HORDE's average, horde interface slot 0x264 RW 0x86EA09; else the body's slot 0x14
//         RW 0x8C1D75) below 0.5 (RW 0xC8E250): yes; r >= 0.8 (RW 0xC8E24C): no; else the objects within 50 (RW 0xC8E254) of the object (type 0) that the
//         player has as ALLIES or ENEMIES (RW 0xC1676C mask 6) and alive (RW 0xC10E20): yes unless the enemies (Object::getRelationship ENEMIES) are fewer
//         than the others.
//       BASIC_SELF_DEBUFF (48, RW 0x9EAF26): a goal; the same scan within the range + 0x14, each side summing the threat values (RW 0x68F0EC, x87 wide, added
//         to the unsigned sum and truncated by _ftol2 RW 0xA3CFA4): yes when the enemies' sum is above the others'.
//       GOBLIN_POISON (52, RW 0x9E9438): a goal that is not STRUCTURE, MACHINE nor SHIP whose 2D squared distance (RW 0x66137C, x87) is below 10000.0.
//       STANCEBATTLE (38, RW 0x9E6C47): r <= 0.35 (RW 0xC71EC8). STANCEAGGRESSIVE (39, RW 0x9E6BA3): a StancesBehavior whose stance class (RW 0x861D8E) is not
//         2 and r > 0.35. STANCEHOLDGROUND (40) and CAPTURE_BUILDING (1): RW 0x7FEAC1, never.
//       ENEMY_TYPE_KILLER (3) / _RANGED (4) / _STRUCTURES (5) / MORGUL_BLADE (51) (RW 0x9EC0A7): a goal; the target is cleared; the kind mask (+ 0x24) is
//         filled by slot 0x20 (3 / 4: HERO, MONSTER, BIG_MONSTER, CREATE_A_HERO, RW 0x9EC08A; 5: STRUCTURE, RW 0x9EBC5A; 51: HERO, INFANTRY,
//         CREATE_A_HERO, RW 0x9EC09A); the player's ENEMIES (mask 4), alive, within the range + 0x14 of the object, near to far: a hit of the mask (RW
//         0x70C548) that slot 0x24 accepts (3 / 51: always, RW 0x8470FA; 4: a 2D squared distance above 10000.0, SSE, RW 0x9EBFF7; 5: not
//         IGNORE_FOR_VICTORY and a production update, RW 0x9EBC5F) becomes the target when there is none or its body ratio is below the target's.
//       TOGGLE_MOUNTED (9, RW 0x9EBE42): the object's team is not its player's default team; the team's members not inside a HORDE are counted as
//         CAVALRY (the template's AIKindOf + 0x530 == 3) or not; MOUNTED (model condition 214): yes when the others outnumber the cavalry; else when the cavalry
//         outnumbers the others.
//   The other 39 types are stop S-1421 (their decision answers no, each call counted).
//
// INFERENCE / NOT PORTED (stop S-1421, stopLines()): SpecialPowerAIType left unset by the data constructor is -1 here (every retail declaration sets it);
// the behaviour interface slot 0xB4 call (RW 0x8B7554: init again, from Object RW 0x694692 with the map object's properties) is the first update's init;
// RW 0x668303's AIUpdate + 0x40 is read as the AI's current victim (the ZH donor; HERO-2's DualWeaponBehavior reads the state machine's goal object);
// RW 0x68C461's other special power update classes (only SpecialAbilityUpdate answers vslot 4 here); the decision objects' xfer (+ 0x2 slot 2).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectTypes.h"

#include <array>
#include <memory>
#include <string>

class CommandButton;
class ModuleFactory;
class Object;
class SpecialPowerTemplate;
class StateHasher;

enum
{
	AI_SPECIAL_POWER_TYPE_COUNT = 53 ///< RW 0x992693 stops at 0x35
};
// RW 0xDB84F8, in the binary's order, nullptr-terminated
extern const char *const TheAISpecialPowerTypeNames[AI_SPECIAL_POWER_TYPE_COUNT + 1];

class AISpecialPowerUpdateModuleData : public ModuleData
{
public:
	std::string m_commandButtonName;      ///< + 0x08
	int m_specialPowerAIType = -1;        ///< + 0x0C (INFERENCE: the constructor leaves it unset)
	float m_specialPowerRadius = -1.0f;   ///< + 0x10 (RW 0xBD19DC)
	float m_specialPowerRange = -1.0f;    ///< + 0x14
	bool m_randomizeTargetLocation = false; ///< + 0x18
	bool m_spellMakesAStructure = false;  ///< + 0x19
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC6DAD0
	static void parseSpecialPowerAIType(INI *ini, void *instance, void *store, const void *userData); // RW 0x992693
};

// the decision object of RW 0x992724 (one class per type in retail; the type selects the slots here)
struct AISpecialPowerDecision
{
	enum Kind
	{
		KIND_SELF = 2,     ///< RW 0xA02390 base: slot 0x18 RW 0x993006
		KIND_OBJECT = 3,   ///< RW 0xA027B2 base: slot 0x18 RW 0xA02806
		KIND_LOCATION = 5  ///< RW 0xA01C6B base: slot 0x18 RW 0xA01C54
	};
	int type = -1;
	int kind = KIND_SELF;                 ///< + 0x04
	int powerType = 0;                    ///< + 0x08
	const CommandButton *button = nullptr; ///< + 0x0C
	unsigned counter = 0;                 ///< + 0x10
	float range = 0.0f;                   ///< + 0x14
	float radius = 0.0f;                  ///< + 0x18
	bool fired = false;                   ///< + 0x1C
	bool randomize = false;               ///< + 0x1D
	bool makesStructure = false;          ///< + 0x1E
	ObjectID target = INVALID_ID;         ///< + 0x20 (KIND_OBJECT)
	Coord3D location{};                   ///< + 0x20 (KIND_LOCATION)
	// the kind of each type (its base class), -1 for an invalid type
	static int kindOf(int type);
	// the types whose decision (slot 0x1C) is ported
	static bool decisionPorted(int type);
};

class AISpecialPowerUpdate : public UpdateModule
{
public:
	AISpecialPowerUpdate(Thing *thing, const AISpecialPowerUpdateModuleData *data); // RW 0x8B703A
	UpdateSleepTime update() override;                                               // RW 0x8B73B7
	void crc(StateHasher &h) const override;
	static void registerClass(ModuleFactory &modules);
	static std::vector<std::string> stopLines();

	bool active() const { return m_active; }
	bool initialized() const { return m_initialized; }
	const AISpecialPowerDecision *decision() const { return m_decision.get(); }
	const AISpecialPowerUpdateModuleData *data() const { return m_data; }

	// RW 0x9E94DB's ratio: a HORDE's member average (RW 0x86EA09), else the body's health / max health (RW 0x8C1D75), stored as a float
	static float healthRatio(const Object &obj);

	// diagnostics (not state): decisions asked, answered yes, buttons run, unported decisions met (per type)
	struct Stats
	{
		unsigned long long asked = 0, yes = 0, executed = 0, unportedDecisions = 0, unportedCommands = 0;
		std::array<unsigned long long, AI_SPECIAL_POWER_TYPE_COUNT> executedByType{};
	};
	static Stats &stats();

private:
	void init();                                                // RW 0x8B714F
	bool powerReady(const SpecialPowerTemplate *t);             // RW 0x8B708B
	bool decide(Object &obj);                                   // slot 0x1C
	void execute(Object &obj);                                  // slot 0x18
	void runButton(Object &obj, Object *target, const Coord3D *loc); // RW 0x696FD2 / 0x697890 / 0x6979D9 with source 1
	bool decideKiller(Object &obj);                             // RW 0x9EC0A7
	bool decideSelfBuff(Object &obj);                           // RW 0x9E9488
	bool decideSelfDebuff(Object &obj);                         // RW 0x9EAF26
	bool decideToggleMounted(Object &obj);                      // RW 0x9EBE42

	const AISpecialPowerUpdateModuleData *m_data;
	bool m_active = false;                                      // + 0x20
	bool m_initialized = false;                                 // + 0x21
	std::unique_ptr<AISpecialPowerDecision> m_decision;         // + 0x24
};
