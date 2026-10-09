// OpenBFME unit tests. GPL-3.0.
// GameData excerpt of the retail economy values, shared by the economy and production test fixtures (lane ECON-1).

#pragma once

namespace econtest
{
// the retail values of data\ini\gamedata.ini that EconomySettings reads (RotWK 2.01: INI.big and _patch201ini.big agree), as the retail file writes them
const char kGameData[] =
	"GameData\n"
	"  PartitionCellSize = 40.0\n"
	"  TerrainResourceCellSize = 20.0\n"
	"  GoodCommandPointLimit = 300\n"
	"  EvilCommandPointLimit = 600\n"
	"  PowerLimit = 60\n"
	"  ResourceMultiplierLimit = 5.0\n"
	"  ResourceBonusMultiplier = 10.0\n"
	"  GoodCommandPoints = 100 150\n"
	"  EvilCommandPoints = 300 350\n"
	"  GoodCommandPointsBonus = 20\n"
	"  EvilCommandPointsBonus = 50\n"
	"  GoodCommandPointsAI = 600 650\n"
	"  EvilCommandPointsAI = 600 650\n"
	"  GoodCommandPointsMP2 = 100 1000\n"
	"  EvilCommandPointsMP2 = 100 1000\n"
	"  GoodCommandPointsMP3 = 100 875\n"
	"  EvilCommandPointsMP3 = 100 875\n"
	"  GoodCommandPointsMP4 = 100 750\n"
	"  EvilCommandPointsMP4 = 100 750\n"
	"  GoodCommandPointsMP5 = 100 675\n"
	"  EvilCommandPointsMP5 = 100 675\n"
	"  GoodCommandPointsMP6 = 100 625\n"
	"  EvilCommandPointsMP6 = 100 625\n"
	"  GoodCommandPointsMP7 = 100 575\n"
	"  EvilCommandPointsMP7 = 100 575\n"
	"  GoodCommandPointsMP8 = 100 500\n"
	"  EvilCommandPointsMP8 = 100 500\n"
	"  MultiPlayMoneyMult = MP1:1.0 MP2:1.0 MP3:1.0 MP4:1.0 MP5:1.0 MP6:1.0 MP7:1.0 MP8:1.0\n"
	"End\n";
} // namespace econtest
