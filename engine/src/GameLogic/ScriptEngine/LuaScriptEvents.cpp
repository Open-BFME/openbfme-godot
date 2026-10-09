// OpenBFME. GPL-3.0. See LuaScriptEvents.h.

#include "GameLogic/ScriptEngine/LuaScriptEvents.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"

#include <algorithm>
#include <cstring>

extern const char *const TheObjectStatusNames[]; // RW 0xD8AFF0, 106 names (GameLogic/BitFlagNames.cpp, lane HORDE-1)

// RW 0x73449A (initEventKeys): slot i is the NameKey of kLuaInternalEventNames[i]
const char *const kLuaInternalEventNames[LUAEVENT_COUNT] = {
	"OnDamaged", "OnDestroyed", "OnArrived", "OnUnitEntered", "OnTeamEntered", "OnUnitExited", "OnTeamExited", "OnTeamDestroyed", "BeScary",
	"DamageIncoming", "OnAflame", "OnQuenched", "OnCreated", "OnBuildingComplete", "OnSlaughtered", "OnGenericEvent", "OnBuildVariation"
};

const LuaEventHandler *LuaEventList::find(NameKeyType key) const
{
	for (const LuaEventHandler &h : handlers)
	{
		if (h.key == key)
		{
			return &h;
		}
	}
	return nullptr;
}

bool LuaModelConditionEvent::matches(const ModelConditionFlags &flags) const
{
	// RW 0x7330DD: excluded.intersects(flags) -> false (RW 0x6632E9), else (flags & required) == required (RW 0x4B37A3, 0x444D9B)
	if (excluded.anyIntersectionWith(flags))
	{
		return false;
	}
	return flags.testForAll(required);
}

bool LuaObjectStatusEvent::matches(const std::uint32_t status[4]) const
{
	for (int i = 0; i < 4; ++i)
	{
		if (excluded[i] & status[i])
		{
			return false;
		}
	}
	for (int i = 0; i < 4; ++i)
	{
		if ((status[i] & required[i]) != required[i])
		{
			return false;
		}
	}
	return true;
}

namespace
{
// RW 0x4B5E05 (model conditions) / 0x73543E (object status), one token. Returns false when the list ends (NONE).
bool applyBitToken(const char *token, const char *const *names, std::uint32_t *words, size_t wordCount, bool &sawNormal, bool &sawSign)
{
	auto setBit = [&](int bit, bool on) {
		if (bit < 0)
		{
			return;
		}
		if (on)
		{
			words[bit >> 5] |= 1u << (bit & 31);
		}
		else
		{
			words[bit >> 5] &= ~(1u << (bit & 31));
		}
	};
	if (AsciiStringUtil::compareNoCase(token, "NONE") == 0)
	{
		if (sawNormal || sawSign)
		{
			throw INIException(2, "you may not mix normal and +- ops in bitstring lists");
		}
		std::memset(words, 0, wordCount * 4);
		return false;
	}
	if (token[0] == '+' || token[0] == '-')
	{
		if (sawNormal)
		{
			throw INIException(2, "you may not mix normal and +- ops in bitstring lists");
		}
		setBit(INI::scanIndexList(token + 1, names), token[0] == '+');
		sawSign = true;
		return true;
	}
	if (sawSign)
	{
		throw INIException(2, "you may not mix normal and +- ops in bitstring lists");
	}
	if (!sawNormal)
	{
		std::memset(words, 0, wordCount * 4);
	}
	setBit(INI::scanIndexList(token, names), true);
	sawNormal = true;
	return true;
}

// RW 0x7353BD: AsciiString::nextToken with the default separators, each token to the handler until it says stop
void parseConditionText(const std::string &text, const char *const *names, std::uint32_t *words, size_t wordCount)
{
	bool sawNormal = false, sawSign = false;
	std::string rest = text, token;
	while (AsciiStringUtil::nextToken(rest, &token, " \n\r\t"))
	{
		if (!applyBitToken(token.c_str(), names, words, wordCount, sawNormal, sawSign))
		{
			break;
		}
	}
}

// the unused high bits of the last word of a BitFlags<N>: retail flips whole words, BitFlags keeps them zero (no observable difference:
// the flags a record is tested against never have them)
void maskTail(std::uint32_t *words, size_t wordCount, int usedBits)
{
	const int tail = usedBits - (int)(wordCount - 1) * 32;
	if (tail < 32)
	{
		words[wordCount - 1] &= (1u << tail) - 1u;
	}
}
} // namespace

LuaEventRegistry::LuaEventRegistry(NameKeyGenerator &keys)
	: m_keys(keys)
{
	for (int i = 0; i < LUAEVENT_COUNT; ++i)
	{
		m_internalKey[i] = keys.nameToKey(kLuaInternalEventNames[i]);
		m_internalEnabled[i] = false;
	}
}

void LuaEventRegistry::reset()
{
	// RW 0x739585: the vectors at +0xA0, +0xB0, +0xBC, +0xC8 and the sort flag +0xAC; the internal slots are not touched
	m_scripted.clear();
	m_modelConditions.clear();
	m_objectStatuses.clear();
	m_lists.clear();
}

void LuaEventRegistry::parse(const std::string &xml, bool keepOpen, const GlobalType &globalType, LuaReportSink &sink)
{
	m_globalType = globalType;
	m_sink = &sink;
	EaXmlLexer x(xml);
	if (x.crashesRetail())
	{
		sink.report("S-127", "ScriptEvents.xml starts with \"<?\" but not with exactly <?xml version=\"1.0\"?>: retail's reader faults on such a file (RW "
			"0x9491AF); nothing of it is read");
		return;
	}
	m_keepOpen = keepOpen; // RW 0x739B3A: LuaScriptEngine+0xD8
	for (;;)
	{
		int status = x.next();
		if (status == 0)
		{
			break;
		}
		if (--status != 0)
		{
			return; // retail returns here without clearing the flag
		}
		parseToken(x);
	}
	m_keepOpen = false;
}

void LuaEventRegistry::parseToken(EaXmlLexer &x)
{
	// RW 0x739ABD
	if (x.tail() != "SageLuaScriptSection")
	{
		return;
	}
	int status = x.next();
	while (status != 0)
	{
		if (--status != 0)
		{
			return;
		}
		const std::string tag = x.tail();
		if (tag == "Events")
		{
			parseEvents(x);
		}
		else
		{
			if (tag != "EventList")
			{
				return;
			}
			parseEventList(x);
		}
		status = x.next();
	}
}

void LuaEventRegistry::parseEvents(EaXmlLexer &x)
{
	// RW 0x738AED
	if (x.tail() != "Events")
	{
		return;
	}
	int status = x.next();
	while (status != 0)
	{
		if (--status != 0)
		{
			return;
		}
		const std::string tag = x.tail();
		if (tag == "InternalEvent")
		{
			parseInternalEvent(x);
		}
		else if (tag == "ScriptedEvent")
		{
			parseScriptedEvent(x);
		}
		else if (tag == "ModelConditionEvent")
		{
			parseModelConditionEvent(x);
		}
		else
		{
			if (tag != "ObjectStatusEvent")
			{
				return;
			}
			parseObjectStatusEvent(x);
		}
		status = x.next();
	}
}

void LuaEventRegistry::parseInternalEvent(EaXmlLexer &x)
{
	// RW 0x73459A
	for (int i = 0; i < x.attributeCount(); ++i)
	{
		if (x.attributeName(i) == "Name")
		{
			const NameKeyType key = m_keys.nameToKey(x.attributeValue(i));
			for (int slot = 0; slot < LUAEVENT_COUNT; ++slot)
			{
				if (m_internalKey[slot] == key)
				{
					m_internalEnabled[slot] = true;
					break;
				}
			}
		}
	}
	x.next();
}

void LuaEventRegistry::parseScriptedEvent(EaXmlLexer &x)
{
	// RW 0x7384DD: no duplicate check
	for (int i = 0; i < x.attributeCount(); ++i)
	{
		if (x.attributeName(i) == "Name")
		{
			m_scripted.push_back(m_keys.nameToKey(x.attributeValue(i)));
		}
	}
	x.next();
}

void LuaEventRegistry::parseModelConditionEvent(EaXmlLexer &x)
{
	// RW 0x738556
	std::string name;
	for (int i = 0; i < x.attributeCount(); ++i)
	{
		if (x.attributeName(i) == "Name")
		{
			name = x.attributeValue(i);
		}
	}
	if (x.next() != 1)
	{
		return;
	}
	if (x.tail() != "Conditions")
	{
		return;
	}
	if (x.next() != 3)
	{
		return;
	}
	const std::string text = x.tail();
	try
	{
		LuaModelConditionEvent record;
		ModelConditionFlags excluded;
		std::uint32_t *ew = excluded.words();
		for (int w = 0; w < ModelConditionFlags::NUM_WORDS; ++w)
		{
			ew[w] = ~ew[w]; // RW 0x738638: not on 19 words
		}
		parseConditionText(text, ModelCondition::bitNames(), record.required.words(), ModelConditionFlags::NUM_WORDS);
		parseConditionText(text, ModelCondition::bitNames(), ew, ModelConditionFlags::NUM_WORDS);
		for (int w = 0; w < ModelConditionFlags::NUM_WORDS; ++w)
		{
			ew[w] = ~ew[w]; // RW 0x738677
		}
		maskTail(ew, ModelConditionFlags::NUM_WORDS, MODELCONDITION_COUNT);
		record.excluded = excluded;
		record.key = m_keys.nameToKey(name);
		for (const LuaModelConditionEvent &it : m_modelConditions)
		{
			if (it.key == record.key && it.required == record.required && it.excluded == record.excluded)
			{
				note("ModelConditionEvent " + name + ": an identical record exists; retail stops reading here, without consuming the rest of the element (RW 0x738709)");
				return;
			}
		}
		m_modelConditions.push_back(record);
	}
	catch (const INIException &e)
	{
		// RW 0x738717: logged to the debug window when it is enabled ("Error during parsing XML data: "), then the element ends normally
		note("ModelConditionEvent " + name + ": Error during parsing XML data: " + e.message());
		if (m_sink)
		{
			m_sink->report("S-127", "ModelConditionEvent " + name + " has Conditions the reader rejects: " + e.message());
		}
	}
	if (x.next() == 2)
	{
		x.next();
	}
}

void LuaEventRegistry::parseObjectStatusEvent(EaXmlLexer &x)
{
	// RW 0x7387A9: the same shape as ModelConditionEvent over 128 bit sets
	std::string name;
	for (int i = 0; i < x.attributeCount(); ++i)
	{
		if (x.attributeName(i) == "Name")
		{
			name = x.attributeValue(i);
		}
	}
	if (x.next() != 1)
	{
		return;
	}
	if (x.tail() != "Conditions")
	{
		return;
	}
	if (x.next() != 3)
	{
		return;
	}
	const std::string text = x.tail();
	try
	{
		LuaObjectStatusEvent record;
		std::uint32_t excluded[4];
		for (int w = 0; w < 4; ++w)
		{
			excluded[w] = 0xFFFFFFFFu;
		}
		parseConditionText(text, TheObjectStatusNames, record.required, 4);
		parseConditionText(text, TheObjectStatusNames, excluded, 4);
		for (int w = 0; w < 4; ++w)
		{
			record.excluded[w] = ~excluded[w];
		}
		record.key = m_keys.nameToKey(name);
		for (const LuaObjectStatusEvent &it : m_objectStatuses)
		{
			if (it.key == record.key && std::memcmp(it.required, record.required, 16) == 0 && std::memcmp(it.excluded, record.excluded, 16) == 0)
			{
				note("ObjectStatusEvent " + name + ": an identical record exists; retail stops reading here (RW 0x738940)");
				return;
			}
		}
		m_objectStatuses.push_back(record);
		if (m_sink)
		{
			m_sink->report("S-129", "ObjectStatusEvent " + name + " is kept but no object status change fires it: retail's call site (RW 0x737954 against "
				"LuaScriptEngine's per-object snapshot) belongs to the AI lane");
		}
	}
	catch (const INIException &e)
	{
		note("ObjectStatusEvent " + name + ": Error during parsing XML data: " + e.message());
		if (m_sink)
		{
			m_sink->report("S-127", "ObjectStatusEvent " + name + " has Conditions the reader rejects: " + e.message());
		}
	}
	if (x.next() == 2)
	{
		x.next();
	}
}

LuaEventList *LuaEventRegistry::findList(const std::string &name)
{
	for (auto &l : m_lists)
	{
		if (l->name == name)
		{
			return l.get();
		}
	}
	return nullptr;
}

const LuaEventList *LuaEventRegistry::findEventList(const std::string &name) const
{
	for (const auto &l : m_lists)
	{
		if (l->name == name)
		{
			return l.get();
		}
	}
	return nullptr;
}

void LuaEventRegistry::parseEventList(EaXmlLexer &x)
{
	// RW 0x7397A3
	std::string name, inherit;
	for (int i = 0; i < x.attributeCount(); ++i)
	{
		if (x.attributeName(i) == "Name")
		{
			name = x.attributeValue(i);
		}
		else if (x.attributeName(i) == "Inherit")
		{
			inherit = x.attributeValue(i);
		}
	}
	LuaEventList list;
	list.name = name;
	int status = x.next();
	// RW 0x739848: findEventList(Inherit); a missing base is silently ignored; its handler vector is copied first (RW 0x733CE6).
	// An empty Inherit is looked up like any other name.
	if (const LuaEventList *base = findEventList(inherit))
	{
		list.handlers = base->handlers;
	}
	else if (!inherit.empty())
	{
		note("EventList " + name + ": Inherit=\"" + inherit + "\" names no EventList defined earlier in the file; retail skips it silently");
	}
	for (;;)
	{
		if (status == 0)
		{
			break; // end of the text: register
		}
		if (status != 1)
		{
			if (status != 2)
			{
				return; // RW 0x739A39: text or an error: abandon the list (not registered)
			}
			break; // </EventList>: register
		}
		if (x.tail() != "EventHandler")
		{
			note("EventList " + name + ": unknown element <" + x.tail() + ">: the list is abandoned and not registered (RW 0x739A7B)");
			return;
		}
		std::string eventName, function;
		bool debug = false;
		for (int i = 0; i < x.attributeCount(); ++i)
		{
			if (x.attributeName(i) == "EventName")
			{
				eventName = x.attributeValue(i);
			}
			else if (x.attributeName(i) == "ScriptFunctionName")
			{
				function = x.attributeValue(i);
			}
			else if (x.attributeName(i) == "DebugSingleStep")
			{
				debug = x.attributeValue(i) == "true";
			}
		}
		if (!eventName.empty() && !function.empty())
		{
			const int type = m_globalType ? m_globalType(function) : 1;
			if (type != 5)
			{
				// RW 0x739983: the text " is not defined." (the global is nil) or " is not a lua function." is built and dropped
				note("EventList " + name + ": handler function " + function + (type == 1 ? " is not defined." : " is not a lua function.") + " (retail drops the message)");
			}
			LuaEventHandler h;
			h.key = m_keys.nameToKey(eventName);
			h.function = function;
			h.debugSingleStep = debug;
			bool replaced = false;
			for (LuaEventHandler &old : list.handlers)
			{
				if (old.key == h.key)
				{
					old = h; // RW 0x733FAF
					replaced = true;
					break;
				}
			}
			if (!replaced)
			{
				list.handlers.push_back(h);
			}
		}
		x.next(); // </EventHandler>, or the synthetic end of "<EventHandler .../>"
		status = x.next();
	}
	// RW 0x739A3C: register. With the overlay flag a list of the same name is replaced in place (RW 0x738BE5), else appended (RW 0x7395E3)
	if (m_keepOpen)
	{
		if (LuaEventList *existing = findList(list.name))
		{
			*existing = list;
			return;
		}
	}
	else if (findEventList(list.name))
	{
		note("EventList " + list.name + " is defined twice; retail appends both and finds one by binary search (S-127: which is not decoded)");
		if (m_sink)
		{
			m_sink->report("S-127", "EventList " + list.name + " is defined more than once (in one file, or a second load without reset): retail appends every copy and finds one by a "
				"binary search whose choice among equal names is not decoded (RW 0x7396E8); the port finds the first");
		}
	}
	m_lists.push_back(std::unique_ptr<LuaEventList>(new LuaEventList(list)));
}

LuaEventRef LuaEventRegistry::findEvent(NameKeyType key) const
{
	LuaEventRef r;
	for (int i = 0; i < LUAEVENT_COUNT; ++i)
	{
		if (m_internalKey[i] == key)
		{
			r.kind = LuaEventRef::INTERNAL;
			r.key = key;
			r.index = i;
			return r;
		}
	}
	for (size_t i = 0; i < m_scripted.size(); ++i)
	{
		if (m_scripted[i] == key)
		{
			r.kind = LuaEventRef::SCRIPTED;
			r.key = key;
			r.index = (int)i;
			return r;
		}
	}
	for (size_t i = 0; i < m_modelConditions.size(); ++i)
	{
		if (m_modelConditions[i].key == key)
		{
			r.kind = LuaEventRef::MODEL_CONDITION;
			r.key = key;
			r.index = (int)i;
			return r;
		}
	}
	for (size_t i = 0; i < m_objectStatuses.size(); ++i)
	{
		if (m_objectStatuses[i].key == key)
		{
			r.kind = LuaEventRef::OBJECT_STATUS;
			r.key = key;
			r.index = (int)i;
			return r;
		}
	}
	return r;
}

LuaEventRef LuaEventRegistry::findEvent(const std::string &name)
{
	return findEvent(m_keys.nameToKey(name));
}
