// OpenBFME. GPL-3.0. MODGAP-2, both RotWK 2.01 and BFME2 1.06.
// RW 0x8D1EA7; BFME2 0x8CDA49 (StoreObjectsSpecialPower::triggerAbilityEffect
// in Open-BFME-2 MissileLauncherBuildingUpdate.cpp). RW binary caveat S-001.
// Local 171: RW 0x652EC2 (StoreObjectsSpecialPower newData)
#pragma once

#include "GameLogic/Module/SpecialAbilityModules.h"

class ModuleFactory;

class StoreObjectsSpecialPowerModuleData : public SpecialAbilityUpdateModuleData {
public:
    float radius = 0.0f; // RW data +0xD0; BFME2 +0xC8
    static void buildFieldParse(MultiIniFieldParse &p);
};

// Local 87: RW 0x008D1DD3 (getModuleName), RW 0x008D1DD9 (getModuleNameKey), RW 0x008D1E77 (data dtor).
// RW 0x008D1E1E (dtor -> RW 0x00854D9A)
// RW 0x00652E87 (StoreObjectsSpecialPower::friend_newModuleInstance)
class StoreObjectsSpecialPower : public SpecialAbilityUpdate {
public:
    OBFME_MODULE_XFER(StoreObjectsSpecialPower)
    StoreObjectsSpecialPower(Thing *thing, const StoreObjectsSpecialPowerModuleData *data)
        : SpecialAbilityUpdate(thing, data), m_store(data) {}
    void crc(StateHasher &h) const override;
    static void registerClass(ModuleFactory &factory);
    const std::vector<ObjectID> &storedObjects() const { return m_objects; }
    void clearStoredObjects() { m_objects.clear(); }
protected:
    void triggerAbilityEffect() override;
private:
    const StoreObjectsSpecialPowerModuleData *m_store;
    std::vector<ObjectID> m_objects;
};
