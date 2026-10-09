// OpenBFME. GPL-3.0.
// ThingFactory and the object field adapters. See ThingFactory.h for the RotWK addresses.

#include "Common/Thing/ThingFactory.h"

#include "GameLogic/ThingCombatSets.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>

namespace
{
const char kDefaultThingTemplate[] = "DefaultThingTemplate"; // RW 0xC18698
}

// How a row of the object / audio table is parsed. The semantic strings come from
// tools/rw_object_model/object_fields.py (each decoded from the binary); "raw" rows keep their text.
enum ThingFactory::Semantic : int
{
	S_Raw,
	S_Bool,
	S_IndexList,
	S_ByteSizedIndexList,
	S_Byte,
	S_UnsignedByte,
	S_UnsignedShort,
	S_Int,
	S_UnsignedIntMax,
	S_Real,
	S_AngleReal,
	S_AsciiString,
	S_AsciiStringVector,
	S_PercentToReal,
	S_DurationUnsignedInt,
	S_Coord3D,
	S_ColorInt,
	S_VelocityReal,
	S_BitString16,
	S_Module,
	S_AddModule,
	S_InheritableModule,
	S_RemoveModule,
	S_ReplaceModule,
	S_ArmorSet,
	S_WeaponSet,
	S_UnitSpecificFX,
	S_UnitSpecificSounds,
	S_Prerequisites,
	S_LocomotorSet
};

struct ThingFactory::FieldBinding
{
	ThingFactory *factory = nullptr;
	size_t slot = 0;
	std::string name;
	unsigned fn = 0;
	unsigned userData = 0;
	std::vector<const char *> nameList; ///< NULL-terminated, for the index-list semantics
	std::vector<std::string> nameStorage;
	Semantic sem = S_Raw;
	RwFunction::Kind kind = RwFunction::Line;
};

ThingFactory::Semantic ThingFactory::semanticOf(const std::string &s)
{
	static const std::map<std::string, int> table = {
		{ "raw", 0 }, { "parseBool", 1 }, { "parseIndexList", 2 }, { "parseByteSizedIndexList", 3 }, { "parseByte", 4 }, { "parseUnsignedByte", 5 },
		{ "parseUnsignedShort", 6 }, { "parseInt", 7 }, { "parseUnsignedIntMax", 8 }, { "parseReal", 9 }, { "parseAngleReal", 10 }, { "parseAsciiString", 11 },
		{ "parseAsciiStringVector", 12 }, { "parsePercentToReal", 13 }, { "parseDurationUnsignedInt", 14 }, { "parseCoord3D", 15 }, { "parseColorInt", 16 },
		{ "parseVelocityReal", 17 }, { "parseBitString16", 18 },
		{ "module", 19 }, { "AddModule", 20 }, { "InheritableModule", 21 }, { "RemoveModule", 22 }, { "ReplaceModule", 23 }, { "ArmorSet", 24 }, { "WeaponSet", 25 },
		{ "UnitSpecificFX", 26 }, { "UnitSpecificSounds", 27 }, { "Prerequisites", 28 }, { "LocomotorSet", 29 },
	};
	auto it = table.find(s);
	if (it == table.end())
	{
		throw std::runtime_error("ThingFactory: unknown field semantic '" + s + "' in the field-table golden");
	}
	return (ThingFactory::Semantic)it->second;
}

ThingFactory::ThingFactory(NameKeyGenerator &keys, ModuleFactory &modules, const RwGrammar &grammar)
	: m_keys(keys)
	, m_modules(modules)
	, m_grammar(grammar)
{
	buildTables();
}

ThingFactory::~ThingFactory() = default;

void ThingFactory::buildTables()
{
	const RwBinaryData &data = m_grammar.data();
	std::set<std::string> seen;
	auto build = [&](const RwTable &t, std::vector<FieldParse> &rows) {
		for (const RwRow &r : t.rows)
		{
			auto b = std::make_unique<FieldBinding>();
			b->factory = this;
			b->slot = m_slotNames.size();
			b->name = r.name;
			b->fn = r.fn;
			b->userData = r.userData;
			const RwFunction &f = data.function(r.fn);
			b->kind = f.kind;
			b->sem = semanticOf(f.semantic.empty() ? "raw" : f.semantic);
			if (!r.names.empty())
			{
				b->nameStorage = r.names;
				for (const std::string &n : b->nameStorage)
				{
					b->nameList.push_back(n.c_str());
				}
				b->nameList.push_back(nullptr);
			}
			if (!seen.insert(r.name).second)
			{
				throw std::runtime_error("ThingFactory: field '" + r.name + "' is in both object tables");
			}
			m_slotIndex.emplace(r.name, m_slotNames.size());
			m_slotNames.push_back(r.name);
			FieldParse fp;
			fp.token = b->name.c_str();
			fp.parse = &ThingFactory::parseField;
			fp.userData = b.get();
			fp.offset = 0; // values live in the template's slot vector, not at an offset
			rows.push_back(fp);
			m_bindings.push_back(std::move(b));
		}
		rows.push_back(FieldParse{ nullptr, nullptr, nullptr, 0 });
	};
	const RwTable &obj = data.table(data.objectTable.table);
	const RwTable &aud = data.table(data.audioTable.table);
	if (obj.hasCatchAll || aud.hasCatchAll || !obj.terminated || !aud.terminated)
	{
		throw std::runtime_error("ThingFactory: the object tables are expected to be plain terminated tables");
	}
	build(obj, m_objectRows);
	build(aud, m_audioRows);
}

void ThingFactory::buildFieldParse(MultiIniFieldParse &p) const
{
	// RW 0x73C13C: object table extra 0, audio table extra 0x124
	p.add(m_objectRows.data(), m_grammar.data().objectTable.extra);
	p.add(m_audioRows.data(), m_grammar.data().audioTable.extra);
}

std::vector<std::string> ThingFactory::rawFieldRowNames() const
{
	std::vector<std::string> out;
	for (const auto &b : m_bindings)
	{
		if (b->sem == S_Raw || b->sem >= S_ArmorSet)
		{
			out.push_back(b->name);
		}
	}
	return out;
}

// ---------------------------------------------------------------------------------------------
// templates
// ---------------------------------------------------------------------------------------------
const ThingTemplate *ThingFactory::findTemplate(const std::string &name) const
{
	auto it = m_byName.find(name);
	return it == m_byName.end() ? nullptr : it->second;
}

const ThingTemplate *ThingFactory::findByTemplateID(unsigned short id) const
{
	if (id >= 1 && id <= m_list.size() && m_list[(size_t)id - 1]->getTemplateID() == id)
	{
		return m_list[(size_t)id - 1];
	}
	for (const ThingTemplate *t : m_list)
	{
		if (t->getTemplateID() == id)
		{
			return t;
		}
	}
	return nullptr;
}

ThingTemplate *ThingFactory::newTemplate(const std::string &name)
{
	auto owned = std::make_unique<ThingTemplate>();
	ThingTemplate *tt = owned.get();
	m_arena.push_back(std::move(owned));
	tt->m_fieldNames = &m_slotNames;
	tt->m_fieldIndex = &m_slotIndex;
	tt->m_fields.assign(m_slotNames.size(), FieldValue());

	// RW 0x6D27E3-0x6D2843: copy "DefaultThingTemplate" when it exists, then setCopiedFromDefault
	if (const ThingTemplate *def = findTemplate(kDefaultThingTemplate))
	{
		tt->copyFrom(def);
		tt->setCopiedFromDefault();
	}
	tt->friend_setTemplateID(m_nextTemplateID++); // RW 0x6D2848: the id is the value BEFORE the increment
	tt->friend_setTemplateName(name);
	m_byName[name] = tt;
	m_list.push_back(tt);
	return tt;
}

ThingTemplate *ThingFactory::newOverride(ThingTemplate *thingTemplate)
{
	// ZH ThingFactory.cpp:169-196; spec 4.8
	// RW 0x6D2733: new template, member-wise copy of the FINAL override (RW 0x6D1D80, which leaves the new
	// template's own override fields untouched), setCopiedFromDefault, markAsOverride, link behind the final one
	ThingTemplate *child = thingTemplate->friend_getFinalOverride();
	auto owned = std::make_unique<ThingTemplate>();
	ThingTemplate *tt = owned.get();
	m_arena.push_back(std::move(owned));
	tt->m_fieldNames = &m_slotNames;
	tt->m_fieldIndex = &m_slotIndex;
	tt->copyFrom(child);
	tt->m_nameString = child->m_nameString; // the override is a copy of the whole template, name and id included
	tt->m_templateID = child->m_templateID;
	tt->setCopiedFromDefault();
	tt->markAsOverride();
	child->setNextOverride(tt);
	return tt; // not in the master list
}

void ThingFactory::reset()
{
	// ZH ThingFactory.cpp:209-238: drop override chains and templates that exist only for the last map
	std::vector<ThingTemplate *> keep;
	for (ThingTemplate *t : m_list)
	{
		if (t->isOverride())
		{
			m_byName.erase(t->getName());
		}
		else
		{
			t->setNextOverride(nullptr);
			keep.push_back(t);
		}
	}
	m_list = std::move(keep);
	const std::set<const ThingTemplate *> alive(m_list.begin(), m_list.end());
	std::deque<std::unique_ptr<ThingTemplate>> arena;
	for (auto &p : m_arena)
	{
		if (alive.count(p.get()))
		{
			arena.push_back(std::move(p));
		}
	}
	m_arena = std::move(arena);
}

// ---------------------------------------------------------------------------------------------
// parseObjectDefinition (RW 0x6D2880)
// ---------------------------------------------------------------------------------------------
void ThingFactory::parseObjectDefinition(INI *ini, const std::string &name, const std::string &reskinFrom, const std::string &childOf)
{
	{
		DefinitionRecord rec;
		rec.name = name;
		rec.kind = !childOf.empty() ? "ChildObject" : !reskinFrom.empty() ? "ObjectReskin" : "Object";
		rec.file = ini->getFilename();
		rec.line = ini->currentSourceLine();
		rec.firstModuleData = m_modules.moduleDataCount();
		m_definitions.push_back(std::move(rec));
	}
	const size_t definitionIndex = m_definitions.size() - 1;
	ThingTemplate *tt = nullptr;
	if (contains(name))
	{
		tt = const_cast<ThingTemplate *>(findTemplate(name));
	}
	if (!tt)
	{
		tt = newTemplate(name);
		if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
		{
			tt->markAsOverride(); // created for this map only; removed on reset
		}
	}
	else if (ini->getLoadType() == INI_LOAD_RELOAD)
	{
		tt = newTemplate(name); // RW 0x6D28F2 (developer reload)
	}
	else if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		tt = newOverride(tt);
	}
	// load type 1: parse into the existing template

	MultiIniFieldParse p;
	buildFieldParse(p);

	if (!childOf.empty())
	{
		const ThingTemplate *parent = findTemplate(childOf);
		if (!parent)
		{
			throw INIException(3, "ChildObject must come after the original Object (%s, %s).", childOf.c_str(), name.c_str());
		}
		tt->copyFrom(parent);
		tt->setCopiedFromDefault();
		const INILoadType saved = ini->getLoadType();
		ini->setLoadType(INI_LOAD_CHILD_OBJECT); // RW 0x6D29D9: load type 4 for the field parse
		ini->initFromINIMulti(tt, p);
		ini->setLoadType(saved);
	}
	else if (!reskinFrom.empty())
	{
		const ThingTemplate *src = findTemplate(reskinFrom);
		if (!src)
		{
			throw INIException(3, "ObjectReskin must come after the original Object (%s, %s).", reskinFrom.c_str(), name.c_str());
		}
		tt->copyFrom(src);
		tt->setCopiedFromDefault();
		tt->setReskinnedFrom(src);
		ini->initFromINIMulti(tt, p); // the FULL table, unlike ZH's reskin table
	}
	else
	{
		ini->initFromINIMulti(tt, p);
	}
	// RW 0x6D2A7B validate (0x73CE7E): not ported, stop S-077
	m_definitions[definitionIndex].endModuleData = m_modules.moduleDataCount();
}

void ThingFactory::registerBlocks(INIBlockRegistry &registry)
{
	// B1 Source/Common/INI/INIObject.cpp: the three headers all end in parseObjectDefinition
	registry.registerBlock("Object", [this](INI *ini) {
		const std::string name = ini->getNextToken();
		parseObjectDefinition(ini, name, std::string(), std::string());
	});
	registry.registerBlock("ObjectReskin", [this](INI *ini) {
		const std::string name = ini->getNextToken();
		const std::string reskinFrom = ini->getNextToken();
		parseObjectDefinition(ini, name, reskinFrom, std::string());
	});
	registry.registerBlock("ChildObject", [this](INI *ini) {
		const std::string name = ini->getNextToken();
		const std::string childOf = ini->getNextToken();
		parseObjectDefinition(ini, name, std::string(), childOf);
	});
}

// ---------------------------------------------------------------------------------------------
// field adapters
// ---------------------------------------------------------------------------------------------
void ThingFactory::parseField(INI *ini, void *instance, void *, const void *userData)
{
	const FieldBinding *b = static_cast<const FieldBinding *>(userData);
	ThingFactory &factory = *b->factory;
	ThingTemplate *tt = static_cast<ThingTemplate *>(instance);
	if (tt->m_fields.size() != factory.m_slotNames.size())
	{
		tt->m_fields.resize(factory.m_slotNames.size());
		tt->m_fieldNames = &factory.m_slotNames;
		tt->m_fieldIndex = &factory.m_slotIndex;
	}
	FieldValue &slot = tt->m_fields[b->slot];

	switch (b->sem)
	{
		case S_Bool:
		{
			bool v = false;
			INI::parseBool(ini, tt, &v, nullptr);
			slot = v;
			return;
		}
		case S_IndexList:
		{
			int v = 0;
			INI::parseIndexList(ini, tt, &v, b->nameList.empty() ? nullptr : b->nameList.data());
			slot = (long long)v;
			return;
		}
		case S_ByteSizedIndexList:
		{
			std::uint8_t v = 0;
			INI::parseByteSizedIndexList(ini, tt, &v, b->nameList.empty() ? nullptr : b->nameList.data());
			slot = (long long)v;
			return;
		}
		case S_Byte:
		{
			std::int8_t v = 0;
			INI::parseByte(ini, tt, &v, nullptr);
			slot = (long long)v;
			return;
		}
		case S_UnsignedByte:
		{
			std::uint8_t v = 0;
			INI::parseUnsignedByte(ini, tt, &v, nullptr);
			slot = (long long)v;
			return;
		}
		case S_UnsignedShort:
		{
			std::uint16_t v = 0;
			INI::parseUnsignedShort(ini, tt, &v, nullptr);
			slot = (long long)v;
			return;
		}
		case S_Int:
		{
			int v = 0;
			INI::parseInt(ini, tt, &v, nullptr);
			slot = (long long)v;
			return;
		}
		case S_UnsignedIntMax:
		{
			// RW 0x42ECB2: scanUnsignedInt; a non-zero userData is the largest accepted value
			const unsigned v = ini->scanUnsignedInt(ini->getNextToken());
			if (b->userData != 0 && v > b->userData)
			{
				throw INIException(3, "value out of range, expected 0..%d", (int)b->userData); // RW 0xBD42E0
			}
			slot = (long long)v;
			return;
		}
		case S_Real:
		{
			float v = 0;
			INI::parseReal(ini, tt, &v, nullptr);
			slot = v;
			return;
		}
		case S_AngleReal:
		{
			float v = 0;
			INI::parseAngleReal(ini, tt, &v, nullptr);
			slot = v;
			return;
		}
		case S_AsciiString:
		{
			std::string v;
			INI::parseAsciiString(ini, tt, &v, nullptr);
			slot = std::move(v);
			return;
		}
		case S_AsciiStringVector:
		{
			// RW 0x42EED6: clears the vector, then appends the tokens
			std::vector<std::string> v;
			INI::parseAsciiStringVector(ini, tt, &v, nullptr);
			slot = std::move(v);
			return;
		}
		case S_PercentToReal:
		{
			float v = 0;
			INI::parsePercentToReal(ini, tt, &v, nullptr);
			slot = v;
			return;
		}
		case S_DurationUnsignedInt:
		{
			unsigned v = 0;
			INI::parseDurationUnsignedInt(ini, tt, &v, nullptr);
			slot = (long long)v;
			return;
		}
		case S_Coord3D:
		{
			Coord3D v = { 0, 0, 0 };
			INI::parseCoord3D(ini, tt, &v, nullptr);
			slot = v;
			return;
		}
		case S_ColorInt:
		{
			std::uint32_t v = 0;
			INI::parseColorInt(ini, tt, &v, nullptr);
			slot = (long long)v;
			return;
		}
		case S_VelocityReal:
		{
			float v = 0;
			INI::parseVelocityReal(ini, tt, &v, nullptr);
			slot = v;
			return;
		}
		case S_BitString16:
		{
			// RW 0x42EF5A: the bit string parser, then a bit above 15 throws the plain int 1; the value is stored as a word
			unsigned bits = 0;
			INI::parseBitString32(ini, tt, &bits, b->nameList.empty() ? nullptr : b->nameList.data());
			if (bits & 0xFFFF0000u)
			{
				throw INIPlainIntError("Bad bitstring list ThingTemplate parseBitString16");
			}
			slot = (long long)bits;
			return;
		}
		case S_Module:
			tt->parseModuleName(ini, factory, (int)b->userData);
			return;
		case S_AddModule:
			tt->parseAddModule(ini, factory);
			return;
		case S_InheritableModule:
			tt->parseInheritableModule(ini, factory);
			return;
		case S_RemoveModule:
			tt->parseRemoveModule(ini);
			return;
		case S_ReplaceModule:
			tt->parseReplaceModule(ini, factory);
			return;
		case S_Raw:
		case S_ArmorSet:
		case S_WeaponSet:
		case S_UnitSpecificFX:
		case S_UnitSpecificSounds:
		case S_Prerequisites:
		case S_LocomotorSet:
			break;
	}

	// ---- raw rows (stops S-071 / S-072) -------------------------------------------------------
	factory.noteRaw(b->name);
	RawLine header;
	const size_t headerIndex = ini->getLineNum() - 1; // the line this field is on
	header.file = ini->sourceFileAt(headerIndex);
	header.sourceLine = ini->sourceLineAt(headerIndex);
	header.text = ini->lineTextAt(headerIndex);

	if (b->kind == RwFunction::Line)
	{
		RawTokens raw;
		raw.line = header;
		for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
		{
			raw.tokens.push_back(t);
			RawTokens::Macro m;
			const char *expanded = ini->preprocessMacro(t);
			if (expanded != t) // preprocessMacro returns its argument unchanged when the token names no macro (digit fast path included)
			{
				m.isMacro = true;
				m.value = expanded;
			}
			raw.macros.push_back(std::move(m));
		}
		// COMBAT-2: the geometry rows are replayed in order into the template's shape list (the slot keeps only the last value of each row)
		if (b->name == "Geometry" || b->name == "AdditionalGeometry" || b->name == "GeometryMajorRadius" || b->name == "GeometryMinorRadius" || b->name == "GeometryHeight" ||
			b->name == "GeometryOffset" || b->name == "GeometryActive" || b->name == "GeometryIsSmall" || b->name == "GeometryName" ||
			b->name == "GeometryContactPoint") // lane BUILD-3: the contact points (RW 0xAD3D30)
		{
			std::vector<std::string> expanded;
			for (size_t i = 0; i < raw.tokens.size(); ++i)
			{
				expanded.push_back(raw.macros[i].isMacro ? raw.macros[i].value : raw.tokens[i]);
			}
			tt->friend_noteGeometryRow(b->name, std::move(expanded));
		}
		// lane BUILD-3: the KindOf rows in order (a ChildObject's +X / -X edits the copied set)
		if (b->name == "KindOf")
		{
			tt->friend_noteKindOfRow(raw);
		}
		// AUDIO-2: the voice rows (RW 0x73ACEF) carry an Eva id that `+SOUND:` keeps (ThingTemplate::voiceEvaEvent)
		if (b->fn == 0x73acef)
		{
			tt->friend_noteVoiceRow(b->name, raw.tokens.empty() ? std::string() : raw.tokens.front());
		}
		slot = std::move(raw);
		return;
	}
	if (b->kind != RwFunction::Block)
	{
		throw std::logic_error("ThingFactory: object field '" + b->name + "' parses a script block, which has no object-level handler");
	}

	// the blocks' template-level behaviour on top of their (raw) body
	auto eraseBlocks = [&]() {
		tt->m_rawBlocks.erase(std::remove_if(tt->m_rawBlocks.begin(), tt->m_rawBlocks.end(), [&](const RawBlock &r) { return r.field == b->name; }), tt->m_rawBlocks.end());
	};
	switch (b->sem)
	{
		case S_ArmorSet: // RW 0x73FAC4: the first set after a copy clears the copied ones
			if (tt->m_armorCopiedFromDefault)
			{
				tt->m_armorCopiedFromDefault = false;
				eraseBlocks();
				tt->mutableCombatSets().armors.clear();
			}
			break;
		case S_WeaponSet: // RW 0x73ED19
			if (tt->m_weaponsCopiedFromDefault)
			{
				tt->m_weaponsCopiedFromDefault = false;
				eraseBlocks();
				tt->mutableCombatSets().weapons.clear();
			}
			break;
		case S_UnitSpecificFX: // RW 0x73F5B1 / 0x73F684 clear the map before parsing into it
		case S_UnitSpecificSounds:
			eraseBlocks();
			break;
		case S_Prerequisites: // RW 0x74057B: load type 2 clears the vector first
			if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
			{
				eraseBlocks();
			}
			break;
		default:
			break;
	}
	RawBlock block;
	block.field = b->name;
	block.lines.push_back(header);
	const size_t first = ini->getLineNum();
	if (b->sem == S_WeaponSet) // WEAPON-1: the typed parser (RW 0x6C99C6), same extent as the grammar's
	{
		WeaponTemplateSet set;
		set.parseWeaponTemplateSet(ini, tt);
		tt->mutableCombatSets().weapons.push_back(std::move(set));
	}
	else if (b->sem == S_ArmorSet) // RW 0x73DD18
	{
		ArmorTemplateSet set;
		set.parseArmorTemplateSet(ini);
		tt->mutableCombatSets().armors.push_back(std::move(set));
	}
	else
	{
		factory.m_grammar.consumeBlock(ini, tt, b->fn);
	}
	const size_t last = ini->getLineNum();
	for (size_t i = first; i < last; ++i)
	{
		RawLine l;
		l.file = ini->sourceFileAt(i);
		l.sourceLine = ini->sourceLineAt(i);
		l.text = ini->lineTextAt(i);
		block.lines.push_back(std::move(l));
	}
	tt->m_rawBlocks.push_back(std::move(block));
}

// ---------------------------------------------------------------------------------------------
// stops
// ---------------------------------------------------------------------------------------------
std::vector<std::string> ThingFactory::acceptanceStops() const
{
	std::vector<std::string> out = m_modules.acceptanceStops();
	const RwBinaryData &data = m_grammar.data();
	size_t rawRows = 0, rawBlockRows = 0;
	for (const auto &b : m_bindings)
	{
		if (b->sem == S_Raw || (b->sem >= S_ArmorSet && b->sem != S_ArmorSet && b->sem != S_WeaponSet)) // WEAPON-1: those two are typed now
		{
			if (b->kind == RwFunction::Block)
			{
				++rawBlockRows;
			}
			else
			{
				++rawRows;
			}
		}
	}
	out.push_back("S-071: " + std::to_string(rawBlockRows) + " object-level nested blocks (LocomotorSet, Prerequisites, UnitSpecificSounds/FX, AutoResolve*, ...) are kept as raw lines");
	out.push_back("S-072: " + std::to_string(rawRows) + " object-table rows have parse functions with no typed port and are kept as raw tokens; field defaults of a fresh template are not ported (unset fields read as absent)");
	size_t conditional = 0, dispatch = 0, guarded = 0;
	for (const auto &kv : data.functions)
	{
		conditional += (kv.second.opens != RwFunction::Always) ? 1 : 0;
		dispatch += kv.second.dispatch ? 1 : 0;
		guarded += kv.second.nullGuarded ? 1 : 0;
	}
	out.push_back("S-074: nested-block extents come from static analysis of the binary (" + std::to_string(data.functions.size()) + " parse functions, " + std::to_string(conditional) +
		" conditional, " + std::to_string(dispatch) + " dispatching, " + std::to_string(guarded) + " null-guarded, reviewed by hand); an indirect-call block opener would read as a line");
	out.push_back("S-075: a module declaration naming a class the binary does not register crashes RotWK (NULL dereference at RW 0x73F496); this port raises an INI error");
	out.push_back("S-076: a BeginScript block without ENDSCRIPT never ends in RotWK (RW 0x42D400 has no end-of-file test); this port raises an INI error");
	out.push_back("S-077: ThingTemplate::validate (RW 0x73CE7E: default shadow strings and audio event strings) is not ported");
	return out;
}
