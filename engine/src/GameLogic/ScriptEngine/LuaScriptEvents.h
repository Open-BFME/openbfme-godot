// OpenBFME. GPL-3.0.
//
// The event model of the Lua script engine and the reader of ScriptEvents.xml (spec lua-scripting.md 1.2, 1.3, 4.1, 4.4). Port of the
// parse half of the retail LuaScriptEngine (RotWK game.dat, caveat S-001):
//   file reader        RW 0x739B3A (parseXml: open with flags 0x41, whole file + 10 bytes, NUL terminated, lexer at 0x949147)
//   root               RW 0x739ABD (strcmp with "SageLuaScriptSection"; children "Events" -> 0x738AED, "EventList" -> 0x7397A3)
//   Events children    RW 0x738AED: InternalEvent 0x73459A, ScriptedEvent 0x7384DD, ModelConditionEvent 0x738556, ObjectStatusEvent 0x7387A9
//   EventList          RW 0x7397A3 (EventHandler children; Inherit; DebugSingleStep)
// Donor facts: Open-BFME-1 LuaScriptEngineParseToken*.cpp / ParseModelConditionEvent.cpp (same shape, 304-bit sets).
//
// The control flow is the retail one, not a summary of it: every `finish()` call (EaXmlLexer::next) is where retail makes it, so an unknown
// element, a duplicate ModelConditionEvent or an EventHandler with children stops or skips exactly what retail stops or skips (the unit
// tests pin the retail data's two parse bugs, EvilPorterFunctions and FramFunctions, and the abort rules).

#pragma once

#include "Common/ModelState.h"
#include "Common/NameKeyGenerator.h"
#include "GameLogic/ScriptEngine/LuaHost.h"
#include "GameLogic/ScriptEngine/LuaXml.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// The 17 built-in events in the engine's slot order (RW 0x73449A: slot i at LuaScriptEngine+0x14+8*i)
enum LuaInternalEvent
{
	LUAEVENT_OnDamaged = 0,
	LUAEVENT_OnDestroyed = 1,
	LUAEVENT_OnArrived = 2,
	LUAEVENT_OnUnitEntered = 3,
	LUAEVENT_OnTeamEntered = 4,
	LUAEVENT_OnUnitExited = 5,
	LUAEVENT_OnTeamExited = 6,
	LUAEVENT_OnTeamDestroyed = 7,
	LUAEVENT_BeScary = 8,
	LUAEVENT_DamageIncoming = 9,
	LUAEVENT_OnAflame = 10,
	LUAEVENT_OnQuenched = 11,
	LUAEVENT_OnCreated = 12,
	LUAEVENT_OnBuildingComplete = 13,
	LUAEVENT_OnSlaughtered = 14,
	LUAEVENT_OnGenericEvent = 15,
	LUAEVENT_OnBuildVariation = 16,
	LUAEVENT_COUNT = 17
};
extern const char *const kLuaInternalEventNames[LUAEVENT_COUNT];

struct LuaEventHandler
{
	NameKeyType key = 0;          ///< NameKey(EventName)
	std::string function;         ///< ScriptFunctionName
	bool debugSingleStep = false; ///< DebugSingleStep="true"
};

// One <EventList>: RW 20 byte record {name AsciiString, handler vector}
struct LuaEventList
{
	std::string name;
	std::vector<LuaEventHandler> handlers; ///< unique by key, in insertion order (RW 0x733FAF replaces an equal key, else appends)
	const LuaEventHandler *find(NameKeyType key) const; ///< RW 0x73411F (sorted binary search; keys are unique)
};

// RW 0x24-byte ObjectStatusEvent record {key, required, excluded} over the 128 bit ObjectStatusMask (names RW 0xD8AFF0): same rule
// (RW 0x733126). Retail data has none; the registry parses them so a mod's are kept, and reports that nothing fires them yet.
struct LuaObjectStatusEvent
{
	NameKeyType key = 0;
	std::uint32_t required[4] = { 0, 0, 0, 0 };
	std::uint32_t excluded[4] = { 0, 0, 0, 0 };
	bool matches(const std::uint32_t status[4]) const;
};

// RW 0x9C-byte ModelConditionEvent record {key, required set, excluded set}: the event fires when the object's model condition flags contain
// every `required` bit and none of the `excluded` bits (RW 0x7330DD: `!intersects(excluded, flags) && ((flags & required) == required)`)
struct LuaModelConditionEvent
{
	NameKeyType key = 0;
	ModelConditionFlags required;
	ModelConditionFlags excluded;
	bool matches(const ModelConditionFlags &flags) const;
};

// What FindEvent (RW 0x735388) returns: the key an event dispatches under and where the name was found
struct LuaEventRef
{
	enum Kind { NONE, INTERNAL, SCRIPTED, MODEL_CONDITION, OBJECT_STATUS } kind = NONE;
	NameKeyType key = 0;
	int index = -1; ///< internal slot / record index
	bool valid() const { return kind != NONE; }
};

class LuaEventRegistry
{
public:
	explicit LuaEventRegistry(NameKeyGenerator &keys);

	// RW 0x73449A (initEventKeys): the 17 slot keys. Done by the constructor; the keys live as long as the NameKeyGenerator's.
	NameKeyType internalKey(int slot) const { return m_internalKey[slot]; }
	bool internalEnabled(int slot) const { return m_internalEnabled[slot]; }
	NameKeyType onDestroyedKey() const { return m_internalKey[LUAEVENT_OnDestroyed]; } ///< LuaScriptEngine+0xDC

	// RW 0x739585 (reset), the part that is the registry's: the scripted-event, event-list, model-condition and object-status vectors
	void reset();

	// RW 0x739B3A parseXml(file text, keepOpen). `globalType(name)` is lua_type of the logic state's global (RW 0x739983: the handler's
	// function must be a function). Nothing a handler names is rejected: retail builds an error text and drops it; the port keeps it in notes().
	void parse(const std::string &xml, bool keepOpen, const std::function<int(const std::string &)> &globalType, LuaReportSink &sink);

	// RW 0x7396E8: exact name (strcmp); null when there is none. With duplicate names (not produced by retail data) the first is returned
	// (the retail sort and binary search are not decoded: stop S-127).
	const LuaEventList *findEventList(const std::string &name) const;
	size_t eventListCount() const { return m_lists.size(); }
	const LuaEventList &eventList(size_t i) const { return *m_lists[i]; }

	// RW 0x735388: internal slots, then declared scripted events, then model condition records, then object status records
	LuaEventRef findEvent(NameKeyType key) const;
	LuaEventRef findEvent(const std::string &name); // creates the NameKey like retail (RW 0x5487EC)

	const std::vector<NameKeyType> &scriptedEvents() const { return m_scripted; }
	const std::vector<LuaModelConditionEvent> &modelConditionEvents() const { return m_modelConditions; }
	const std::vector<LuaObjectStatusEvent> &objectStatusEvents() const { return m_objectStatuses; }

	// what the reader found that retail accepts silently or that it cannot represent (one line each, in order)
	const std::vector<std::string> &notes() const { return m_notes; }
	void clearNotes() { m_notes.clear(); }

private:
	typedef std::function<int(const std::string &)> GlobalType;
	void parseToken(EaXmlLexer &x);
	void parseEvents(EaXmlLexer &x);
	void parseEventList(EaXmlLexer &x);
	void parseInternalEvent(EaXmlLexer &x);
	void parseScriptedEvent(EaXmlLexer &x);
	void parseModelConditionEvent(EaXmlLexer &x);
	void parseObjectStatusEvent(EaXmlLexer &x);
	void note(const std::string &text) { m_notes.push_back(text); }
	LuaEventList *findList(const std::string &name);

	NameKeyGenerator &m_keys;
	NameKeyType m_internalKey[LUAEVENT_COUNT];
	bool m_internalEnabled[LUAEVENT_COUNT];
	std::vector<NameKeyType> m_scripted;
	std::vector<LuaModelConditionEvent> m_modelConditions;
	std::vector<LuaObjectStatusEvent> m_objectStatuses;
	std::vector<std::unique_ptr<LuaEventList>> m_lists; ///< stable addresses: objects hold pointers (AI+0x220)
	std::vector<std::string> m_notes;
	bool m_keepOpen = false;   ///< LuaScriptEngine+0xD8 (the map overlay load flag; 0x739B3A's keepOpen)
	GlobalType m_globalType;
	LuaReportSink *m_sink = nullptr;
};
