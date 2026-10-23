// OpenBFME. GPL-3.0.

#pragma once

#include "Common/INI.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/Module/UpdateModule.h"

#include <memory>
#include <string>

struct ObjectFilter;
class CommandButton;
class Object;
class Player;

class BloodthirstyUpdateModuleData : public ModuleData
{
public:
	std::shared_ptr<ObjectFilter> m_sacrificeFilter;
	float m_experienceModifier = 1.0f;
	std::string m_initiateVoice;
	// BFME1-only data; consuming these fields is unported (S-5017).
	// Donor: BloodthirstyUpdateModuleDataConstructor.cpp, integer +0x0C = 1.
	int m_bfme1NumToSacrifice = 1;
	std::string m_bfme1InitiateVoice2;

	static void buildFieldParse(MultiIniFieldParse &p);
};

// TARGET FACTS (RotWK 2.01, caveat S-001): canSacrifice RW 0x850DF9, isValidSacrifice RW 0x850E83, addSacrifice RW 0x850EEC, sacrificeHorde
// RW 0x851080, update RW 0x85123F (read in Ghidra; tier A / B to the byte-matched BFME2 donor
// Code/GameEngine/Source/GameLogic/Object/Update/BloodthirstyUpdateIfaceChecks.cpp, rva0044E12F .. rva0044E533). MSG_SACRIFICE (1056): the
// dispatcher gives the selected group's FRONT member's interface the target, source 0 (donor GameLogicDispatch.cpp case 1056 -> AIGroup::rva0036DC6F).
class BloodthirstyUpdate : public UpdateModule
{
public:
	BloodthirstyUpdate(Thing *thing, const BloodthirstyUpdateModuleData *data);
	UpdateSleepTime update() override;
	// RW 0x8B2A30 (the update interface vtable RW 0xC5567C, slot 1): 0x850F76(mask, 3, 4) = DISABLED_HELD | DISABLED_PARALYZED, so a held or
	// paralyzed horde still runs update() (its victim-state cleanup and its 2 s + 1 frame sleep)
	DisabledMaskType getDisabledTypesToProcess() const override { return (DisabledMaskType)((1u << 3) | (1u << 4)); }
	void crc(StateHasher &hasher) const override;
	// OpenBFME native snapshot (S-4190), the crc's fields (retail's xfer, BFME2 BloodthirstyUpdateXfer.cpp: the target, the sacrificer, a counter)
	OBFME_MODULE_XFER(BloodthirstyUpdate) // lane SAVE-4b

	static BloodthirstyUpdate *find(Object &object);
	bool canSacrifice(Object *candidate) const;
	bool isValidSacrifice(Object *candidate) const;
	void sacrificeHorde(Object *victim, CommandSourceType source);
	// the first BLOODTHIRSTY button of the object's command set (null: the object offers no sacrifice)
	static const CommandButton *sacrificeButton(const Object &object);
	// RW 0x81E60B's prelude for a context command: the button's NEED_TARGET_*_OBJECT bit for `player`'s relationship to the target's team (no team: NEUTRAL)
	static bool relationshipAllowed(const CommandButton &button, const Player &player, const Object &target);
	// the eligibility the client tests before it sends MSG_SACRIFICE (RW 0x81E60B: relationship gate, then the action check: another horde, canSacrifice),
	// re-tested at dispatch for the issuing player (review DEVPORT-7e: a forged message must not reserve a horde the UI would refuse)
	bool isSacrificeTargetFor(const Player &issuer, Object *target) const;
	void addSacrifice(Object *victim);
	void cancelSacrifice();
	const std::string &initiateVoice() const;

	ObjectID bestTargetID() const { return m_bestTargetID; }
	ObjectID sacrificerID() const { return m_sacrificerID; }
	unsigned kills() const { return m_kills; }

private:
	bool matchesFilter(const Object &candidate) const;
	void clearVictimState(Object &victim);

	const BloodthirstyUpdateModuleData *m_data;
	ObjectID m_bestTargetID = INVALID_ID;
	ObjectID m_sacrificerID = INVALID_ID;
	unsigned m_kills = 0;
};

bool cancelBloodthirstyCommand(Object &object, int command, Object *target);
