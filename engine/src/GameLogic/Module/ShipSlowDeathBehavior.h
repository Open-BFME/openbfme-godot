// OpenBFME. GPL-3.0. MODGAP-2; both editions.
// RW 0x00861AA9 / RW 0x00861B45 / RW 0x00861D51, BFME2 0x85EA66 / 0x85EB02 (ShipSlowDeathBehavior.cpp).
// RW binary provenance: S-001. Native save format: S-4190.
#pragma once
#include "GameLogic/Module/CombatModules.h"

class ModuleFactory;
// RW 0x00861A37 (getModuleName), RW 0x00861A3D (getModuleNameKey), RW 0x00861D72 (scalar deleting dtor -> RW 0x00861A82)
class ShipSlowDeathBehavior : public SlowDeathBehavior {
public:
    OBFME_MODULE_XFER(ShipSlowDeathBehavior)
    ShipSlowDeathBehavior(Thing *thing, const SlowDeathBehaviorModuleData *data) : SlowDeathBehavior(thing, data) {}
    UpdateSleepTime update() override;
    void crc(StateHasher &h) const override;
    static void registerClass(ModuleFactory &factory);
protected:
    void beginSlowDeath(const DieModuleInterface::Event &event) override;
private:
    float m_pitchRate = 0.0f;
    float m_rollRate = 0.0f;
};
