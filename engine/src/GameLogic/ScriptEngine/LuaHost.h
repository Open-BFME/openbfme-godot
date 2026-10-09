// OpenBFME. GPL-3.0.
//
// The interfaces between the Lua script engine (LuaScriptEngine) and the rest of the engine, and the report channel of its acceptance stops.
//
// WHY: the retail LuaScriptEngine (RW 0x733F27-0x73A400, ZH has no Lua) reaches the object table, the AI, the players, upgrades, special powers,
// weapons, the partition manager, audio and the map script engine through game singletons. Those systems belong to other lanes and mostly
// do not exist yet, so the 41 logic bindings call them through the narrow interface LuaGameHost below. Its default for every callee that
// is not implemented is a REPORTED acceptance stop (S-124, PLAN "Acceptance stops"): the call is recorded in LuaReportSink with the callee's
// retail name and address, never silently skipped. A real engine overrides the callee; a test overrides it to observe the call.
//
// Every method cites the retail function it stands for (RW = RotWK game.dat, caveat S-001; spec = workspace/rebuild/specs/lua-scripting.md).

#pragma once

#include "Common/NameKeyGenerator.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct LuaEventList;

// ---------------------------------------------------------------------------------------------------------------------
// Acceptance stop reports
// ---------------------------------------------------------------------------------------------------------------------
// Each distinct (stop, text) is kept once with a count, in first-seen order. Tests assert on it; the engine's users print it.
class LuaReportSink
{
public:
	struct Entry
	{
		std::string stop;
		std::string text;
		size_t count = 0;
	};
	void report(const std::string &stop, const std::string &text);
	const std::vector<Entry> &entries() const { return m_entries; }
	size_t count(const std::string &stop) const; // total occurrences of the stop
	bool has(const std::string &stop) const { return count(stop) != 0; }
	bool has(const std::string &stop, const std::string &textPart) const;
	void clear() { m_entries.clear(); }

private:
	std::vector<Entry> m_entries;
};

// ---------------------------------------------------------------------------------------------------------------------
// Objects as the dispatcher sees them
// ---------------------------------------------------------------------------------------------------------------------
// What dispatch (RW 0x735F53) and register (RW 0x735941) read from an Object. "+0x..." are RW offsets.
struct LuaObjectInfo
{
	int id = 0;                          ///< Object+0x74
	bool hasAI = false;                  ///< Object+0x260 != 0
	bool forceLuaRegistration = false;   ///< ThingTemplate+0x640 (INI field ForceLuaRegistration)
	bool dead = false;                   ///< Object+0x458 & 1 (a dead object only receives OnDestroyed)
	const LuaEventList *luaEvents = nullptr; ///< AIUpdateInterface+0x220: the EventList of AILuaEventsList (null: no handlers)
};

// One of the three argument records of a dispatch (RW DelayedLuaEventList, 0x4C bytes; spec 4.3)
struct LuaEventArg
{
	enum Kind
	{
		NONE = 0,   ///< ends the argument list
		REAL = 1,   ///< lua_pushnumber((double)float)
		BOOLEAN = 2,
		OBJECT = 3, ///< pushObject(findObjectByID(id)): the table of the registered object, nil (and a second nil, quirk Q5) otherwise
		STRING = 4
	};
	Kind kind = NONE;
	float real = 0.0f;
	bool boolean = false;
	int objectId = 0;
	std::string text;

	static LuaEventArg makeReal(float v) { LuaEventArg a; a.kind = REAL; a.real = v; return a; }
	static LuaEventArg makeBool(bool v) { LuaEventArg a; a.kind = BOOLEAN; a.boolean = v; return a; }
	static LuaEventArg makeObject(int id) { LuaEventArg a; a.kind = OBJECT; a.objectId = id; return a; }
	static LuaEventArg makeString(const std::string &s) { LuaEventArg a; a.kind = STRING; a.text = s; return a; }
};
struct LuaEventArgs
{
	LuaEventArg arg[3];
};

// Partition manager query order (RW 0xA39340 parameter; B1 ITER_SORTED_NEAR_TO_FAR / ITER_FASTEST) and relationship flags
// (PartitionFilterRelationship in this binary: ENEMIES 1, NEUTRAL 2, ALLIES 4; LuaEventNugget 0x911EDC)
enum LuaRangeOrder
{
	LUA_RANGE_FASTEST = 0,
	LUA_RANGE_NEAR_TO_FAR = 1
};
enum LuaRelationship
{
	LUA_REL_ENEMIES = 1,
	LUA_REL_NEUTRAL = 2,
	LUA_REL_ALLIES = 4
};

// ---------------------------------------------------------------------------------------------------------------------
// The logic state's view of the engine
// ---------------------------------------------------------------------------------------------------------------------
class LuaGameHost
{
public:
	virtual ~LuaGameHost() = default;
	void attach(LuaReportSink *sink) { m_sink = sink; }

	// TheGameLogic->findObjectByID (RW 0x449681 through global 0xDE412C). false: no such object (id 0 included).
	virtual bool findObject(int id, LuaObjectInfo *out) = 0;

	// TheGameLogic frame, `*(0xDE412C + 0x40)` (GetFrame, RW 0x73461B)
	virtual std::uint32_t logicFrame();
	// TheAudio != 0 (RW global 0xDE42FC): ObjectPlaySound, ObjectDoSpecialPower and ObjectCreateAndFireTempWeapon return 0 without it
	virtual bool audioAvailable() { return true; }

	// ---- read-only queries (spec 3.2 #5, #13-#17) ----
	struct ObjectDescription
	{
		std::string objectName; ///< Object+0x88 (the map object's name), "" when it has none
		std::string templateName; ///< Object+4 -> template+0x64
		int playerIndex = 0;     ///< controlling player +0x54
		std::wstring playerName; ///< controlling player +0x38 (display name); L"<unknown>" when the player is missing
	};
	// RW 0x69005F: the pieces of "Object %d [%s, owned by player %d (%ls)]"
	virtual bool objectDescription(int id, ObjectDescription *out);
	// RW 0x736770: Object+0x31C (team) -> prototype (+0x30) -> name (+0x14); "" when there is no prototype
	virtual std::string objectTeamName(int id);
	// RW 0x7367E3: the string at +0x58 of the record RW 0x79FD6F returns for the object's team (INFERRED: the owner's side name)
	virtual std::string objectPlayerSide(int id);
	// RW 0x73684D: Object+0x80 is the capturer's id; that object's controlling player's +0x58 string. false: no capturer object (nil)
	virtual bool objectCapturingPlayerSide(int id, std::string *out);
	// RW 0x7368CA: Object+4 -> template -> +0x64 name, "" when the template has no name
	virtual std::string objectTemplateName(int id);
	// RW 0x73692C: bit `bit` of the ModelCondition flags at Object+0x10C (the bit number is resolved by the engine: RW 0x4B3B5B)
	virtual bool objectModelConditionBit(int id, int bit);
	// RW 0x68C818(object, 4, &bonus, 0): the object's attribute modifier of type 4 (the fear resistance bonus), if it has one
	virtual bool objectAttributeModifier(int id, int type, float *bonus);
	// RW 0x662D70(Object+0x260): the AI's kind id, -1 when the object has no AI (RW compares it with 0x2D)
	virtual int objectAIKind(int id);
	// RW 0xA39340: ids of the objects within `radius` of the object's position, in the order the partition manager yields them.
	// `relationship` is a LuaRelationship mask; the object itself and non-root things are excluded (spec 3.2 #8-#11, #19).
	// `notOfPlayer`: the filter of ObjectBroadcastEventToUnits instead of a relationship (RW 0x660D2C(self, 0)).
	virtual std::vector<int> objectsInRange(int id, float radius, LuaRangeOrder order, int relationship, bool notOfPlayer);
	// the horde interface of the object's contain module (Object+0x258 -> vtable +0x7C): its members in list order. false: not a horde
	virtual bool hordeMembers(int id, std::vector<int> *out);

	// ---- actions (spec 3.2 #20-#39) ----
	// RW 0x68F37F(object, type, other, 1): EmotionTypes 6 run away panic, 4 cower, 5 uncontrollable cower, 9 alert (spec G10); other 0: none
	virtual void enterEmotion(int id, int type, int otherId);
	// RW 0x73841C + Object.ai(+0x260)+0x3C4: AI command 0x2F with `otherId`, then AI flag = `flag`. Only for an object with an AI
	virtual void enterFearState(int id, int otherId, bool flag);
	// RW 0x738A24: delayed-death module toggle, AI helpers 0x663082 / 0x66309C and AI command 0x3A
	virtual void enterRampageState(int id);
	// RW 0x73529F-style sound: AudioEventRTS(name, object id) added through TheAudio. false: no such event. *handle: the audio handle
	virtual bool playObjectSound(int id, const std::string &eventName, std::uint32_t *handle);
	virtual void setChanting(int id, bool on);                          // RW 0x691784
	virtual float fearFactor(int id);                                    // Object+0x1AC
	virtual void setFearFactor(int id, float value);
	virtual void setEnragedState(int id, bool on);                       // RW 0x68F2F1
	virtual bool doSpecialPower(int id, const std::string &powerName);   // RW 0x69C146 + 0x691526; false: no such power or module
	virtual bool createAndFireTempWeapon(int id, const std::string &weaponName); // RW 0x6CC5DF + 0x6CF530; false: no such weapon
	struct UpgradeLookup
	{
		bool found = false;      ///< TheUpgradeCenter->findUpgrade (RW 0x66F5E5)
		bool playerType = false; ///< upgrade->type == 0 (a player upgrade); otherwise an object upgrade
	};
	virtual UpgradeLookup findUpgrade(const std::string &name);
	virtual bool playerHasUpgradeComplete(int id, const std::string &name); // controlling player, RW 0x6AC2AF
	// RW 0x736DF5. grant: player upgrades Player::addUpgrade (RW 0x6AEE22), object upgrades Object::giveUpgrade (RW 0x69388B);
	// remove: Player::removeUpgrade (RW 0x6AE60C), Object::removeUpgrade (RW 0x691438). (The spec's table has the two player
	// addresses swapped; the registered binding passes flag 1 for ObjectGrantUpgrade, which takes the 0x6AEE22 / 0x69388B branch.)
	virtual void grantUpgrade(int id, const std::string &name, bool playerType);
	virtual void removeUpgrade(int id, const std::string &name, bool playerType);
	virtual void setDelayedDeath(int id, bool on);                       // module at Object+0x25C vtable +0x98
	// drawable of the object: RW 0x6789B4 (showModule: the draw module whose tag is `name`; true if one exists) and 0x672823 (showSubObject)
	virtual bool drawableShowModule(int id, const std::string &name, bool visible, bool permanent);
	virtual void drawableShowSubObject(int id, const std::string &name, bool visible, bool permanent);
	virtual void setGeometryActive(int id, const std::string &name, bool active); // RW 0xAD3520
	virtual void changeAllegianceFromNonPlayablePlayer(int selfId, int otherId);   // RW 0x7365BE
	virtual void forbidPlayerCommands(int id, bool forbid);              // AI+0x3C5

	// ---- the map script engine (spec 3.2 #3, #4; spec G12) ----
	// the condition / action templates of data\scripts\scripts.ini by InternalName: the parameter count, -1 when there is no such template
	virtual int scriptTemplateParamCount(bool action, const std::string &name);
	struct ScriptArg
	{
		enum Kind { NIL, NUMBER, STRING, BOOLEAN, OBJECT } kind = NIL;
		float real = 0.0f;      ///< NUMBER: (float)lua_tonumber (RW 0x735C6A `fst dword [param+0xC]`)
		int integer = 0;        ///< NUMBER: _ftol(lua_tonumber) (RW 0x735C74; BOOLEAN: 0/1; NIL: 0)
		std::string text;       ///< STRING
		int objectId = 0;       ///< OBJECT (ExecuteAction only: param +0x24)
	};
	virtual bool evaluateCondition(const std::string &name, const std::vector<ScriptArg> &args);
	virtual void executeAction(const std::string &name, const std::vector<ScriptArg> &args);

protected:
	// The stop S-124 for a callee that is not implemented: `callee` is its retail name and address, e.g. "Player::addUpgrade (RW 0x6AEE22)".
	void unimplemented(const char *callee) const;
	LuaReportSink *m_sink = nullptr;
};

// ---------------------------------------------------------------------------------------------------------------------
// The drawable state's view of the drawable (spec 3.3; the CurDrawable* functions)
// ---------------------------------------------------------------------------------------------------------------------
// The script context of a running BeginScript (RW LuaScriptEngine+0x9C): the previous state and animation, the request for a transition
// state and the "allow to continue" flag are written by the script, everything else is read. Methods that return a pointer-like "absent"
// (nil in Lua) say so. A context that lacks a capability reports a stop through LuaScriptEngine (see the W3D adapter).
class LuaDrawableContext
{
public:
	virtual ~LuaDrawableContext() = default;
	// ctx+0 / ctx+8: AsciiStrings; "" is pushed as nil
	virtual std::string previousAnimationState() = 0;
	virtual std::string previousAnimation() = 0;
	virtual float previousAnimFraction() = 0;                    // ctx+0x10
	virtual void setTransitionAnimState(const std::string &name) = 0; // ctx+4 = name
	virtual void allowToContinue() = 0;                          // ctx+0x14 = 1
	// drawable+0x258 model condition bits and object+0x94 status bits of the drawable's object
	virtual bool modelCondition(const std::string &name) = 0;
	virtual bool objectStatus(const std::string &name) = 0;
	// does a draw module of the drawable have this tag (the lookup of RW 0x6789B4); the module-first rule of CurDrawableShowSubObject /
	// CurDrawableHideSubObject asks it before treating the name as a sub object
	virtual bool hasModule(const std::string &name) = 0;
	// drawable->showModule (RW 0x6789B4) / showSubObject (RW 0x672823)
	virtual bool showModule(const std::string &name, bool visible, bool permanent) = 0; // true: a draw module with that tag exists
	virtual void showSubObject(const std::string &name, bool visible, bool permanent) = 0;
	virtual void playSound(const std::string &name) = 0;
	// the object's current target (object+0x3A8 position, +0x3B0 z, +0x3B4 id): false = no drawable / object (nil in Lua)
	virtual bool targetDistance(double *out) = 0;
	virtual bool targetHeight(double *out) = 0;
	virtual bool targetBearing(double *out) = 0;
	// RW 0x73667D: 0 = the chain context -> drawable -> object is missing (nil then false), 1 = no target / not of that kind (false), 2 = of that kind
	virtual int targetKindOf(const std::string &kind) = 0;
	// GetGameClientRandomValueReal(lo, hi) (RW 0x6D33AB): the client random stream
	virtual float clientRandomReal(float lo, float hi) = 0;
};
