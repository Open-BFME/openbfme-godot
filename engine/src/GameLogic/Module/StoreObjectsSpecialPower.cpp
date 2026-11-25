// OpenBFME. GPL-3.0. See StoreObjectsSpecialPower.h.
#include "GameLogic/Module/StoreObjectsSpecialPower.h"

#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>

namespace {
const ObjectFilter &filterOrDefault(const ObjectFilter *filter)
{
    static const ObjectFilter kEmpty{};
    return filter ? *filter : kEmpty;
}

const FieldParse kFields[] = {
    { "Radius", INI::parseReal, nullptr, (int)offsetof(StoreObjectsSpecialPowerModuleData, radius) },
    { nullptr, nullptr, nullptr, 0 }
};
}

// RW 0x8D1D76 (StoreObjectsSpecialPower::buildFieldParse)
void StoreObjectsSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p) {
    SpecialAbilityUpdateModuleData::buildFieldParse(p);
    p.add(kFields);
}

// RW 0x008D1EA7
void StoreObjectsSpecialPower::triggerAbilityEffect() {
    SpecialAbilityUpdate::triggerAbilityEffect();
    Object *source = getObject();
    GameLogic &logic = source->logic();
    m_objects.clear();
    const SpecialPowerModule *power = mySpecialPowerModule();
    const SpecialPowerTemplate *powerTemplate = power ? power->getSpecialPowerTemplate() : nullptr;
    if (!powerTemplate) throw std::logic_error("StoreObjectsSpecialPower: special power module or template is missing");
    // S-523 (closed, fork issue 555): an unset filter is RW's default — rule NONE, both masks empty (RW 0x763D11 over the all-zero KindOf mask RW 0xDE49E4): it allows nothing, so the search below finds nothing, as in retail
    const ObjectFilter &allowed = filterOrDefault(powerTemplate->m_objectFilter.get());
    const int horde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
    if (horde < 0) throw std::logic_error("StoreObjectsSpecialPower: HORDE kind is unavailable");
    PartitionFilterFn filter([&](Object &candidate) {
        return &candidate != source && !candidate.isEffectivelyDead()
            && !candidate.testStatus(38)
            && candidate.isOffMap() == source->isOffMap() // RW 0x008D1F4C: PartitionFilterSameMapStatus (vtable RW 0x00C0F374, slot +4 RW 0x00660E93: Object +0x458 bit 3)
            && ObjectFilterMatch::allows(logic, allowed, candidate, source->getControllingPlayer());
    });
    const auto hits = logic.partition().iterateObjectsInRange(m_targetPos, m_store->radius,
        FROM_CENTER_2D, { &filter }, ITER_SORTED_NEAR_TO_FAR);
    for (const PartitionHit &hit : hits) {
        Object *candidate = hit.object;
        Object *producer = logic.findObjectByID(candidate->getProducerID()); // RW Object +0x78
        if ((producer && producer->isKindOf((unsigned)horde)) || candidate->testStatus(3)) continue;
        m_objects.push_back(candidate->getID());
    }
}

void StoreObjectsSpecialPower::crc(StateHasher &h) const {
    SpecialAbilityUpdate::crc(h);
    h.addU32((std::uint32_t)m_objects.size());
    for (ObjectID id : m_objects) h.addU32(id);
}

void StoreObjectsSpecialPower::xfer(Xfer &x) {
    SnapshotXfer::version(x, 1, 1);
    SpecialAbilityUpdate::xfer(x);
    // Native OpenBFME snapshots retain the ordered IDs needed by a later teleport.
    SnapshotXfer::ids(x, m_objects);
}

void StoreObjectsSpecialPower::registerClass(ModuleFactory &factory) {
    factory.bindTypedData<StoreObjectsSpecialPowerModuleData>("StoreObjectsSpecialPower", MODULETYPE_BEHAVIOR);
    factory.bindModuleProc("StoreObjectsSpecialPower", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
        auto *typed = dynamic_cast<const StoreObjectsSpecialPowerModuleData *>(data);
        if (!typed) throw std::logic_error("StoreObjectsSpecialPower: the module data is not typed");
        return std::make_unique<StoreObjectsSpecialPower>(thing, typed);
    });
}
