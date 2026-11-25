// OpenBFME. GPL-3.0. MODGAP-2 (finished by lane MODGAP-1), RotWK 2.01; BFME2 ModuleRva004CD4DC.cpp names the shape.
// TeleportToCasterSpecialPower: units teleport around a point, those a StoreObjectsSpecialPower of the object kept, else the allies around the target.
// TARGET FACTS (RotWK game.dat, caveat S-001): create RW 0x652DF8, 0x88 bytes, constructor RW 0x8D1802 over SpecialAbilityUpdate RW 0x851D42; table RW 0xC76710: Radius + 0xD0,
// MinDestinationRadius + 0xD4, MaxDestinationRadius + 0xD8, TriggerFX + 0xDC, TargetFX + 0xE0.
//   * trigger RW 0x8D1AEB: the base's RW 0x853EDF. The first StoreObjectsSpecialPower with a list (RW 0x8D18BA): the list goes to the ability's target point and the list clears (RW 0x8D1E93).
//     Else the objects within Radius of the target point (2D, RW 0xA39340): no status 0x26 (RW 0x6FF651), the power's object filter (RW 0xBE4CC8), same map status (RW 0x008D1BAE -> vtable RW 0x00C0F374, slot +4
//     RW 0x00660E93: Object +0x458 bit 3 matches caster), allies (RW 0xC11DC0, 4), not STRUCTURE and MOVE_ONLY together (RW 0xC309C0: RW 0x70C4FE negated), alive, not the object; a member whose producer is a HORDE, status 3 or
//     status 2 is left out: these go to the object's position. TriggerFX plays at the target point (RW 0x494615).
//   * teleport RW 0x8D1979: for each id the angle starts at 0 and adds 2 PI / count: TargetFX on the object (RW 0x8D193A: its drawable's slot 0x1EC), findPositionAround(point, Min, Max, angle)
//     (RW 0x68592D): found: the object faces away from the point (atan2 of found - point, RW 0x441BD2), else it stands on the point keeping its facing; the AI idles (CMD_FROM_AI, RW 0x5E821A);
//     teleportTo(found, keepAI 0) (RW 0x696E63); TargetFX again.
// INFERENCE: TargetFX is logged as an object FX (the drawable's slot 0x1EC was not read); TriggerFX as an object FX of the caster (stop S-6003).
#pragma once

#include "GameLogic/Module/SpecialAbilityModules.h"

#include <string>
#include <vector>

class ModuleFactory;

class TeleportToCasterSpecialPowerModuleData : public SpecialAbilityUpdateModuleData {
public:
    float m_radius = 0.0f;               // + 0xD0
    float m_minDestinationRadius = 0.0f; // + 0xD4
    float m_maxDestinationRadius = 0.0f; // + 0xD8
    std::string m_triggerFX;             // + 0xDC
    std::string m_targetFX;              // + 0xE0
    static void buildFieldParse(MultiIniFieldParse &p);
};

// Local 87: RW 0x008D1833 (getModuleName), RW 0x008D1878 (getModuleNameKey), RW 0x008D189E (dtor).
// RW 0x008D187E (dtor -> RW 0x00854D9A)
class TeleportToCasterSpecialPower : public SpecialAbilityUpdate {
public:
    OBFME_MODULE_XFER(TeleportToCasterSpecialPower)
    TeleportToCasterSpecialPower(Thing *thing, const TeleportToCasterSpecialPowerModuleData *data)
        : SpecialAbilityUpdate(thing, data), m_teleport(data) {}
    static void registerClass(ModuleFactory &factory);
    static const char *stopLine();
protected:
    void triggerAbilityEffect() override; // RW 0x8D1AEB
private:
    void teleportList(const std::vector<ObjectID> &ids, const Coord3D &point); // RW 0x8D1979
    const TeleportToCasterSpecialPowerModuleData *m_teleport;
};
