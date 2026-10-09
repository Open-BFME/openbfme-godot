// OpenBFME. GPL-3.0.
//
// ThingTemplate: an Object / ChildObject / ObjectReskin definition. Port of ZH
// Include/Common/ThingTemplate.h and Source/Common/Thing/ThingTemplate.cpp (module lists, ModuleInfo,
// the module-editing parsers) as RotWK 2.01 changes them (RW = RotWK game.dat, caveat S-001;
// spec ini-and-object-model.md sections 4.2-4.8).
//
// What is ported exactly:
//   * the four module lists (RW +0x2E4 behavior, +0x2F0 draw, +0x2FC client update, +0x308 client
//     behavior) and the 20-byte nugget {name, tag, ModuleData*, interfaceMask, copiedFromDefault,
//     inheritable} (RW 0x73DCED find, 0x73E1D2 clear-by-tag, 0x73E215 clear-copied, 0x73E24D clear-AI,
//     0x73E280 clear-slot-7, 0x73EF3B addModuleInfo, 0x73CB3B setCopiedFromDefault);
//   * parseModuleName (RW 0x73F24D) and the module-editing keywords AddModule / InheritableModule /
//     RemoveModule / ReplaceModule (RW 0x73BEC3, 0x73BF17, 0x73E716, 0x73E7ED);
//   * copyFrom / setCopiedFromDefault / setReskinnedFrom (RW 0x7405B1, 0x73CEEA, 0x73FA52);
//   * the Overridable chain used by map.ini overrides (ZH Include/Common/Overridable.h).
// Object-level FIELD values are kept in one slot per field-table row (FieldValue): rows whose parse
// function maps to an INI parser hold the parsed value, the others hold their tokens verbatim
// (RawTokens), nested blocks without a ported parser are kept as RawBlock lines. Acceptance stops
// S-071 / S-072 (see ThingFactory.h).

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/Module.h"
#include "Common/NameKeyGenerator.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

class INI;
class ThingFactory;

// A line stored verbatim from an INI file.
struct RawLine
{
	std::string file;
	int sourceLine = 0;
	std::string text;
};

// The tokens of a field line whose parse function is not ported (stop S-072).
struct RawTokens
{
	std::vector<std::string> tokens;
	RawLine line;
	// MAPOBJ-1: INI::preprocessMacro of each token AT PARSE TIME (the macro table grows while the files load, so a token that names a
	// macro defined by a LATER file stays what it was, e.g. KindOf = ... ARAGORN before experiencelevels.ini defines ARAGORN).
	// Parallel to `tokens`; isMacro is false when the token named no macro then.
	struct Macro
	{
		bool isMacro = false;
		std::string value;
	};
	std::vector<Macro> macros;
};

// A nested object-level block (ArmorSet, WeaponSet, ...) whose typed parser is not ported (stop S-071):
// the header line and every line up to and including its End.
struct RawBlock
{
	std::string field;
	std::vector<RawLine> lines;
};

typedef std::variant<std::monostate, bool, long long, float, std::string, std::vector<std::string>, Coord3D, RawTokens> FieldValue;

// RW 0x73F317 / 0x73F4C1 compare the mode byte (+0x608) with 1 and 2.
enum ModuleParseMode
{
	MODULEPARSE_NORMAL = 0,
	MODULEPARSE_ADD_REMOVE_REPLACE = 1,
	MODULEPARSE_INHERITABLE = 2
};

// WEAPON-1: the typed WeaponSet / ArmorSet blocks (GameLogic/WeaponSet.h, ArmorSet.h). Forward declared: those headers pull in the bit
// flag registries, which must not reach every translation unit that includes the template.
class WeaponTemplateSet;
class ArmorTemplateSet;
struct ThingCombatSets;

class ThingTemplate
{
public:
	struct Nugget
	{
		std::string name; ///< module class
		std::string tag;  ///< ModuleTag
		std::shared_ptr<const ModuleData> data;
		int interfaceMask = 0;
		bool copiedFromDefault = false;
		bool inheritable = false;
	};

	// ZH ThingTemplate.h ModuleInfo: a vector of nuggets.
	class ModuleInfo
	{
	public:
		const std::vector<Nugget> &nuggets() const { return m_info; }
		size_t size() const { return m_info.size(); }
		void push_back(const Nugget &n) { m_info.push_back(n); }
		void clear() { m_info.clear(); }
		// RW 0x73DCED: the nugget with this tag (tags compare with strcmp), or nullptr.
		const Nugget *find(const std::string &tag) const;
		// RW 0x73CB3B
		void setCopiedFromDefault(bool value);
		// RW 0x73E215: erase nuggets with (mask & m) != 0 && copiedFromDefault && !inheritable.
		bool clearCopiedFromDefaultEntries(int mask);
		// RW 0x73E24D: erase nuggets whose data is an AI module data.
		bool clearAiModuleInfo();
		// RW 0x73E280: erase nuggets whose data answers true to the slot 7 predicate.
		bool clearSlot7ModuleInfo();
		// RW 0x73E1D2: erase every nugget with this tag; the class of the last one erased is returned.
		bool clearModuleDataWithTag(const std::string &tag, std::string &clearedModuleNameOut);

	private:
		std::vector<Nugget> m_info;
	};

	ThingTemplate() = default;

	// ---- identity (RW +0x64 name, +0x5E8 template id) ---------------------------------------
	const std::string &getName() const { return m_nameString; }
	unsigned short getTemplateID() const { return m_templateID; }
	void friend_setTemplateName(const std::string &name) { m_nameString = name; }
	void friend_setTemplateID(unsigned short id) { m_templateID = id; }

	// ---- Overridable (ZH Include/Common/Overridable.h) --------------------------------------
	void markAsOverride() { m_isOverride = true; }
	bool isOverride() const { return m_isOverride; }
	const ThingTemplate *getNextOverride() const { return m_nextOverride; }
	ThingTemplate *friend_getNextOverride() { return m_nextOverride; }
	void setNextOverride(ThingTemplate *next) { m_nextOverride = next; }
	const ThingTemplate *getFinalOverride() const;
	ThingTemplate *friend_getFinalOverride();

	// ---- copying (ZH ThingTemplate.cpp:1219-1243; RW 0x7405B1, 0x73CEEA, 0x73FA52) -----------
	// `*this = *that`, preserving this template's name and id (and the master-list link, which lives in the
	// ThingFactory here) and, unlike ZH, its Overridable fields: RotWK's Overridable assignment copies nothing
	// (RW 0x92BB7C is `mov eax, ecx; ret 4`).
	void copyFrom(const ThingTemplate *that);
	// armor and weapon "copied from default" flags, and every nugget's copiedFromDefault, in all four lists
	void setCopiedFromDefault();
	// RW 0x73FA52: appends the source's name to the reskinned-from list
	void setReskinnedFrom(const ThingTemplate *source);
	const std::vector<std::string> &reskinnedFrom() const { return m_reskinnedFrom; }

	// ---- modules ------------------------------------------------------------------------------
	const ModuleInfo &behaviorModules() const { return m_behavior; }
	const ModuleInfo &drawModules() const { return m_draw; }
	const ModuleInfo &clientUpdateModules() const { return m_clientUpdate; }
	const ModuleInfo &clientBehaviorModules() const { return m_clientBehavior; }
	ModuleInfo &moduleList(ModuleType type);
	const ModuleInfo &moduleList(ModuleType type) const;
	// RW 0x73E2B3: erase the tag from all four lists; false when no list had it.
	bool removeModuleInfo(const std::string &moduleToRemove, std::string &clearedModuleNameOut);
	// RW 0x73EF3B (spec 4.4 step 8): the unique-tag check and, for a ChildObject, replace-by-tag.
	void addModuleInfo(ModuleInfo &list, const std::string &name, const std::string &moduleTag, const std::shared_ptr<const ModuleData> &data, int interfaceMask, bool inheritable, bool isChild);
	// RW 0x73F24D parseModuleName. `userType` is the row's userData: a ModuleType, or 999 for Body.
	void parseModuleName(INI *ini, ThingFactory &factory, int userType);
	// RW 0x73BEC3 / 0x73BF17 / 0x73E716 / 0x73E7ED
	void parseAddModule(INI *ini, ThingFactory &factory);
	void parseInheritableModule(INI *ini, ThingFactory &factory);
	void parseRemoveModule(INI *ini);
	void parseReplaceModule(INI *ini, ThingFactory &factory);

	// every module as "class tag", behavior, draw, client update, client behavior (tests, reports)
	std::vector<std::pair<std::string, std::string>> moduleList() const;

	// ---- object-level fields -----------------------------------------------------------------
	// the slot of the field-table row of this name, or nullptr when it was never set
	const FieldValue *findField(const std::string &name) const;
	// VIS-1: the same slots by index: the row-name table (shared by the templates of one factory, fixed by the binary's field table) and the slot's live value
	// (nullptr when never set), so a hot reader resolves a name once per table and still reads the current value
	const std::vector<std::string> *fieldNameTable() const { return m_fieldNames; }
	const FieldValue *fieldAt(size_t slot) const
	{
		return slot < m_fields.size() && !std::holds_alternative<std::monostate>(m_fields[slot]) ? &m_fields[slot] : nullptr;
	}
	std::vector<std::string> fieldNames() const; ///< names of the fields that were set
	const std::vector<RawBlock> &rawBlocks() const { return m_rawBlocks; }
	// AUDIO-2: a voice row's value (the audio table rows parsed by RW 0x73ACEF -> 0x73AB45 store {Eva event id, audio event}). Each line changes the
	// slot the template already has (from an earlier line or the template it was copied from): "NoSound" clears both halves; `EVA:<event>` sets only the
	// Eva half (RW 0x73ABC5 .. 0x73ABF8 writes the id and leaves the sound at +4); `+SOUND:<event>` sets only the sound (RW 0x73AC21, the id kept);
	// any other value sets the sound and clears the Eva half (RW 0x73AC7F). Names are kept (retail resolves them at parse time); "" = none.
	struct VoiceRowValue
	{
		std::string eva, sound;
	};
	// nullptr when no line of the row was parsed (here or in the template copied from)
	const VoiceRowValue *voiceRow(const std::string &row) const;
	void friend_noteVoiceRow(const std::string &row, const std::string &firstToken);
	// COMBAT-2: the Geometry / AdditionalGeometry / GeometryMajorRadius / ... rows in the order they were parsed (RW applies each to the template's shape list at +0xA0 as it parses: Geometry sets
	// shape 0, AdditionalGeometry appends one, every other row changes the LAST shape). The tokens are macro-expanded. A template copied from another (ChildObject, reskin, the default
	// template) starts with the source's events, exactly as retail's copied shape list. Object::geometry replays them (GameLogic/Object/ObjectGeometry.h).
	struct GeometryEvent
	{
		std::string row;
		std::vector<std::string> tokens;
	};
	const std::vector<GeometryEvent> &geometryEvents() const { return m_geometryEvents; }
	// lane BUILD-3: every KindOf row in parse order, a copied template's rows first (RW parses the bit string ON the copied value: a ChildObject's `KindOf = +X`
	// edits its parent's set, RW 0x65621C). KindOfTokens::parseTemplate applies them; the "KindOf" field keeps only the last row
	const std::vector<RawTokens> &kindOfRows() const { return m_kindOfRows; }
	void friend_noteKindOfRow(const RawTokens &raw) { m_kindOfRows.push_back(raw); }
	void friend_noteGeometryRow(const std::string &row, std::vector<std::string> tokens) { m_geometryEvents.push_back(GeometryEvent{ row, std::move(tokens) }); }
	// WEAPON-1: the typed WeaponSet / ArmorSet blocks in parse order (the same blocks as rawBlocks() "WeaponSet" / "ArmorSet"; RW +0x358 /
	// +0x370). The best match for an object's flags is FindWeaponTemplateSet / FindArmorTemplateSet.
	const std::vector<WeaponTemplateSet> &weaponTemplateSets() const;
	const std::vector<ArmorTemplateSet> &armorTemplateSets() const;
	bool armorCopiedFromDefault() const { return m_armorCopiedFromDefault; }
	bool weaponsCopiedFromDefault() const { return m_weaponsCopiedFromDefault; }
	ModuleParseMode moduleParsingMode() const { return m_moduleParsingMode; }

private:
	friend class ThingFactory;

	std::string m_nameString;
	unsigned short m_templateID = 0;

	// Overridable
	ThingTemplate *m_nextOverride = nullptr;
	bool m_isOverride = false;

	ModuleInfo m_behavior;
	ModuleInfo m_draw;
	ModuleInfo m_clientUpdate;
	ModuleInfo m_clientBehavior;
	ModuleParseMode m_moduleParsingMode = MODULEPARSE_NORMAL;
	std::string m_moduleBeingReplacedName; ///< RW +0x94
	std::string m_moduleBeingReplacedTag;  ///< RW +0x98
	bool m_armorCopiedFromDefault = false;   ///< RW +0x5F9
	bool m_weaponsCopiedFromDefault = false; ///< RW +0x5FA
	std::vector<std::string> m_reskinnedFrom;

	// field slots (see ThingFactory: one per row of the object table, then the audio table) and the
	// names of the slots, shared by every template of one factory
	std::vector<FieldValue> m_fields;
	const std::vector<std::string> *m_fieldNames = nullptr;
	const std::unordered_map<std::string, size_t> *m_fieldIndex = nullptr; ///< the factory's name -> slot index of m_fieldNames (unique names)
	std::vector<RawBlock> m_rawBlocks;
	std::vector<GeometryEvent> m_geometryEvents;
	std::vector<RawTokens> m_kindOfRows;
	std::vector<std::pair<std::string, VoiceRowValue>> m_voiceRows; ///< AUDIO-2: row name -> both halves (see voiceRow); copied with the template
	std::shared_ptr<ThingCombatSets> m_combatSets; ///< copy-on-write (a copy of the template shares it until a set is added)
	ThingCombatSets &mutableCombatSets();
};
