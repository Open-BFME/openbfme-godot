// OpenBFME. GPL-3.0. MODGAP-2; both RotWK 2.01 and BFME2 1.06.
// RW 0x8B6E38 / 0x8B6D10, BFME2 0x8B2D28 / 0x8B2C00
// (Open-BFME-2 ReplaceObjectUpdate.cpp, rows 0x004B2D28 / 0x004B2C00).
// Scatter's AI command 0x2E remains S-6002. RW binary caveat S-001.
// Local 171: RW 0x64FA84 (ReplaceObjectUpdate newData)
#pragma once
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/ObjectFilter.h"
#include <string>
#include <vector>

class ModuleFactory;
struct ReplaceObjectEntry {
    ObjectFilter targetFilter = ObjectFilter::parserDefault();
    std::vector<std::string> replacements;
};
class ReplaceObjectUpdateModuleData : public SpecialAbilityUpdateModuleData {
public:
    std::vector<ReplaceObjectEntry> entries;
    float radius = 0.0f;
    std::string replaceFX;
    bool scatter = false;
    static void buildFieldParse(MultiIniFieldParse &p);
};
class ReplaceObjectUpdate : public SpecialAbilityUpdate {
public:
    OBFME_MODULE_XFER(ReplaceObjectUpdate)
    ReplaceObjectUpdate(Thing *thing, const ReplaceObjectUpdateModuleData *data) : SpecialAbilityUpdate(thing, data), m_replace(data) {}
    void crc(StateHasher &h) const override;
    static void registerClass(ModuleFactory &factory);
    static const char *scatterStop();
protected:
    void triggerAbilityEffect() override;
private:
    const ReplaceObjectUpdateModuleData *m_replace;
};
