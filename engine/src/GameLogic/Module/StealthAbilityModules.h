// OpenBFME. GPL-3.0.
//
// Lane STEALTH-2: the abilities that hide objects. Both classes are RotWK's (no Zero Hour donor); every step below is read from the binary.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json, field-tables.json):
//   * ToggleHiddenSpecialAbilityUpdate (create RW 0x64F420, constructor RW 0x8B184A after SpecialAbilityUpdate's RW 0x851D42, vtable RW 0xC6BE90, update
//     interface RW 0xC6BE84; data RW 0x64F45B -> 0x8B1831: the SpecialAbilityUpdate table RW 0xC55F40 + RW 0xC05A8C ShowPalantirTimer +0xD0, default false).
//     The module's + 0x88 is the frame the object was hidden (zeroed by the constructor, xfer RW 0x8B1938, read by slot 0x64 RW 0x69AE14). Virtual slots:
//       - 0x40 continuePreparation RW 0x8B42F3 (ToggleMounted's: the second trigger does not prepare);
//       - 0x44 triggerAbilityEffect RW 0x8B1909: the base's (RW 0x853EDF), then on the first trigger: the object HIDDEN (status 0x10) unhides (slot 0x5C), else
//         hides (slot 0x60);
//       - 0x5C unhide RW 0x8B19AC: a HIDDEN object loses HIDDEN (RW 0x62684D), the model condition HIDDEN (0x103, Object + 0x12C bit 3) and the weapon set
//         flag HIDDEN (0x3D, RW 0x691106); its special power module (RW 0x68C26D) resumes its countdown (slot 0x24 pauseCountdown(false)); the object's
//         first module named "InvisibilityUpdate" (RW 0x68BDA5) goes inactive (RW 0x8A7049(false));
//       - 0x60 hide RW 0x8B1A70: the special power update interface's slot 0x20 (RW 0x851B6E: SpecialAbilityUpdate's conditions RW 0x851551; the enums 0x27 /
//         0x28 also need room in the object's contain) must pass; an object not HIDDEN with an AI: the special power module pauses its countdown
//         (pauseCountdown(true)), an object moving faster than 0.1 (RW 0x68BE97 -> 0x68B34C, RW 0xBD83D4) or whose AI state is active (AI vslot 0x1BC) is
//         idled by the AI (RW 0x5E821A(2)), + 0x88 = now, HIDDEN on, the weapon set flag HIDDEN on (RW 0x691059), the model condition HIDDEN on, the first
//         "InvisibilityUpdate" goes active (RW 0x8A7049(true));
//       - update RW 0x8B1962: the base update (RW 0x854DF7); with EffectDuration (data + 0x7C) and the object HIDDEN: now >= + 0x88 + EffectDuration unhides,
//         else the module sleeps EffectDuration frames (the base's sleep is dropped);
//     Callers of slot 0x5C: InvisibilityManager RW 0x81B3D3 (UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH, a HIDDEN object leaving STEALTH) and StealthUpdate RW
//     0x7760B7 (STEALTHED lost, RW 0x777849). Caller of slot 0x64: StealthUpdate's TAKING_DAMAGE (RW 0x77633D .. 0x776358: the body's last damage frame
//     before the hide frame is not "taking damage").
//   * InvisibilitySpecialPower (create RW 0x651631, constructor RW 0x8C6553 after SpecialPowerModule's RW 0x8973EE; special power interface RW 0xC73138;
//     data RW 0x651669 -> 0x8C6621: the SpecialPowerModule table + RW 0xC730E8: InvisibilityNugget + 0x7C (RW 0x81A667), BroadcastRadius + 0x144 (0.0),
//     ObjectFilter + 0x148 (RW 0x76392F, default ALL), Duration + 0x14C (RW 0x73A429, 0)). Interface slots:
//       - 0x28 doSpecialPower: the base's (RW 0x8980A3), no effect;
//       - 0x2C doSpecialPowerAtObject RW 0x8C65D2: the base's (RW 0x8980EF), then the nugget to the caster itself (RW 0x81C217, Duration frames);
//       - 0x30 doSpecialPowerAtLocation RW 0x8C66EB: the base's (RW 0x89816C; its answer is not tested), then every object ThePartitionManager finds within
//         BroadcastRadius of the location (RW 0xA39340: distance type 0, near to far) through the filters RW 0xC1D660 (not the caster), RW 0xC10E20 (alive:
//         Object + 0x458 bit 0 clear), RW 0xC0F374 (the same Object + 0x458 bit 3 as the caster: never set) and the ObjectFilter for the caster's
//         controlling player (RW 0xBE4CC8, flag 1) gets the nugget for Duration frames (RW 0x81C217).
// WHAT IS NOT PORTED (stop S-1043, reported per use): ShowPalantirTimer and the EffectDuration report to the player (the palantir timer, client); the hide's
// room test for the garrison enums 0x27 / 0x28 (no retail ToggleHidden ability uses them: counted, the hide does not happen).

#pragma once

#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <string>
#include <vector>

class ModuleFactory;

class ToggleHiddenSpecialAbilityUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	bool m_showPalantirTimer = false; // + 0xD0
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ToggleHiddenSpecialAbilityUpdate : public SpecialAbilityUpdate
{
public:
	ToggleHiddenSpecialAbilityUpdate(Thing *thing, const ToggleHiddenSpecialAbilityUpdateModuleData *data); // RW 0x8B184A
	UpdateSleepTime update() override; // RW 0x8B1962
	void unhide();                     // slot 0x5C, RW 0x8B19AC
	void hide();                       // slot 0x60, RW 0x8B1A70
	unsigned hideFrame() const { return m_hideFrame; } // slot 0x64, RW 0x69AE14
	unsigned hides() const { return m_hides; }
	unsigned unhides() const { return m_unhides; }
	void crc(StateHasher &h) const override;

	// RW 0x68BDA5 with the class name key: the object's first ToggleHiddenSpecialAbilityUpdate (null for none)
	static ToggleHiddenSpecialAbilityUpdate *of(const Object &obj);

protected:
	bool continuePreparation() override;  // RW 0x8B42F3
	void triggerAbilityEffect() override; // RW 0x8B1909

private:
	bool canHide() const; // the special power update interface's slot 0x20, RW 0x851B6E
	const ToggleHiddenSpecialAbilityUpdateModuleData *m_th;
	unsigned m_hideFrame = 0; // + 0x88
	unsigned m_hides = 0, m_unhides = 0;
};

class InvisibilitySpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	InvisibilityNugget m_nugget;                                               // + 0x7C
	float m_broadcastRadius = 0.0f;                                            // + 0x144
	ObjectFilter m_objectFilter = ObjectFilter::all(KindOfMaskType{});          // + 0x148
	unsigned m_duration = 0;                                                   // + 0x14C (frames)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class InvisibilitySpecialPower : public SpecialPowerModule
{
public:
	InvisibilitySpecialPower(Thing *thing, const InvisibilitySpecialPowerModuleData *data); // RW 0x8C6553
	void doSpecialPowerAtObject(Object *target, unsigned options) override;       // RW 0x8C65D2
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override; // RW 0x8C66EB
	const std::vector<ObjectID> &lastAffected() const { return m_lastAffected; } // the objects of the last cast (tests)
	unsigned long long nuggetsGiven() const { return m_given; }
	void crc(StateHasher &h) const override;

private:
	const InvisibilitySpecialPowerModuleData *m_data;
	std::vector<ObjectID> m_lastAffected;
	unsigned long long m_given = 0;
};

namespace StealthAbilityModules
{
void registerAll(ModuleFactory &modules);
std::vector<std::string> stopLines();
}
