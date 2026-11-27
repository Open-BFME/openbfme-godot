// OpenBFME. GPL-3.0.
// VictorySystem: the War of the Ring large-battle grid victory subsystem.
// RW 0xDE897C TheVictorySystem (ctor RW 0x4054E1, initSubsystem 0x63C301).
// Field tables: VictorySystemFieldParse 0xC4EBF8 (CellSize +0x10 etc) and
// FactionVictoryFieldParse 0xC4EBF8/0xC38760 (5 reals). See docs/ennorath/PORT72_RW_ANALYSIS.md.
#pragma once
#include "Common/INI.h"
#include "Common/StateHash.h"
#include <string>
#include <vector>
#include <map>

struct VictorySystemData
{
    float cellSize = 500.0f;              // RW +0x10 VictorySystemData.CellSize
    float scalePerLogicFrame = 0.0f;      // RW +0x18 ScalePerLogicFrame
    float subtractPerLogicFrame = 0.0f;   // RW +0x1C SubtractPerLogicFrame
    float cellBonusRadius = 1.0f;         // RW +0x24 CellBonusRadius
};

struct FactionVictoryData
{
    std::string name;
    float allyDeathScaleFactor = 1.0f;    // +0x04
    float enemyKillScaleFactor = 1.0f;    // +0x08
    float victoryThreshold = 500.0f;      // +0x10 (donor BFME2 default)
    float mapToCellVictoryRatio = 1.0f;   // +0x0C
    float majorUnitValue = 10.0f;         // +0x14
};

class VictorySystem
{
public:
    VictorySystem() = default;
    void reset();
    // RW 0x61584E parsers: VictorySystemData and FactionVictoryData
    void parseVictorySystemData(INI *ini); // RW 0x615A68 for Faction, RW 0x61584E for System
    void parseFactionVictoryData(INI *ini);
    const VictorySystemData &systemData() const { return m_system; }
    const std::vector<FactionVictoryData> &factionData() const { return m_factions; }
    const FactionVictoryData *findFaction(const std::string &name) const;
    // Logic driven from it (RW 0x806909 mask): kills*EnemyKill - deaths*AllyDeath > Threshold
    bool isMapVictorious(const std::string &factionName, int kills, int deaths) const;
    float cellThreshold(const std::string &factionName) const;
    void crc(StateHasher &h) const;
    void xfer(class Xfer &x);
    static void registerBlocks(INIBlockRegistry &registry, VictorySystem &vs);
private :
    VictorySystemData m_system;
    std::vector<FactionVictoryData> m_factions;
};
extern VictorySystem *TheVictorySystem;
