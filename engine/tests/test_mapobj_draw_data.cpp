// OpenBFME unit tests: the MAPOBJ-1 draw module data (W3DTreeDraw, W3DPropDraw, W3DFloorDraw, W3DTruckDraw, W3DSailModelDraw,
// W3DQuadrupedDraw, W3DTankDraw, W3DSupplyDraw). Expectations come from the INI text in the tests, from the RotWK tables in
// engine/tests/data/mapobj/draw_variant_tables.json (tools/mapobj/extract_mapobj_tables.py) and from constructor facts read off the
// binary (cited at W3DTreeDraw.h / W3DModelDrawVariants.h); never from running the code under test.

#include "doctest.h"

#include "Common/MiniJson.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawVariants.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.h"
#include "ObjectTestUtil.h"

#include <cmath>
#include <fstream>
#include <sstream>

namespace
{
std::string readFile(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	REQUIRE_MESSAGE(f.good(), path);
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

INIFieldParseProc proc(unsigned va)
{
	switch (va)
	{
	case 0x42e558: return INI::parseBool;
	case 0x42ed00: return INI::parseReal;
	case 0x42ee5e: return INI::parseAsciiString;
	case 0x42ecb2: return INI::parseUnsignedInt;
	case 0x42ee15: return INI::parseAngleReal;
	case 0x73a429: return INI::parseDurationUnsignedInt;
	case 0x73a4b6: return INI::parseVelocityReal;
	case 0x42ed1c: return INI::parsePositiveNonZeroReal;
	case 0x42eefa: return INI::parsePercentToReal;
	default: return nullptr; // custom parsers (FXList name, WeatherTexture, HideIfModelConditions): compared by name only
	}
}

void compareTable(const JsonValue &doc, const char *name, const FieldParse *mine)
{
	const JsonValue *tables = doc.get("tables");
	REQUIRE(tables != nullptr);
	const JsonValue *table = tables->get(name);
	REQUIRE_MESSAGE(table != nullptr, name);
	const JsonValue *rows = table->get("rows");
	REQUIRE(rows != nullptr);
	size_t i = 0;
	for (const FieldParse *f = mine; f->token; ++f, ++i)
	{
		REQUIRE_MESSAGE(i < rows->array.size(), name << ": more rows than the binary's table");
		const JsonValue &row = rows->array[i];
		CHECK_MESSAGE(std::string(f->token) == row.get("token")->string, name << " row " << i);
		const unsigned fn = (unsigned)std::stoul(row.get("parse")->string, nullptr, 16);
		if (INIFieldParseProc generic = proc(fn))
		{
			CHECK_MESSAGE(f->parse == generic, name << " row " << i << " (" << f->token << ") uses the parser at RW " << row.get("parse")->string);
		}
	}
	CHECK_MESSAGE(i == rows->array.size(), name << ": fewer rows than the binary's table");
}

struct World : objtest::World
{
	World() { W3DDrawModules::registerTypedDrawModuleData(modules); }
};

template <class T>
const T *drawData(const ThingTemplate *t, size_t n = 0)
{
	REQUIRE(t != nullptr);
	REQUIRE(t->drawModules().nuggets().size() > n);
	return dynamic_cast<const T *>(t->drawModules().nuggets()[n].data.get());
}
} // namespace

TEST_CASE("mapobj draw data: every table equals the binary's (names, order, standard parse functions)")
{
	const std::string text = readFile(std::string(OPENBFME_TEST_DATA_DIR) + "/mapobj/draw_variant_tables.json");
	JsonValue doc;
	std::string error;
	REQUIRE_MESSAGE(JsonValue::parse(text, doc, &error), error);
	compareTable(doc, "W3DTreeDraw", W3DTreeDrawTables::tree());
	compareTable(doc, "W3DPropDraw", W3DTreeDrawTables::prop());
	compareTable(doc, "W3DFloorDraw", W3DTreeDrawTables::floor());
	compareTable(doc, "W3DTruckDraw", W3DVariantTables::truck());
	compareTable(doc, "W3DSailModelDraw", W3DVariantTables::sail());
	compareTable(doc, "W3DQuadrupedDraw", W3DVariantTables::quadruped());
	compareTable(doc, "W3DTankDraw", W3DVariantTables::tank());
	compareTable(doc, "W3DSupplyDraw", W3DVariantTables::supply());
	const JsonValue *lists = doc.get("lists");
	REQUIRE(lists != nullptr);
	const JsonValue *weather = lists->get("WeatherTexture.weather");
	REQUIRE(weather != nullptr);
	const char *const *mine = W3DTreeDrawTables::weatherNames();
	size_t i = 0;
	for (; mine[i]; ++i)
	{
		REQUIRE(i < weather->array.size());
		CHECK(std::string(mine[i]) == weather->array[i].string);
	}
	CHECK(i == weather->array.size());
}

TEST_CASE("mapobj draw data: the registry's 20 draw classes are all classified, and the typed ones bind")
{
	CHECK(W3DDrawModules::all().size() == 20);
	World w;
	// every classified class is a registered DRAW class of the factory, in the registry's own spelling
	for (const W3DDrawClassInfo &c : W3DDrawModules::all())
	{
		const ModuleFactory::ModuleTemplate *t = w.modules.findModuleTemplate(c.name, MODULETYPE_DRAW);
		REQUIRE_MESSAGE(t != nullptr, c.name);
		const bool typedKind = c.kind == W3D_DRAWKIND_MODEL || c.kind == W3D_DRAWKIND_TREE || c.kind == W3D_DRAWKIND_PROP || c.kind == W3D_DRAWKIND_FLOOR ||
			std::string(c.name) == "W3DDefaultDraw" || std::string(c.name) == "W3DStreakDraw"; // RENDER-1 binds the streak data
		CHECK_MESSAGE(t->typed == typedKind, c.name);
	}
	size_t drawClasses = 0;
	for (const std::string &n : w.modules.rawClassNames())
	{
		(void)n;
		++drawClasses;
	}
	CHECK(w.modules.typedCount() == 12); // + W3DStreakDraw (RENDER-1)
	CHECK(W3DDrawModules::find("w3dtreedraw") == nullptr); // class names are case sensitive
	REQUIRE(W3DDrawModules::find("W3DTreeDraw") != nullptr);
	CHECK(W3DDrawModules::find("W3DTreeDraw")->kind == W3D_DRAWKIND_TREE);
	CHECK(W3DDrawModules::find("W3DLightDraw")->kind == W3D_DRAWKIND_NOT_DRAWN);
	CHECK(W3DDrawModules::find("W3DDefaultDraw")->kind == W3D_DRAWKIND_NOTHING);
	CHECK(W3DDrawModules::notDrawnClasses().size() == 10); // nine effect draws + W3DDefaultDraw
}

TEST_CASE("mapobj draw data: W3DTreeDraw parses every field and keeps the constructor defaults (RW 0x4CE3A8)")
{
	World w;
	const std::string text =
		"Object TreeA\n"
		"  Draw = W3DTreeDraw ModuleTag_01\n"
		"    ModelName = PTTree01\n"
		"    TextureName = PTTree.tga\n"
		"    MoveOutwardTime = 400\n"
		"    MoveInwardTime = 1000\n"
		"    MoveOutwardDistanceFactor = 2.0\n"
		"    DarkeningFactor = 0.5\n"
		"    ToppleFX = FX_Topple\n"
		"    BounceFX = FX_Bounce\n"
		"    StumpName = PTStump\n"
		"    KillWhenFinishedToppling = No\n"
		"    DoTopple = Yes\n"
		"    InitialVelocityPercent = 50%\n"
		"    InitialAccelPercent = 10%\n"
		"    BounceVelocityPercent = 25%\n"
		"    MinimumToppleSpeed = 1.5\n"
		"    SinkDistance = 7.0\n"
		"    SinkTime = 600\n"
		"    MorphTree = TreeB\n"
		"    MorphTime = 2000\n"
		"    MorphFX = FX_Morph\n"
		"    TaintedTree = Yes\n"
		"    FadeRate = 9\n"
		"    FadeTarget = 100\n"
		"    FadeDistance = 55.0\n"
		"  End\n"
		"End\n"
		"Object TreeDefaults\n"
		"  Draw = W3DTreeDraw ModuleTag_01\n"
		"    ModelName = M\n"
		"  End\n"
		"End\n";
	REQUIRE(w.load(text).empty());
	const W3DTreeDrawModuleData *d = drawData<W3DTreeDrawModuleData>(w.get("TreeA"));
	REQUIRE(d != nullptr);
	CHECK(d->m_modelName == "PTTree01");
	CHECK(d->m_textureName == "PTTree.tga");
	// durations: ceil(ms * 0.005) logic frames (parseDurationUnsignedInt, RW 0x73A429)
	CHECK(d->m_framesToMoveOutward == 2);
	CHECK(d->m_framesToMoveInward == 5);
	CHECK(d->m_maxOutwardMovement == 2.0f);
	CHECK(d->m_darkening == 0.5f);
	CHECK(d->m_toppleFX == "FX_Topple");
	CHECK(d->m_bounceFX == "FX_Bounce");
	CHECK(d->m_stumpName == "PTStump");
	CHECK_FALSE(d->m_killWhenToppled);
	CHECK(d->m_doTopple);
	CHECK(d->m_initialVelocityPercent == doctest::Approx(0.5f));
	CHECK(d->m_initialAccelPercent == doctest::Approx(0.1f));
	CHECK(d->m_bounceVelocityPercent == doctest::Approx(0.25f));
	CHECK(d->m_minimumToppleSpeed == 1.5f);
	CHECK(d->m_sinkDistance == 7.0f);
	CHECK(d->m_sinkFrames == 3);
	CHECK(d->m_morphTree == "TreeB");
	CHECK(d->m_morphTime == 10);
	CHECK(d->m_morphFX == "FX_Morph");
	CHECK(d->m_taintedTree);
	CHECK(d->m_fadeRate == 9);
	CHECK(d->m_fadeTarget == 100);
	CHECK(d->m_fadeDistance == 55.0f);

	const W3DTreeDrawModuleData *def = drawData<W3DTreeDrawModuleData>(w.get("TreeDefaults"));
	REQUIRE(def != nullptr);
	CHECK(def->m_framesToMoveOutward == 1);
	CHECK(def->m_framesToMoveInward == 1);
	CHECK(def->m_maxOutwardMovement == 1.0f);
	CHECK(def->m_darkening == 0.0f);
	CHECK(def->m_initialVelocityPercent == doctest::Approx(0.2f));
	CHECK(def->m_initialAccelPercent == doctest::Approx(0.01f));
	CHECK(def->m_bounceVelocityPercent == doctest::Approx(0.3f));
	CHECK(def->m_minimumToppleSpeed == 0.5f);
	CHECK(def->m_killWhenToppled);
	CHECK_FALSE(def->m_doTopple);
	CHECK(def->m_sinkFrames == 50);   // 10 * LogicFramesPerSecond (5)
	CHECK(def->m_sinkDistance == 20.0f);
	CHECK(def->m_morphTime == 50);
	CHECK_FALSE(def->m_taintedTree);
	CHECK(def->m_fadeRate == 5);
	CHECK(def->m_fadeTarget == 0x69);
	CHECK(def->m_fadeDistance == 40.0f);
	// an unknown field is an error, like any table (no catch-all)
	CHECK(objtest::contains(w.load("Object T3\n  Draw = W3DTreeDraw T\n    Nope = 1\n  End\nEnd\n"), "Unknown field 'Nope'"));
	// a non-positive MinimumToppleSpeed is rejected (RW 0x42ED1C)
	CHECK_FALSE(w.load("Object T4\n  Draw = W3DTreeDraw T\n    MinimumToppleSpeed = 0\n  End\nEnd\n").empty());
}

TEST_CASE("mapobj draw data: W3DPropDraw and W3DFloorDraw")
{
	World w;
	const std::string text =
		"Object P1\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = WPSkull01\n    DistanceFog = No\n  End\nEnd\n"
		"Object P2\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = WPSkull02\n  End\nEnd\n"
		"Object F1\n  Draw = W3DFloorDraw ModuleTag_DrawFloor\n"
		"    StaticModelLODMode = Yes\n    ModelName = EBTree_Bib\n"
		"    WeatherTexture = SNOWY EBTree_Bib_snow.tga\n"
		"    WeatherTexture = NORMAL EBTree_Bib.tga\n"
		"    HideIfModelConditions = AWAITING_CONSTRUCTION\n"
		"    HideIfModelConditions = PARTIALLY_CONSTRUCTED NIGHT\n"
		"    ForceToBack = Yes\n    StartHidden = Yes\n    FloorFadeRateOnObjectDeath = 0.25\n"
		"  End\nEnd\n"
		"Object F2\n  Draw = W3DFloorDraw ModuleTag_DrawFloor\n    ModelName = Plain\n  End\nEnd\n";
	REQUIRE(w.load(text).empty());
	const W3DPropDrawModuleData *p1 = drawData<W3DPropDrawModuleData>(w.get("P1"));
	REQUIRE(p1 != nullptr);
	CHECK(dynamic_cast<const W3DFloorDrawModuleData *>(p1) == nullptr);
	CHECK(p1->m_modelName == "WPSkull01");
	CHECK_FALSE(p1->m_distanceFog);
	CHECK(drawData<W3DPropDrawModuleData>(w.get("P2"))->m_distanceFog); // constructor default (RW 0x4CE710)
	const W3DFloorDrawModuleData *f1 = drawData<W3DFloorDrawModuleData>(w.get("F1"));
	REQUIRE(f1 != nullptr);
	CHECK(f1->m_modelName == "EBTree_Bib");
	CHECK(f1->m_distanceFog); // the prop table's default carries over
	CHECK(f1->m_staticModelLODMode);
	CHECK(f1->m_forceToBack);
	CHECK(f1->m_startHidden);
	CHECK(f1->m_floorFadeRateOnObjectDeath == 0.25f);
	REQUIRE(f1->m_weatherTextures.size() == 2);
	CHECK(f1->m_weatherTextures[0] == std::make_pair(1, std::string("EBTree_Bib_snow.tga")));
	CHECK(f1->m_weatherTextures[1] == std::make_pair(0, std::string("EBTree_Bib.tga")));
	REQUIRE(f1->m_hideIfModelConditions.size() == 2); // one set per line, appended (RW 0x4CEF06)
	CHECK(f1->m_hideIfModelConditions[0].count() == 1);
	CHECK(f1->m_hideIfModelConditions[0].test(ModelCondition::indexOf("AWAITING_CONSTRUCTION")));
	CHECK(f1->m_hideIfModelConditions[1].count() == 2);
	CHECK(f1->m_hideIfModelConditions[1].test(ModelCondition::indexOf("NIGHT")));
	const W3DFloorDrawModuleData *f2 = drawData<W3DFloorDrawModuleData>(w.get("F2"));
	REQUIRE(f2 != nullptr);
	CHECK_FALSE(f2->m_staticModelLODMode);
	CHECK_FALSE(f2->m_forceToBack);
	CHECK_FALSE(f2->m_startHidden);
	CHECK(f2->m_floorFadeRateOnObjectDeath == 0.0f);
	CHECK(f2->m_weatherTextures.empty());
	CHECK(f2->m_hideIfModelConditions.empty());
	// the prop table has no floor fields
	CHECK(objtest::contains(w.load("Object P3\n  Draw = W3DPropDraw T\n    ForceToBack = Yes\n  End\nEnd\n"), "Unknown field 'ForceToBack'"));
	// a weather word outside RW 0xDA3A84 is an error
	CHECK_FALSE(w.load("Object F3\n  Draw = W3DFloorDraw T\n    WeatherTexture = FOGGY a.tga\n  End\nEnd\n").empty());
}

TEST_CASE("mapobj draw data: the ModelDraw variants add their own table after the model draw table, with the constructors' defaults")
{
	World w;
	const std::string text =
		"Object Tr\n  Draw = W3DTruckDraw ModuleTag_Draw\n"
		"    DefaultModelConditionState\n      Model = TruckMdl\n    End\n"
		"    Dust = TruckDust\n    LeftFrontTireBone = TireLF\n    MidRightMidTireBone2 = TireMRM2\n    CabBone = Cab\n"
		"    TireRotationMultiplier = 0.5\n    RotationDamping = 0.2\n"
		"  End\nEnd\n"
		"Object TrDef\n  Draw = W3DTruckDraw ModuleTag_Draw\n    DefaultModelConditionState\n      Model = T\n    End\n  End\nEnd\n"
		"Object Sl\n  Draw = W3DSailModelDraw ModuleTag_Draw\n"
		"    DefaultModelConditionState\n      Model = SailMdl\n    End\n"
		"    MaxRotationDegrees = 90\n    AboutDamping = 0.3\n"
		"  End\nEnd\n"
		"Object SlDef\n  Draw = W3DSailModelDraw ModuleTag_Draw\n    DefaultModelConditionState\n      Model = S\n    End\n  End\nEnd\n"
		"Object Qd\n  Draw = W3DQuadrupedDraw ModuleTag_Draw\n"
		"    DefaultModelConditionState\n      Model = QuadMdl\n    End\n"
		"    LeftFrontFootBone = LF\n    RightRearFootBone = RR\n"
		"  End\nEnd\n"
		"Object Tk\n  Draw = W3DTankDraw ModuleTag_Draw\n    DefaultModelConditionState\n      Model = TankMdl\n    End\n    TreadDebrisLeft = DL\n  End\nEnd\n"
		"Object TkDef\n  Draw = W3DTankDraw ModuleTag_Draw\n    DefaultModelConditionState\n      Model = T\n    End\n  End\nEnd\n"
		"Object Su\n  Draw = W3DSupplyDraw ModuleTag_Draw\n    DefaultModelConditionState\n      Model = SupMdl\n    End\n    SupplyBonePrefix = Box\n  End\nEnd\n";
	REQUIRE(w.load(text).empty());
	const W3DTruckDrawModuleData *tr = drawData<W3DTruckDrawModuleData>(w.get("Tr"));
	REQUIRE(tr != nullptr);
	CHECK(tr->m_conditionStates.size() == 1); // the model draw table still applies
	CHECK(tr->m_conditionStates[0].modelName() == "TruckMdl");
	CHECK(tr->m_dust == "TruckDust");
	CHECK(tr->m_leftFrontTireBone == "TireLF");
	CHECK(tr->m_midRightMidTireBone2 == "TireMRM2");
	CHECK(tr->m_cabBone == "Cab");
	CHECK(tr->m_tireRotationMultiplier == 0.5f);
	CHECK(tr->m_rotationDamping == 0.2f);
	const W3DTruckDrawModuleData *trd = drawData<W3DTruckDrawModuleData>(w.get("TrDef"));
	REQUIRE(trd != nullptr);
	CHECK(trd->m_cabRotationMultiplier == 1.0f);   // RW 0x4CA9B6
	CHECK(trd->m_trailerRotationMultiplier == 1.0f);
	CHECK(trd->m_rotationDamping == 1.0f);
	CHECK(trd->m_tireRotationMultiplier == 1.0f);
	CHECK(trd->m_powerslideRotationAddition == 0.0f);
	CHECK(trd->m_dust.empty());
	const W3DSailModelDrawModuleData *sl = drawData<W3DSailModelDrawModuleData>(w.get("Sl"));
	REQUIRE(sl != nullptr);
	CHECK(sl->m_maxRotation == doctest::Approx(1.5707964f).epsilon(1e-5)); // 90 degrees in radians (parseAngleReal)
	CHECK(sl->m_aboutDamping == 0.3f);
	CHECK(sl->m_blowingThreshold == 0.25f); // RW 0x4CFF2A default, stored raw
	const W3DSailModelDrawModuleData *sld = drawData<W3DSailModelDrawModuleData>(w.get("SlDef"));
	CHECK(sld->m_maxRotation == 0.0f);
	CHECK(sld->m_aboutDamping == doctest::Approx(0.05f));
	const W3DQuadrupedDrawModuleData *qd = drawData<W3DQuadrupedDrawModuleData>(w.get("Qd"));
	REQUIRE(qd != nullptr);
	CHECK(qd->m_leftFrontFootBone == "LF");
	CHECK(qd->m_rightRearFootBone == "RR");
	CHECK(qd->m_rightFrontFootBone.empty());
	const W3DTankDrawModuleData *tk = drawData<W3DTankDrawModuleData>(w.get("Tk"));
	REQUIRE(tk != nullptr);
	CHECK(tk->m_treadDebrisLeft == "DL");
	CHECK(tk->m_treadDebrisRight == "TrackDebrisDirtRight"); // RW 0x4CDEDA default
	const W3DTankDrawModuleData *tkd = drawData<W3DTankDrawModuleData>(w.get("TkDef"));
	CHECK(tkd->m_treadDebrisLeft == "TrackDebrisDirtLeft");
	CHECK(tkd->m_treadPivotSpeedFraction == doctest::Approx(0.6f));
	CHECK(tkd->m_treadDriveSpeedFraction == doctest::Approx(0.3f));
	CHECK(tkd->m_treadAnimationRate == 0.0f);
	const W3DSupplyDrawModuleData *su = drawData<W3DSupplyDrawModuleData>(w.get("Su"));
	REQUIRE(su != nullptr);
	CHECK(su->m_supplyBonePrefix == "Box");
	// each variant rejects another variant's field
	CHECK(objtest::contains(w.load("Object Z1\n  Draw = W3DQuadrupedDraw T\n    DefaultModelConditionState\n      Model = Q\n    End\n    CabBone = X\n  End\nEnd\n"), "Unknown field 'CabBone'"));
}
