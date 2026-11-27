// OpenBFME. GPL-3.0. See FlingPassengerSpecialAbilityUpdate.h.
#include "GameLogic/Module/FlingPassengerSpecialAbilityUpdate.h"
#include "Common/AsciiString.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Weapon.h"
#include <cstddef>
#include <stdexcept>

namespace {
void parseWeaponName(INI *ini, void *, void *store, const void *) {
    const std::string name = ini->getNextToken();
    *static_cast<std::string *>(store) = AsciiStringUtil::compareNoCase(name, "None") == 0 ? std::string() : name;
}
#define FLING_OFF(m) (int)offsetof(FlingPassengerSpecialAbilityUpdateModuleData, m)
const FieldParse kFields[] = {
    { "FlingPassengerVelocity", INI::parseCoord3D, nullptr, FLING_OFF(m_flingVelocity) },
    { "FlingPassengerLandingWarhead", parseWeaponName, nullptr, FLING_OFF(m_landingWarhead) },
    { nullptr, nullptr, nullptr, 0 }
};
#undef FLING_OFF
}

// RW 0x64DB10 (friend_newModuleInstance)
// RW 0x64DAF5 (FlingPassengerSpecialAbilityUpdate::buildFieldParse)
void FlingPassengerSpecialAbilityUpdateModuleData::buildFieldParse(MultiIniFieldParse &p) {
    SpecialAbilityUpdateModuleData::buildFieldParse(p);
    p.add(kFields);
}
// RW 0x008986C9 (getModuleNameKey)
// Local 173: RW 0x898672 (~FlingPassengerSpecialAbilityUpdate)
FlingPassengerSpecialAbilityUpdate::FlingPassengerSpecialAbilityUpdate(Thing *thing, const FlingPassengerSpecialAbilityUpdateModuleData *data)
    : SpecialAbilityUpdate(thing, data),
      m_warhead(TheWeaponStore && !data->m_landingWarhead.empty() ? TheWeaponStore->findWeaponTemplate(data->m_landingWarhead) : nullptr) {}

void FlingPassengerSpecialAbilityUpdate::triggerAbilityEffect() {
    SpecialAbilityUpdate::triggerAbilityEffect(); // RW 0x853EDF; BFME2 0x85108D
    Object *owner = getObject();
    ContainModuleInterface *contain = owner->getContain();
    if (!contain) throw std::logic_error("FlingPassengerSpecialAbilityUpdate: no contain interface");
    const auto *items = contain->getContainedItemsList();
    if (!items || items->empty()) return;
    Object *rider = items->front();
    contain->removeFromContain(rider); // RW 0x866367, slot 0x13C with null filter
    if (m_warhead) ObjectWeapons::createAndFireTempWeaponAt(m_warhead, owner, *rider);
    if (rider->isEffectivelyDead()) {
        rider->setModelConditionState(62, true);
        rider->setModelConditionState(122, true);
    } else if (PhysicsBehavior *physics = PhysicsBehavior::find(*rider)) {
        physics->standUp(); // RW 0x792AFF; BFME2 0x7906BF
    }
}
const char *FlingPassengerSpecialAbilityUpdate::stopLine() {
    return "[S-6001] FlingPassengerSpecialAbilityUpdate: contain slots 0x114 / 0x13C are not verified outside OpenContain (RW 0x89876A; BFME2 0x894FDC)";
}
void FlingPassengerSpecialAbilityUpdate::crc(StateHasher &h) const { SpecialAbilityUpdate::crc(h); }
// RW 0x008C6E67 (slot 3): version 1, delegates to SpecialAbilityUpdate::xfer (RW 0x00852782).
// Deleting dtor RW 0x0089873E -> 0x008986A9 chains to ~SpecialAbilityUpdate (RW 0x00854D9A).
void FlingPassengerSpecialAbilityUpdate::xfer(Xfer &x) { SnapshotXfer::version(x, 1, 1); SpecialAbilityUpdate::xfer(x); }
void FlingPassengerSpecialAbilityUpdate::registerClass(ModuleFactory &factory) {
    factory.bindTypedData<FlingPassengerSpecialAbilityUpdateModuleData>("FlingPassengerSpecialAbilityUpdate", MODULETYPE_BEHAVIOR);
    factory.bindModuleProc("FlingPassengerSpecialAbilityUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
        auto *typed = dynamic_cast<const FlingPassengerSpecialAbilityUpdateModuleData *>(data);
        if (!typed) throw std::logic_error("FlingPassengerSpecialAbilityUpdate: the module data is not typed");
        return std::make_unique<FlingPassengerSpecialAbilityUpdate>(thing, typed);
    });
}
