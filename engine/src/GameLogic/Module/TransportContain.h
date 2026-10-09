// OpenBFME. GPL-3.0.
//
// TransportContainModuleData: the INI data of TransportContain (base of HordeContain). Port of ZH
// GameEngine/Include/GameLogic/Module/TransportContain.h as changed by RotWK. Lane HORDE-1.
//
// TARGET FACTS (RotWK game.dat; S-001 caveat): field table RW 0xC5ABD8 (33 rows, golden:
// tests/data/horde1/table_transport_contain.tsv), constructor RW 0x86B425 (calls the OpenContain
// constructor RW 0x867E1B first, then replaces PassengerFilter with RW 0x763D11 (NONE), ManualPickUpFilter
// with RW 0x763DAA (ALL) and sets FadeFilter with 0x763D11). The five Type*For* fields are KindOf masks
// (parse RW 0x6564E7 -> 0x65621C). InitialPayload (RW 0x86AF0A) appends {name, count} to the vector at
// module data +0xA4, count defaults to 1 when the second token is absent; UpgradeCreationTrigger
// (RW 0x86BA2C) appends {name1, name2, count} to +0x180 and refuses a fifth entry with
// INIException(1, "iniParseQuery: Too many triggers, can only have 4."). Retail's table rows for the two
// have offset 0 and the procs use the module data address; here the rows carry the vector's offset and the
// procs use `store`, which keeps the data class embeddable (same result).

#pragma once

#include "GameLogic/Module/OpenContain.h"

struct InitialPayloadEntry
{
	std::string name;
	int count = 1;
};

struct UpgradeCreationTriggerEntry
{
	std::string first;
	std::string second;
	unsigned count = 99999; // RW 0x1869F before the parse (the parse always overwrites it)
};

struct TransportContainModuleData
{
	TransportContainModuleData(); // RW 0x86B425

	OpenContainModuleData m_open;                      // RW base (first member: address of the whole)

	int m_slotCapacity;                                // 0x98 Slots
	float m_exitPitchRate;                             // 0x9C ExitPitchRate (angular velocity)
	std::string m_exitBone;                            // 0xA0 ExitBone
	std::vector<InitialPayloadEntry> m_initialPayload; // 0xA4 InitialPayload
	float m_healthRegenPercentPerSec;                  // 0xA8 HealthRegen%PerSec
	unsigned m_exitDelay;                              // 0xAC ExitDelay (frames)
	KindOfMaskType m_typeOneForWeaponSet;              // 0xB0
	KindOfMaskType m_typeTwoForWeaponSet;              // 0xCC
	KindOfMaskType m_typeOneForWeaponState;            // 0xE8
	KindOfMaskType m_typeTwoForWeaponState;            // 0x104
	KindOfMaskType m_typeThreeForWeaponState;          // 0x120
	bool m_forceOrientationContainer;                  // 0x13C
	bool m_canGrabStructure;                           // 0x13D
	bool m_scatterNearbyOnExit;                        // 0x13E
	bool m_orientLikeContainerOnExit;                  // 0x13F
	bool m_goAggressiveOnExit;                         // 0x140
	bool m_resetMoodCheckTimeOnExit;                   // 0x141
	bool m_destroyRidersWhoAreNotFreeToExit;           // 0x142
	StoreReference m_grabWeapon;                       // 0x144 GrabWeapon (weapon template)
	bool m_fireGrabWeaponOnVictim;                     // 0x148
	int m_conditionForEntry;                           // 0x14C ConditionForEntry (model condition index, -1 unknown)
	bool m_shouldThrowOutPassengers;                   // 0x150
	unsigned m_throwOutPassengersDelay;                // 0x154 (frames)
	Coord3D m_throwOutPassengersVelocity;              // 0x158
	StoreReference m_throwOutPassengersLandingWarhead; // 0x164
	ObjectFilter m_fadeFilter;                         // 0x168 FadeFilter
	bool m_fadePassengerOnEnter;                       // 0x16C
	bool m_fadePassengerOnExit;                        // 0x16D
	float m_enterFadeTime;                             // 0x170
	float m_exitFadeTime;                              // 0x174
	bool m_fadeReverse;                                // 0x178
	float m_releaseSnappyness;                         // 0x17C
	std::vector<UpgradeCreationTriggerEntry> m_upgradeCreationTrigger; // 0x180

	static void buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset = 0);
	static const FieldParse *getFieldParse();

	static void parseInitialPayload(INI *ini, void *instance, void *store, const void *userData);
	static void parseConditionForEntry(INI *ini, void *instance, void *store, const void *userData);
	static void parseWeaponTemplate(INI *ini, void *instance, void *store, const void *userData);
	static void parseUpgradeCreationTrigger(INI *ini, void *instance, void *store, const void *userData);
};
