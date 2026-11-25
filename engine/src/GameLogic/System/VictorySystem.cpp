#include "GameLogic/System/VictorySystem.h"
#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Xfer.h"
#include "GameLogic/SimMath.h"
#include <algorithm>
VictorySystem *TheVictorySystem = nullptr;
void VictorySystem::reset() {
    m_system = VictorySystemData();
    m_factions.clear();
}
void VictorySystem::parseVictorySystemData(INI *ini) {
    ini->getNextTokenOrNull();
    const FieldParse table[] = {
        { "CellSize", INI::parseReal, nullptr, (int)offsetof(VictorySystemData, cellSize) },
        { "ScalePerLogicFrame", INI::parseReal, nullptr, (int)offsetof(VictorySystemData, scalePerLogicFrame) },
        { "SubtractPerLogicFrame", INI::parseReal, nullptr, (int)offsetof(VictorySystemData, subtractPerLogicFrame) },
        { "CellBonusRadius", INI::parseReal, nullptr, (int)offsetof(VictorySystemData, cellBonusRadius) },
        { nullptr, nullptr, nullptr, 0 }
    };
    ini->initFromINI(&m_system, table);
}
void VictorySystem::parseFactionVictoryData(INI *ini) {
    std::string name = ini->getNextToken();
    if (AsciiStringUtil::compareNoCase(name, "neutral") == 0) name.clear();
    FactionVictoryData *rec = nullptr;
    for (auto &f : m_factions) if (AsciiStringUtil::compareNoCase(f.name, name) == 0) { rec = &f; break; }
    if (!rec) { m_factions.emplace_back(); rec = &m_factions.back(); rec->name = name; }
    const FieldParse table[] = {
        { "AllyDeathScaleFactor", INI::parseReal, nullptr, (int)offsetof(FactionVictoryData, allyDeathScaleFactor) },
        { "EnemyKillScaleFactor", INI::parseReal, nullptr, (int)offsetof(FactionVictoryData, enemyKillScaleFactor) },
        { "VictoryThreshold", INI::parseReal, nullptr, (int)offsetof(FactionVictoryData, victoryThreshold) },
        { "MapToCellVictoryRatio", INI::parseReal, nullptr, (int)offsetof(FactionVictoryData, mapToCellVictoryRatio) },
        { "MajorUnitValue", INI::parseReal, nullptr, (int)offsetof(FactionVictoryData, majorUnitValue) },
        { nullptr, nullptr, nullptr, 0 }
    };
    ini->initFromINI(rec, table);
}
const FactionVictoryData *VictorySystem::findFaction(const std::string &name) const {
    for (auto &f : m_factions) if (AsciiStringUtil::compareNoCase(f.name, name)==0) return &f;
    return nullptr;
}
bool VictorySystem::isMapVictorious(const std::string &factionName, int kills, int deaths) const {
    const FactionVictoryData *rec = findFaction(factionName);
    if (!rec) return false;
    float score = SimMath::subf32(SimMath::mulf32(SimMath::sseFromInt32(kills), rec->enemyKillScaleFactor), SimMath::mulf32(SimMath::sseFromInt32(deaths), rec->allyDeathScaleFactor));
    return score > rec->victoryThreshold;
}
float VictorySystem::cellThreshold(const std::string &factionName) const {
    const FactionVictoryData *rec = findFaction(factionName);
    if (!rec) return 0.0f;
    return SimMath::mulf32(rec->victoryThreshold, rec->mapToCellVictoryRatio);
}
void VictorySystem::crc(StateHasher &h) const {
    h.addFloat(m_system.cellSize);
    h.addFloat(m_system.scalePerLogicFrame);
    h.addFloat(m_system.subtractPerLogicFrame);
    h.addFloat(m_system.cellBonusRadius);
    h.addU32((std::uint32_t)m_factions.size());
    for (auto &f : m_factions) {
        h.addString(f.name);
        h.addFloat(f.allyDeathScaleFactor);
        h.addFloat(f.enemyKillScaleFactor);
        h.addFloat(f.victoryThreshold);
        h.addFloat(f.mapToCellVictoryRatio);
        h.addFloat(f.majorUnitValue);
    }
}
void VictorySystem::xfer(Xfer &x) {
    XferVersion v{1,1}; x.xferVersion(v);
    x.xferReal(&m_system.cellSize);
    x.xferReal(&m_system.scalePerLogicFrame);
    x.xferReal(&m_system.subtractPerLogicFrame);
    x.xferReal(&m_system.cellBonusRadius);
    x.xferVectorSize(m_factions, 32);
    for (auto &d : m_factions){
        x.xferAsciiString(&d.name);
        x.xferReal(&d.allyDeathScaleFactor);
        x.xferReal(&d.enemyKillScaleFactor);
        x.xferReal(&d.victoryThreshold);
        x.xferReal(&d.mapToCellVictoryRatio);
        x.xferReal(&d.majorUnitValue);
    }
}
void VictorySystem::registerBlocks(INIBlockRegistry &registry, VictorySystem &vs) {
    registry.registerBlock("VictorySystemData", [&vs](INI *ini){ vs.parseVictorySystemData(ini); });
    registry.registerBlock("FactionVictoryData", [&vs](INI *ini){ vs.parseFactionVictoryData(ini); });
}
