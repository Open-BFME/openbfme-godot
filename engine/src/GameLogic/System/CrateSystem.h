// OpenBFME. GPL-3.0.
// CrateSystem: Global INI block CrateData (RW 0x75EC76) into TheCrateSystem 0xDE78A0.
// CrateTemplate 0x44 bytes, table RW 0xC2C928 (CreationChance +0x14 etc).
// Only consumer is CreateCrateDie::onDie RW 0x888A24.
// DONOR BFME2 CrateSystem.cpp, CrateTemplate.cpp, CreateCrateDie.cpp
#pragma once
#include "Common/INI.h"
#include "Common/StateHash.h"
#include <string>
#include <vector>

struct CrateTemplate
{
    std::string name;
    float creationChance = 0.0f;                 // +0x14 parseReal
    int veterancyLevel = 5;                      // +0x18 parseIndexList (LEVEL_INVALID)
    std::string killedByType;                  // +0x1C KindOf mask stringified (simplified)
    std::vector<std::pair<std::string,float>> crates; // +0x3C list of CrateObject (name,chance)
    int killerScience = -1;                      // +0x38 parseScience
    bool ownedByMaker = false;                  // +0x40 parseBool
};

class CrateSystem
{
public:
    void reset();
    void parseCrateData(INI *ini); // RW 0x75EC76
    const CrateTemplate *findTemplate(const std::string &name) const;
    const std::vector<CrateTemplate> &templates() const { return m_templates; }
    // Logic: try to create crate on victim death, returns chosen crate name or "".
    // RW 0x8886B5 testCreationChance, RW 0x8887D6 createCrate random pick
    std::string rollCrate(const std::string &crateDataName, float random01) const;
    bool testCreationChance(const std::string &crateDataName, float draw) const;
    bool testKilledByType(const std::string &crateDataName, const std::string &killerKind) const;
    void crc(StateHasher &h) const;
    void xfer(class Xfer &x);
    static void registerBlocks(INIBlockRegistry &registry, CrateSystem &cs);
private:
    std::vector<CrateTemplate> m_templates;
};
extern CrateSystem *TheCrateSystem;
