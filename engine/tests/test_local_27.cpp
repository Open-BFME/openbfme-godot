// OpenBFME unit tests: LOCAL-27, the roads.ini Bridge block (TerrainRoadType's Bridge field table and its readers). GPL-3.0.
//
// RotWK 2.01 addresses (game.dat):
//   - the Bridge block parse RW 0x5DA647: findBridge RW 0x6DC620 by name; a known bridge is parsed again in place, a road of that name is
//     INIException 3 "Redefining road '%s' as a bridge!" (RW 0xBF068C), otherwise newBridge RW 0x6DC794; then initFromINI with RW 0xC19DC0
//   - the field table RW 0xC19DC0 (22 rows, offsets in GameClient/TerrainRoads.h)
//   - newBridge RW 0x6DC794: the copy from an earlier Bridge DefaultBridge (texture +0x38, scale +0x1C, the seven names +0x34 .. +0x50,
//     +0x144, +0x148, the sounds +0x64 / +0xD4 per body state, and DefaultBridge's damage OCL (getter RW 0x6DBE3A, +0x74) into the
//     damage OCL / FX and repaired OCL / FX slots, setters RW 0x6DC033 / 0x6DC071 / 0x6DC0B2 / 0x6DC0F2)
//   - TransitionToOCL RW 0x6DC132, TransitionToFX RW 0x6DC221; parseRGBColor RW 0x42EF99

#include "doctest.h"
#include "RetailTestMount.h"

#include "Common/NumericState.h"
#include "GameClient/TerrainRoads.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <string>

namespace
{

RoadTypeIndex scanOk(const std::string &ini)
{
	RoadTypeIndex idx;
	std::string err;
	REQUIRE_MESSAGE(TerrainRoads::scanText(ini, idx, &err), err);
	return idx;
}

std::string scanError(const std::string &ini)
{
	RoadTypeIndex idx;
	std::string err;
	CHECK_FALSE(TerrainRoads::scanText(ini, idx, &err));
	return err;
}

std::string bridge(const std::string &rows)
{
	return "Bridge B\n" + rows + "End\n";
}

} // namespace

TEST_CASE("LOCAL-27 bridges retail: the Bridge blocks of roads.ini read through the field table RW 0xC19DC0")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (mount == nullptr)
	{
		retailtest::printSkip("LOCAL-27 bridges retail");
		return;
	}
	REQUIRE_MESSAGE(mount->error.empty(), mount->error);
	RoadTypeIndex idx;
	std::string err;
	REQUIRE_MESSAGE(TerrainRoads::load(*mount->fs, idx, &err), err);

	const RoadType *wide = idx.find("TEST_EuropeanBridgeWide");
	const RoadType *fresh = idx.find("TEST_NewBridgeWide");
	REQUIRE(wide != nullptr);
	REQUIRE(fresh != nullptr);
	CHECK(wide->isBridge);
	CHECK(fresh->isBridge);
	const float grey = NumericState::pc24Mul(192.0f, 1.0f / 255.0f);
	for (const RoadType *b : { wide, fresh })
	{
		CHECK(b->bridgeScale == 0.6f);
		CHECK(b->radarColor[0] == grey);
		CHECK(b->radarColor[1] == grey);
		CHECK(b->radarColor[2] == grey);
		CHECK(b->scaffoldObjectName == "BridgeScaffold01");
		CHECK(b->scaffoldSupportObjectName == "BridgeScaffoldSupport01");
		for (const std::string &t : b->towerObjectName)
		{
			CHECK(t == "EuropeanTowerBridge01");
		}
		// no RoadWidth, sounds, transitions, TransitionEffectsHeight or NumFXPerType in the retail rows: the constructor's values stay
		CHECK(b->roadWidth == 0.0f);
		CHECK(b->transitionEffectsHeight == 0.0f);
		CHECK(b->numFXPerType == 0);
		CHECK(b->damageToSound[1].empty());
		CHECK(b->damageToOCL[1][0].empty());
	}
	// one model and texture for all four body states
	CHECK(wide->bridgeModelName == "CBWBrdgeArc");
	CHECK(wide->texture == "CBWBrdgeArc.tga");
	CHECK(wide->bridgeModelNameBroken == "CBWBrdgeArc");
	CHECK(wide->textureReallyDamaged == "CBWBrdgeArc.tga");
	CHECK(fresh->bridgeModelName == "CBBrdge1");
	CHECK(fresh->texture == "OSBridge2.tga");
	CHECK(fresh->bridgeModelNameDamaged == "CBBrdge1");
	CHECK(fresh->textureBroken == "OSBridge2.tga");
}

TEST_CASE("LOCAL-27 bridges: newBridge RW 0x6DC794 copies a DefaultBridge's texture, scale, names, height, count, sounds and damage OCL, "
          "not its scaffold, tower or radar rows")
{
	const RoadTypeIndex idx = scanOk(
		"Bridge DefaultBridge\n"
		"  BridgeScale = 0.75\n"
		"  RadarColor = R:10 G:20 B:30\n"
		"  ScaffoldObjectName = S\n"
		"  ScaffoldSupportObjectName = SS\n"
		"  BridgeModelName = M\n  Texture = T.tga\n"
		"  BridgeModelNameDamaged = MD\n  TextureDamaged = TD.tga\n"
		"  BridgeModelNameReallyDamaged = MR\n  TextureReallyDamaged = TR.tga\n"
		"  BridgeModelNameBroken = MB\n  TextureBroken = TB.tga\n"
		"  TowerObjectNameFromLeft = Tower\n"
		"  TransitionEffectsHeight = 12.5\n"
		"  NumFXPerType = 2\n"
		"  DamagedToSound = Crack\n"
		"  RepairedToSound = Mend\n"
		"  TransitionToOCL = Transition:Damage ToState:RUBBLE EffectNum:2 OCL:OCL_Fall\n"
		"  TransitionToFX = Transition:Repair ToState:DAMAGED EffectNum:1 FX:FX_Dust\n"
		"End\n"
		"Bridge Copy\n"
		"End\n");
	const RoadType *c = idx.find("Copy");
	REQUIRE(c != nullptr);
	CHECK(c->bridgeScale == 0.75f);
	CHECK(c->texture == "T.tga");
	CHECK(c->bridgeModelName == "M");
	CHECK(c->bridgeModelNameDamaged == "MD");
	CHECK(c->textureDamaged == "TD.tga");
	CHECK(c->bridgeModelNameReallyDamaged == "MR");
	CHECK(c->textureReallyDamaged == "TR.tga");
	CHECK(c->bridgeModelNameBroken == "MB");
	CHECK(c->textureBroken == "TB.tga");
	CHECK(c->transitionEffectsHeight == 12.5f);
	CHECK(c->numFXPerType == 2);
	CHECK(c->damageToSound[1] == "Crack");
	CHECK(c->repairedToSound[1] == "Mend");
	// the copy reads every OCL / FX slot with the damage OCL getter: DefaultBridge's FX is lost, its damage OCL lands in all four
	CHECK(c->damageToOCL[3][1] == "OCL_Fall");
	CHECK(c->damageToFX[3][1] == "OCL_Fall");
	CHECK(c->repairedToOCL[3][1] == "OCL_Fall");
	CHECK(c->repairedToFX[3][1] == "OCL_Fall");
	CHECK(c->repairedToFX[1][0].empty());
	// not copied
	CHECK(c->scaffoldObjectName.empty());
	CHECK(c->scaffoldSupportObjectName.empty());
	CHECK(c->towerObjectName[BRIDGE_TOWER_FROM_LEFT].empty());
	CHECK(c->radarColor[0] == 0.0f);
	// the DefaultBridge itself keeps its own FX
	CHECK(idx.find("DefaultBridge")->repairedToFX[1][0] == "FX_Dust");
}

TEST_CASE("LOCAL-27 bridges: a known bridge's second block parses into it (RW 0x5DA647); a road of that name is an error")
{
	const RoadTypeIndex idx = scanOk(bridge("  BridgeScale = 0.5\n  BridgeModelName = M\n") + bridge("  BridgeModelName = N\n"));
	CHECK(idx.find("B")->bridgeScale == 0.5f);
	CHECK(idx.find("B")->bridgeModelName == "N");
	CHECK(scanError("Road B\n  Texture = R.tga\nEnd\n" + bridge("")).find("Redefining road 'B' as a bridge!") != std::string::npos);
}

TEST_CASE("LOCAL-27 bridges: RadarColor reads R: G: B: in that order, each 0..255 (parseRGBColor RW 0x42EF99); anything else is an error")
{
	const RoadTypeIndex idx = scanOk(bridge("  RadarColor = R:255 G:0 B:128\n"));
	CHECK(idx.find("B")->radarColor[0] == 1.0f);
	CHECK(idx.find("B")->radarColor[1] == 0.0f);
	CHECK(idx.find("B")->radarColor[2] == NumericState::pc24Mul(128.0f, 1.0f / 255.0f));
	CHECK(scanError(bridge("  RadarColor = R:256 G:0 B:0\n")).find("out of range") != std::string::npos);
	CHECK(scanError(bridge("  RadarColor = R:-1 G:0 B:0\n")).find("out of range") != std::string::npos);
	CHECK(scanError(bridge("  RadarColor = 192 192 192\n")).find("expected 'R'") != std::string::npos);
	CHECK(scanError(bridge("  RadarColor = G:0 R:0 B:0\n")).find("expected 'R'") != std::string::npos);
	CHECK(scanError(bridge("  RadarColor = R:1 G:2\n")).find("expected 'B'") != std::string::npos);
}

TEST_CASE("LOCAL-27 bridges: TransitionToOCL / TransitionToFX (RW 0x6DC132 / 0x6DC221) and the sounds fill their state and effect slot")
{
	const RoadTypeIndex idx = scanOk(bridge(
		"  TransitionToOCL = Transition:Repair ToState:pristine EffectNum:3 OCL:OCL_Up\n"
		"  TransitionToFX = Transition:Damage ToState:REALLYDAMAGED EffectNum:1 FX:FX_Smoke\n"
		"  DamagedToSound = NoSound\n"));
	const RoadType *b = idx.find("B");
	CHECK(b->repairedToOCL[0][2] == "OCL_Up");
	CHECK(b->damageToFX[2][0] == "FX_Smoke");
	CHECK(b->damageToSound[1].empty()); // "NoSound" clears (RW 0x73AA94)
	CHECK(scanError(bridge("  TransitionToOCL = Transition:Damage ToState:RUBBLE EffectNum:0 OCL:X\n")).find("EffectNum") != std::string::npos);
	CHECK(scanError(bridge("  TransitionToOCL = Transition:Damage ToState:RUBBLE EffectNum:4 OCL:X\n")).find("EffectNum") != std::string::npos);
	CHECK(scanError(bridge("  TransitionToFX = Transition:Break ToState:RUBBLE EffectNum:1 FX:X\n")).find("Damage or Repair") != std::string::npos);
	CHECK(scanError(bridge("  TransitionToFX = Transition:Damage ToState:GONE EffectNum:1 FX:X\n")).find("GONE") != std::string::npos);
	CHECK(scanError(bridge("  TransitionToFX = Transition:Damage ToState:RUBBLE EffectNum:1 OCL:X\n")).find("expected 'FX'") != std::string::npos);
}

TEST_CASE("LOCAL-27 bridges: a field outside the table RW 0xC19DC0 is an error in a Bridge block (RoadWidth is a Road field only)")
{
	CHECK(scanError(bridge("  RoadWidth = 34\n")).find("unknown field 'roadwidth'") != std::string::npos);
	CHECK(scanError(bridge("  BridgeScale = wide\n")).find("floating point") != std::string::npos);
}
