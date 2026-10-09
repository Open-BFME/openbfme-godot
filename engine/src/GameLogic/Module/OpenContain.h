// OpenBFME. GPL-3.0.
//
// OpenContainModuleData: the INI data of OpenContain, the base of TransportContain and HordeContain.
// Port of ZH GameEngine/Include/GameLogic/Module/OpenContain.h (OpenContainModuleData) as changed by
// RotWK. Lane HORDE-1 (spec horde-and-movement.md 1.1 / 1.2, checklist step 2).
//
// TARGET FACTS (RotWK game.dat; S-001 caveat): field table RW 0xC59F30 (24 rows, golden:
// tests/data/horde1/table_open_contain.tsv), constructor RW 0x867E1B. The retail class derives from
// ModuleData (OBJ-1); this port is a standalone standard-layout struct so it can be wrapped by
// OBJ-1's ModuleFactory: `buildFieldParse(MultiIniFieldParse &)` adds the field table, and a derived
// module data embeds this struct as its FIRST member (the INI parse procs are handed the address of
// the outermost module data, which is therefore also the address of this struct).
//
// Differences from ZH recorded here: ShowPips / CollidePickup / ... defaults, the Bone* and Modifier*
// fields, ObjectFilter based PassengerFilter / ManualPickUpFilter, the audio events are looked up at
// parse time (ContainParseHooks.h).

#pragma once

#include "Common/INI.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/ObjectFilter.h"

#include <map>
#include <string>
#include <vector>

// A name that retail resolves to a pointer in another store at parse time.
struct StoreReference
{
	std::string name;       // as written ("" when absent / NoSound)
	bool resolved = false;  // the store knew the name
};

// RW 0x8677D2: `PassengerBonePrefix = PassengerBone:<prefix> KindOf:<mask>`.
struct PassengerBonePrefixEntry
{
	KindOfMaskType kindOf{}; // entry +0x00 (0x1C bytes)
	std::string bonePrefix;  // entry +0x1C
};

// RW 0x866FF0 stores the parsed mask at module data +0x58 and sets the flag byte +0x86; the port keeps
// both in one struct so the parse proc needs only `store`.
struct ObjectStatusOfContainedField
{
	ObjectStatusMaskType mask{};
	bool set = false;
};

struct OpenContainModuleData
{
	OpenContainModuleData(); // RW 0x867E1B

	StoreReference m_enterSound;                       // 0x38 EnterSound (audio event)
	StoreReference m_exitSound;                        // 0x3C ExitSound
	ObjectFilter m_passengerFilter;                    // 0x40 PassengerFilter
	ObjectFilter m_manualPickUpFilter;                 // 0x44 ManualPickUpFilter
	std::vector<PassengerBonePrefixEntry> m_passengerBonePrefix; // 0x48 PassengerBonePrefix
	std::map<unsigned, ModelConditionMask> m_boneSpecificConditionState; // 0x4C BoneSpecificConditionState
	ObjectStatusOfContainedField m_objectStatusOfContained; // 0x58 ObjectStatusOfContained, flag 0x86
	std::vector<std::string> m_modifierToGiveOnExit;   // 0x88 ModifierToGiveOnExit
	float m_damagePercentToUnits;                      // 0x6C DamagePercentToUnits (percent)
	float m_passengersTestCollisionHeight;             // 0x68 PassengersTestCollisionHeight
	int m_containMax;                                  // 0x70 ContainMax
	int m_numberOfExitPaths;                           // 0x74 NumberOfExitPaths
	unsigned m_doorOpenTime;                           // 0x78 DoorOpenTime (frames)
	bool m_allowOwnPlayerInsideOverride;               // 0x7C
	bool m_allowAlliesInside;                          // 0x7D
	bool m_allowEnemiesInside;                         // 0x7E
	bool m_allowNeutralInside;                         // 0x7F
	bool m_showPips;                                   // 0x80
	bool m_collidePickup;                              // 0x81
	bool m_passengersInTurret;                         // 0x85
	bool m_ejectPassengersOnDeath;                     // 0x82
	bool m_killPassengersOnDeath;                      // 0x83
	bool m_enabled;                                    // 0x84
	unsigned m_modifierRequiredTime;                   // 0x94 ModifierRequiredTime (frames)

	// ZH style: adds this class's FieldParse table. `extraOffset` is the offset of this struct inside
	// the outermost module data (0 for the first member).
	static void buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset = 0);

	// Stops this data class reports (see tests): none beyond the hooks (S-083).
	static const FieldParse *getFieldParse();

	// Individual retail parse procs (also used by tests).
	static void parseObjectStatusOfContained(INI *ini, void *instance, void *store, const void *userData);
	static void parsePassengerBonePrefix(INI *ini, void *instance, void *store, const void *userData);
	static void parseBoneSpecificConditionState(INI *ini, void *instance, void *store, const void *userData);
	static void parseModifierToGiveOnExit(INI *ini, void *instance, void *store, const void *userData);
	static void parseAudioEvent(INI *ini, void *instance, void *store, const void *userData);
};
