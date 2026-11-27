// OpenBFME. GPL-3.0. MODGAP-2, RotWK 2.01 and BFME2 1.06.
// FlingPassengerSpecialAbilityUpdate: RW 0x89875A, BFME2 0x894FCC
// (decomp FlingPassengerSpecialAbilityUpdate.cpp, Rva00494FCC).
// Local 171: RW 0x64DB4B (FlingPassengerSpecialAbilityUpdate newData)
#pragma once

#include "GameLogic/Module/SpecialAbilityModules.h"

class ModuleFactory;
class WeaponTemplate;

// Local 173: RW 0x89870E (FlingPassengerSpecialAbilityUpdateModuleData ctor)
class FlingPassengerSpecialAbilityUpdateModuleData : public SpecialAbilityUpdateModuleData {
public:
    Coord3D m_flingVelocity{}; // RW table 0xC0556C; BFME2 0xBEF20C
    std::string m_landingWarhead;
    static void buildFieldParse(MultiIniFieldParse &p);
};

// RW vtable 0x00C652D4; name/key RW 0x008986A3 / 0x008986C9; s1 no-op RW 0x008E38D8
// RW 0x008986A9 (dtor -> RW 0x00854D9A)
class FlingPassengerSpecialAbilityUpdate : public SpecialAbilityUpdate {
public:
    OBFME_MODULE_XFER(FlingPassengerSpecialAbilityUpdate)
    FlingPassengerSpecialAbilityUpdate(Thing *thing, const FlingPassengerSpecialAbilityUpdateModuleData *data);
    void crc(StateHasher &h) const override;
    static void registerClass(ModuleFactory &factory);
    static const char *stopLine();
protected:
    void triggerAbilityEffect() override;
private:
    const WeaponTemplate *m_warhead;
};
