// OpenBFME. GPL-3.0. See ShipSlowDeathBehavior.h.
#include "GameLogic/Module/ShipSlowDeathBehavior.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include <stdexcept>

// RW 0x00861AA9
void ShipSlowDeathBehavior::beginSlowDeath(const DieModuleInterface::Event &event) {
    SlowDeathBehavior::beginSlowDeath(event);
    const float duration = SimMath::fstpDword(SimMath::fildU32(data()->m_destructionDelay));
    const float rate = SimMath::mulf32(SimMath::divf32(3.14159274f, duration), 0.5f);
    m_pitchRate = rate;
    m_rollRate = rate;
    auto &rng = getObject()->logic().random();
    if (rng.getValue(0, 1, "ShipSlowDeathBehavior.cpp", 64) == 1) m_pitchRate = SimMath::subf32(0.0f, m_pitchRate);
    if (rng.getValue(0, 1, "ShipSlowDeathBehavior.cpp", 70) == 1) m_rollRate = SimMath::subf32(0.0f, m_rollRate);
}

// RW 0x00861B45
UpdateSleepTime ShipSlowDeathBehavior::update() {
    const UpdateSleepTime sleep = SlowDeathBehavior::update();
    Object *obj = getObject();
    if (!obj) return sleep;
    const float *old = obj->getBasis();
    float b[9];
    for (int i = 0; i < 9; ++i) b[i] = old[i];
    const float cx = SimMath::cosf32(m_pitchRate), sx = SimMath::sinf32(m_pitchRate);
    const double sxWide = SimMath::sind(m_pitchRate);
    for (int i = 0; i < 3; ++i) {
        const float y = b[3 + i], z = b[6 + i];
        b[3 + i] = i == 0
            ? SimMath::subf32(SimMath::mulf32(cx, y), SimMath::mulf32(sx, z))
            : SimMath::fstpDword(SimMath::pc24SubW(SimMath::pc24MulW((double)cx, (double)y), SimMath::pc24MulW(sxWide, (double)z)));
        b[6 + i] = SimMath::addf32(SimMath::mulf32(cx, z), SimMath::mulf32(sx, y));
    }
    const float cy = SimMath::cosf32(m_rollRate), sy = SimMath::sinf32(m_rollRate);
    const double syWide = SimMath::sind(m_rollRate);
    for (int i = 0; i < 3; ++i) {
        const float x = b[i], z = b[6 + i];
        b[i] = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW((double)cy, (double)x), SimMath::pc24MulW(syWide, (double)z)));
        b[6 + i] = SimMath::subf32(SimMath::mulf32(cy, z), SimMath::mulf32(sy, x));
    }
    const Coord3D pos = *obj->getPosition();
    obj->setTransform(&pos, b);
    return sleep;
}

void ShipSlowDeathBehavior::crc(StateHasher &h) const {
    SlowDeathBehavior::crc(h);
    h.addFloat(m_pitchRate);
    h.addFloat(m_rollRate);
}
// RW 0x00861D51 (OpenBFME native snapshot order, S-4190)
void ShipSlowDeathBehavior::xfer(Xfer &x) {
    SnapshotXfer::version(x, 1, 1);
    SlowDeathBehavior::xfer(x);
    // OpenBFME native saves retain the rates so a sinking ship resumes deterministically.
    x.xferReal(&m_pitchRate);
    x.xferReal(&m_rollRate);
}
void ShipSlowDeathBehavior::registerClass(ModuleFactory &factory) {
    // RW 0x8619CD (ShipSlowDeathBehavior::buildFieldParse)
    factory.bindTypedData<SlowDeathBehaviorModuleData>("ShipSlowDeathBehavior", MODULETYPE_BEHAVIOR);
    factory.bindModuleProc("ShipSlowDeathBehavior", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
        auto *typed = dynamic_cast<const SlowDeathBehaviorModuleData *>(data);
        if (!typed) throw std::logic_error("ShipSlowDeathBehavior: the module data is not typed");
        return std::make_unique<ShipSlowDeathBehavior>(thing, typed);
    });
}
