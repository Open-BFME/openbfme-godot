// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// SpecialPowerTemplate / SpecialPowerStore (ZH Include/Common/SpecialPower.h, Source/Common/RTS/SpecialPower.cpp): the `SpecialPower <Name>`
// INI block. Lane SPELL-1.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; each read from the disassembly):
//   * TheSpecialPowerStore is RW 0xDE878C: the template vector at store + 0xC / + 0x10, the id counter at store + 0x18.
//   * SpecialPowerTemplate is 0x88 bytes (constructor RW 0x7B1F5B): +0x04 next override, +0x08 isOverride, +0x0C flag (-1), +0x10 name, +0x14 id,
//     +0x18 Flags (0), +0x1C Enum (0 = SPECIAL_INVALID), +0x20 ReloadTime (0 frames), +0x24 RequiredSciences, +0x30 LightPointCost (0), +0x34
//     InitiateSound, +0x38 InitiateAtLocationSound, +0x3C / +0x40 the two UnitSpecificSound names, +0x44 EvaEventToPlayOnSuccess (-1), +0x48
//     DetectionTime (RW 0xD9F608 * 10 = 5 * 10 = 50 frames), +0x4C ViewObjectDuration (0), +0x50 ViewObjectRange (0.0), +0x54 RadiusCursorRadius
//     (0.0), +0x58 PublicTimer (false), +0x59 SharedSyncedTimer (false), +0x5C PalantirMovie, +0x60 ObjectFilter (a default filter built from a
//     global at RW 0xDE49E4: stop S-523), +0x64 PreventActivationConditions (ObjectStatus mask, 128 bits), +0x74 MaxCastRange (0.0), +0x78
//     ForbiddenObjectFilter (same default), +0x7C ForbiddenObjectRange (0.0), +0x80 UnitCost (0), +0x84 UnitCostDeathType (0).
//   * field table RW 0xDA5FD8 (24 rows, order below). Parsers: Flags parseBitString32 RW 0x42E840 over RW 0xDA5F34; ReloadTime / DetectionTime /
//     ViewObjectDuration parseDurationUnsignedInt RW 0x73A429; RequiredSciences parseScienceVector RW 0x73B4A0; the sounds RW 0x73B217 (audio
//     event names: stored by name, S-520); EvaEventToPlayOnSuccess RW 0x5DE588 (TheEva lookup by name: stored by name, S-520); Enum
//     parseIndexList RW 0x42E956 over RW 0xDA5C90; the reals parseReal RW 0x42ED00; ObjectFilter / ForbiddenObjectFilter RW 0x76392F;
//     PreventActivationConditions RW 0x7B1E5C -> 0x7B1B4C (ObjectStatus names RW 0xD8AFF0).
//   * the block parser RW 0x7B212F: name = getNextToken; existing = findSpecialPowerTemplate(name) (RW 0x7B1D07: the vector in order, comparing
//     each entry's FINAL OVERRIDE's name, returns that final override). Load type 2: existing -> a new template copies it (RW 0x7B1E6C, every field
//     including name and id), the existing one links it (+4) and it is marked an override; no existing -> a new template that copies the template
//     named "DefaultSpecialPower" when one exists (RW 0xC34FAC, lookup RW 0x69C146), then id = ++counter, name set (RW 0x7B1ACD), marked an
//     override and appended. Any other load type: existing -> INIException(3, "Special power '%s' already defined") (RW 0xC34F88; load type 5
//     included); new -> the same DefaultSpecialPower copy, id = ++counter (the first template has id 1), appended. Then the table parses.
//     So every template defined after a `SpecialPower DefaultSpecialPower` block inherits its values (retail data defines none).
// DONOR: ZH SpecialPower.cpp; BFME1 Common/RTS/SpecialPower.cpp.

#pragma once

#include "Common/INI.h"
#include "Common/Science.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ObjectFilter;

// The flag bits of RW 0xDA5F34 (the first seven names; see SpecialPowerNames.inc)
enum SpecialPowerFlag : std::uint32_t
{
	SPF_NEEDS_TARGET = 1u << 0,
	SPF_WATER_OK = 1u << 1,
	SPF_NEEDS_OBJECT_FILTER = 1u << 2,
	SPF_LIMIT_DISTANCE = 1u << 3,
	SPF_NO_FORBIDDEN_OBJECTS = 1u << 4,
	SPF_RESPECT_RECHARGE_TIME_DISCOUNT = 1u << 5,
	SPF_PATHABLE_ONLY = 1u << 6,
};

class SpecialPowerTemplate
{
public:
	SpecialPowerTemplate *getFinalOverride() { return m_nextOverride ? m_nextOverride->getFinalOverride() : this; }
	const SpecialPowerTemplate *getFinalOverride() const { return m_nextOverride ? m_nextOverride->getFinalOverride() : this; }

	const std::string &getName() const { return m_name; }
	unsigned getID() const { return m_id; }
	int getSpecialPowerType() const { return m_type; }
	unsigned getReloadTime() const { return m_reloadTime; }
	const ScienceVec &getRequiredSciences() const { return m_requiredSciences; }
	bool isSharedNSync() const { return m_sharedNSync; }
	bool hasPublicTimer() const { return m_publicTimer; }
	std::uint32_t getFlags() const { return m_flags; }
	float getRadiusCursorRadius() const { return m_radiusCursorRadius; }

	SpecialPowerTemplate *m_nextOverride = nullptr;  // +0x04
	bool m_isOverride = false;                       // +0x08
	std::string m_name;                              // +0x10
	unsigned m_id = 0;                               // +0x14
	std::uint32_t m_flags = 0;                       // +0x18
	int m_type = 0;                                  // +0x1C SPECIAL_INVALID
	unsigned m_reloadTime = 0;                       // +0x20 frames
	ScienceVec m_requiredSciences;                   // +0x24
	int m_lightPointCost = 0;                        // +0x30
	std::string m_initiateSound;                     // +0x34 (name, S-520)
	std::string m_initiateAtLocationSound;           // +0x38 (name, S-520)
	std::string m_unitSpecificInitiateVoice;         // +0x3C
	std::string m_unitSpecificEnterStateVoice;       // +0x40
	std::string m_evaEventToPlayOnSuccess;           // +0x44 (name, S-520; RW stores the EVA index, -1 by default)
	unsigned m_detectionTime = 50;                   // +0x48 (RW 0xD9F608 LOGICFRAMES_PER_SECOND 5 * 10)
	unsigned m_viewObjectDuration = 0;               // +0x4C
	float m_viewObjectRange = 0.0f;                  // +0x50
	float m_radiusCursorRadius = 0.0f;               // +0x54
	bool m_publicTimer = false;                      // +0x58
	bool m_sharedNSync = false;                      // +0x59
	std::string m_palantirMovie;                     // +0x5C
	std::shared_ptr<const ObjectFilter> m_objectFilter;          // +0x60 (null = the default filter, S-523)
	std::array<std::uint32_t, 4> m_preventActivationConditions{}; // +0x64 ObjectStatus mask
	float m_maxCastRange = 0.0f;                     // +0x74
	std::shared_ptr<const ObjectFilter> m_forbiddenObjectFilter; // +0x78 (S-523)
	float m_forbiddenObjectRange = 0.0f;             // +0x7C
	int m_unitCost = 0;                              // +0x80
	int m_unitCostDeathType = 0;                     // +0x84
};

class SpecialPowerStore
{
public:
	SpecialPowerStore() = default;
	~SpecialPowerStore();
	SpecialPowerStore(const SpecialPowerStore &) = delete;
	SpecialPowerStore &operator=(const SpecialPowerStore &) = delete;

	void parseSpecialPowerDefinition(INI *ini);              // RW 0x7B212F
	static void parseSpecialPowerDefinitionGlobal(INI *ini); // throws INIException(3, "TheSpecialPowerStore==NULL") without a store
	void resetOverrides();

	// RW 0x7B1D07 / 0x69C146: the final override of the template with this name (case-sensitive), null when none
	const SpecialPowerTemplate *findSpecialPowerTemplate(const std::string &name) const;
	// by id (ZH findSpecialPowerTemplateByID: the stored entries' ids; the final override)
	const SpecialPowerTemplate *findSpecialPowerTemplateByID(unsigned id) const;
	size_t getNumSpecialPowers() const { return m_templates.size(); }
	const SpecialPowerTemplate *getSpecialPowerByIndex(size_t i) const { return i < m_templates.size() ? m_templates[i]->getFinalOverride() : nullptr; } // RW 0x7B1AE7

	static const char *const *specialPowerTypeNames(); // RW 0xDA5C90
	static const char *const *specialPowerFlagNames(); // RW 0xDA5F34

private:
	SpecialPowerTemplate *findMutable(const std::string &name);
	SpecialPowerTemplate *newTemplate();
	std::vector<std::unique_ptr<SpecialPowerTemplate>> m_owned;
	std::vector<SpecialPowerTemplate *> m_templates; ///< store + 0xC
	unsigned m_nextID = 0;                           ///< store + 0x18
};

extern thread_local SpecialPowerStore *TheSpecialPowerStore;
