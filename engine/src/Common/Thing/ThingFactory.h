// OpenBFME. GPL-3.0.
//
// ThingFactory: owns the ThingTemplates and parses the Object / ChildObject / ObjectReskin blocks.
// Port of ZH Source/Common/Thing/ThingFactory.cpp (newTemplate, newOverride, reset) and
// parseObjectDefinition as RotWK 2.01 has it (RW 0x6D2880, 0x6D27AD, 0x6D2733; spec 4.1-4.2).
//
// TARGET FACTS (RW addresses, caveat S-001):
//   * parseObjectDefinition(ini, name, reskinFrom, childOf) (RW 0x6D2880):
//       tt = contains(name) ? find(name) : NULL
//       !tt:  tt = newTemplate(name); if load type == 2 tt->markAsOverride()
//       tt, load type 5:  tt = newTemplate(name)   (developer reload; a log line is written)
//       tt, load type 2:  tt = newOverride(tt)
//       tt, other:        parse INTO the existing template (values merge)
//     then, with the MultiIniFieldParse {object table @RW 0xDA3DF8 extra 0, audio table @0xC26720
//     extra 0x124} (RW 0x73C13C):
//       childOf:    parent = find(childOf) or throw 3 "ChildObject must come after the original Object (%s, %s)."
//                   tt->copyFrom(parent); tt->setCopiedFromDefault(); load type := 4; parse; load type restored
//       reskinFrom: src = find(...) or throw 3 "ObjectReskin must come after ..."; copyFrom, setCopiedFromDefault,
//                   setReskinnedFrom(src); parse with the FULL table
//       else:       parse
//     then validate (RW 0x73CE7E, stop S-077).
//   * newTemplate (RW 0x6D27AD): a template is copied from "DefaultThingTemplate" when that exists (copying flag
//     RW 0xDE78F0 set around the copy, then setCopiedFromDefault), gets the next 16-bit id (the counter starts at
//     1, RW 0x6D1ACD, and the id is assigned before the increment), gets its name and joins the list and the
//     name map. The map compares names case-SENSITIVELY (AsciiString::compare, RW 0x4065AA).
//   * AddModule / InheritableModule / ReplaceModule re-enter the INI loop with the OBJECT table only (RW 0x73BEC3
//     pushes 0xDA3DF8): audio fields are unknown fields inside them.
//
// Acceptance stops registered by this class (see acceptanceStops()):
//   S-071 object-level nested blocks are kept as raw lines; S-072 object fields whose parse function is not
//   ported are kept as raw tokens, and field defaults of a fresh template are not ported; S-077 the
//   post-parse validate step (RW 0x73CE7E) is not ported; S-075 / S-076 are reported by ModuleFactory and
//   RwGrammar.

#pragma once

#include "Common/INI.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/RwGrammar.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/ObjectTypes.h"

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class GameLogic;
class Object;
class Team;

class ThingFactory
{
public:
	ThingFactory(NameKeyGenerator &keys, ModuleFactory &modules, const RwGrammar &grammar);
	~ThingFactory();
	ThingFactory(const ThingFactory &) = delete;
	ThingFactory &operator=(const ThingFactory &) = delete;

	// Registers the Object, ObjectReskin and ChildObject block parsers (INIObject.cpp in B1).
	void registerBlocks(INIBlockRegistry &registry);

	// ZH ThingFactory.h
	const ThingTemplate *findTemplate(const std::string &name) const;
	bool contains(const std::string &name) const { return m_byName.count(name) != 0; }
	// RW 0x6CFE6C ThingFactory::findByTemplateID (lane PROD-1): the master-list template with this 16-bit id (the id the INI loader assigned, RW +0x5E8), or null
	const ThingTemplate *findByTemplateID(unsigned short id) const;
	ThingTemplate *newTemplate(const std::string &name);
	ThingTemplate *newOverride(ThingTemplate *thingTemplate);
	// RW 0x6D2880
	void parseObjectDefinition(INI *ini, const std::string &name, const std::string &reskinFrom, const std::string &childOf);
	// RW 0x6D165E / ZH ThingFactory::newObject (Common/Thing/ThingFactoryObjects.cpp; lane LOGIC-1): the whole creation order of spec 5.3
	// (see GameLogic/Object/Object.h). A null template gives null. `id` 0 = the next id.
	Object *newObject(GameLogic &logic, const ThingTemplate *tt, Team *team, const ObjectStatusMaskType &status, ObjectID id = INVALID_ID);
	// ZH ThingFactory::reset: deletes override chains and the templates created only for a map
	void reset();

	// Bookkeeping that retail does not keep: every Object / ChildObject / ObjectReskin block parsed, in order
	// (the retail golden counts templates by header kind and reports errors by file and line).
	struct DefinitionRecord
	{
		std::string name;
		std::string kind; ///< "Object", "ChildObject" or "ObjectReskin"
		std::string file;
		int line = 0;
		size_t firstModuleData = 0; ///< index range of the module data this block made (ModuleFactory::moduleDataClass)
		size_t endModuleData = 0;
	};
	const std::vector<DefinitionRecord> &definitions() const { return m_definitions; }

	// the master list, in creation order (overrides are not in it)
	const std::vector<ThingTemplate *> &templates() const { return m_list; }
	size_t templateCount() const { return m_list.size(); }

	NameKeyGenerator &nameKeys() { return m_keys; }
	ModuleFactory &moduleFactory() { return m_modules; }
	const RwGrammar &grammar() const { return m_grammar; }

	// the single-table form of the object table, for the module-editing keywords' re-entry
	const FieldParse *objectFieldParse() const { return m_objectRows.data(); }
	// object table (extra 0) + audio table (extra 0x124), RW 0x73C13C
	void buildFieldParse(MultiIniFieldParse &p) const;

	// field slot names: the object table's rows, then the audio table's
	const std::vector<std::string> &fieldSlotNames() const { return m_slotNames; }
	size_t fieldSlotCount() const { return m_slotNames.size(); }

	// how many field lines / blocks have been stored raw so far, by field name (stops S-071, S-072)
	const std::map<std::string, size_t> &rawFieldCounts() const { return m_rawFieldCounts; }
	// the rows of the object tables stored raw (no typed port: raw tokens or raw nested blocks): names, in table order
	std::vector<std::string> rawFieldRowNames() const;

	// The acceptance stops of the object model as report lines (module factory included).
	std::vector<std::string> acceptanceStops() const;

private:
	friend class ThingTemplate;
	struct FieldBinding;
	enum Semantic : int;

	static void parseField(INI *ini, void *instance, void *store, const void *userData);
	static Semantic semanticOf(const std::string &semantic);
	void buildTables();
	void noteRaw(const std::string &field) { ++m_rawFieldCounts[field]; }

	NameKeyGenerator &m_keys;
	ModuleFactory &m_modules;
	const RwGrammar &m_grammar;

	std::deque<std::unique_ptr<ThingTemplate>> m_arena; ///< every template ever made (masters and overrides)
	std::vector<ThingTemplate *> m_list;                ///< the master list, creation order
	std::map<std::string, ThingTemplate *> m_byName;    ///< case-sensitive (RW 0x4065AA)
	unsigned short m_nextTemplateID = 1;                ///< RW 0x6D1ACD

	std::vector<std::string> m_slotNames;
	std::unordered_map<std::string, size_t> m_slotIndex; ///< m_slotNames' name -> slot (ThingTemplate::findField)
	std::vector<std::unique_ptr<FieldBinding>> m_bindings;
	std::vector<FieldParse> m_objectRows;
	std::vector<FieldParse> m_audioRows;
	std::map<std::string, size_t> m_rawFieldCounts;
	std::vector<DefinitionRecord> m_definitions;
};
