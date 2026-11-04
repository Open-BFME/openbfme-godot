// OpenBFME. GPL-3.0. See docs/CRATE_PICKUPS.md for target/donor evidence and limits.
// VeterancyCrateCollide (the experience shields): BFME2 Open-BFME-2 Code/GameEngine/Source/GameLogic/Object/Collide/CrateCollide/
// {VeterancyCrateCollide,VeterancyCrateCollideModuleDataCtor}.cpp over the CrateCollide base of GameLogic/Module/SalvageCrateCollide.h (RW 0x8C080B).
// Editions: shared BFME2/RotWK simulation; BFME1 parity remains S-5017.
// RotWK (S-001): create 0x650FFF -> ctor 0x8C0F7B, data ctor 0x6558B7, primary vtable 0xC71554.
// Validity RW 0x8C1039 / BFME2 RVA 0x4BCEF0; execute RW 0x8C10DB / BFME2 RVA 0x4BCFF0.
// Levels RW 0x8C1025 / BFME2 RVA 0x4BCEDC returns zero for AddsOwnerVeterancy, otherwise one.
// Target field table RW 0xC05EB8: EffectRange (unsigned), AddsOwnerVeterancy / IsPilot (bool), AffectsUpToLevel (int).
#pragma once

#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/SalvageCrateCollide.h"

class VeterancyCrateCollideModuleData : public CrateCollideModuleData
{
public:
	unsigned m_effectRange = 0;
	bool m_addsOwnerVeterancy = false;
	bool m_isPilot = false;
	int m_affectsUpToLevel = 10;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class VeterancyCrateCollide : public CrateCollide
{
public:
	OBFME_MODULE_XFER(VeterancyCrateCollide)
	VeterancyCrateCollide(Thing *thing, const VeterancyCrateCollideModuleData *data) : CrateCollide(thing, data), m_veterancy(data) {}

	bool isValidToExecute(const Object *other) const override;
	bool executeCrateBehavior(Object &other) override;

	static const char *limitationsStop();

private:
	const VeterancyCrateCollideModuleData *m_veterancy;
};
