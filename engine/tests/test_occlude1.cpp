// OpenBFME unit tests, lane OCCLUDE-1: the behind-building markers (GameClient/RTS3DSceneOcclusion.h). GPL-3.0.
//
// Pinned: the three GameData rows and the GlobalData constructor's defaults; which objects RTS3DScene::Visibility_Check (RW 0x470B83) makes occluders and
// occludees (and the order its list limits apply in); the safe occlusion frame (Object + 0x444) an object gets at construction; the player colour of the
// silhouette (RW 0x470326 .. 0x470411); script action 298 (RW 0x7CE207) and the snapshot's switch; the occlusion pass shaders' stencil and depth modes; and,
// with RW_GAME_DAT set, the retail bytes these values come from.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "PeImage.h"
#include "RetailTestMount.h"
#include "ScriptTestUtil.h"

#include "GameClient/LogicSnapshot.h"
#include "GameClient/RTS3DSceneOcclusion.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <cstdint>
#include <string>

namespace
{
using O = RTS3DSceneOcclusion;

const char kGameData[] =
	"GameData\n"
	"  ForceModelsToFollowTimeOfDay = Yes\n"
	"  ForceModelsToFollowWeather = No\n"
	"  UnitDamagedThreshold = 0.5\n"
	"  UnitReallyDamagedThreshold = 0.25\n"
	"  DefaultStartingCash = 700\n";

const char kObjects[] =
	"Object OcclTower\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object OcclFence\n"
	"  KindOf = STRUCTURE IMMOBILE NONOCCLUDING\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object OcclRider\n"
	"  KindOf = CAVALRY SELECTABLE\n"
	"  OcclusionDelay = 2000\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object OcclMarker\n"
	"  KindOf = INFANTRY IGNORED_IN_GUI\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n";

O::Facts unitFacts()
{
	O::Facts f;
	f.hasObject = true;
	f.unitKind = true;
	f.hasControllingPlayer = true;
	f.safeOcclusionFrame = 20;
	return f;
}

std::uint8_t snapshotBits(GameLogic &logic, const Object *o)
{
	const std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(logic, nullptr, false, 0);
	const ObjectSnapshot *rec = snap->find(o->getID());
	REQUIRE(rec != nullptr);
	return rec->occlusion;
}
} // namespace

TEST_CASE("occlude1 GameData: UseBehindBuildingMarker, DefaultOcclusionDelay and OccludedColorLuminanceScale parse; without them the constructor's 1 / 15 / 0.5")
{
	GameLogicSettings defaults;
	CHECK(defaults.useBehindBuildingMarker);
	CHECK(defaults.defaultOcclusionDelay == 15u); // RW 0x642B5F: LOGICFRAMES_PER_SECOND * 3
	CHECK(defaults.occludedColorLuminanceScale == 0.5f);
	std::string err;
	GameLogicSettings absent;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(std::string(kGameData) + "End\n", absent, &err), err);
	CHECK(absent.useBehindBuildingMarker);
	CHECK(absent.defaultOcclusionDelay == 15u);
	CHECK(absent.occludedColorLuminanceScale == 0.5f);
	CHECK(absent.unappliedFields.count("GameData.UseBehindBuildingMarker") == 0);
	GameLogicSettings retail; // retail 2.01 gamedata.ini's three lines
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(std::string(kGameData) +
		"  UseBehindBuildingMarker = Yes\n  DefaultOcclusionDelay = 3000  ; in ms\n  OccludedColorLuminanceScale = 0.5\nEnd\n", retail, &err), err);
	CHECK(retail.useBehindBuildingMarker);
	CHECK(retail.defaultOcclusionDelay == 15u); // 3000 ms * 0.005f, ceil
	CHECK(retail.occludedColorLuminanceScale == 0.5f);
	CHECK(retail.unappliedFields.count("GameData.DefaultOcclusionDelay") == 0);
	GameLogicSettings modded;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(std::string(kGameData) +
		"  UseBehindBuildingMarker = No\n  DefaultOcclusionDelay = 1000\n  OccludedColorLuminanceScale = 0.75\nEnd\n", modded, &err), err);
	CHECK_FALSE(modded.useBehindBuildingMarker);
	CHECK(modded.defaultOcclusionDelay == 5u);
	CHECK(modded.occludedColorLuminanceScale == 0.75f);
	GameLogicSettings bad;
	CHECK_FALSE(GameLogicSettingsLoader::scanGameData(std::string(kGameData) + "  UseBehindBuildingMarker = Maybe\nEnd\n", bad, &err));
	CHECK(err.find("UseBehindBuildingMarker") != std::string::npos);
}

TEST_CASE("occlude1 filter: Visibility_Check's occluder and occludee tests (RW 0x470DD2 .. 0x470EC5), one condition at a time")
{
	// the occluder: STRUCTURE and not NONOCCLUDING, nothing else asked
	O::Facts s;
	s.structure = true;
	CHECK(O::eligibility(s, 16) == O::ROLE_OCCLUDER);
	s.nonOccluding = true;
	CHECK(O::eligibility(s, 16) == 0);
	// the occludee
	const O::Facts u = unitFacts();
	CHECK(O::eligibility(u, 20) == O::ROLE_OCCLUDEE);
	CHECK(O::eligibility(u, 19) == 0); // the safe occlusion frame is not reached
	auto without = [&](void (*edit)(O::Facts &)) {
		O::Facts f = unitFacts();
		edit(f);
		return O::eligibility(f, 20);
	};
	CHECK(without([](O::Facts &f) { f.hasObject = false; }) == 0);
	CHECK(without([](O::Facts &f) { f.effectivelyDead = true; }) == 0);
	CHECK(without([](O::Facts &f) { f.unitKind = false; }) == 0);
	CHECK(without([](O::Facts &f) { f.ignoredOrInert = true; }) == 0);
	CHECK(without([](O::Facts &f) { f.hasControllingPlayer = false; }) == 0);
	CHECK(without([](O::Facts &f) { f.insideGarrison = true; }) == 0);
	CHECK(without([](O::Facts &f) { f.scalingWall = true; }) == 0);
	// a STRUCTURE that is also a unit KindOf qualifies for both
	O::Facts both = unitFacts();
	both.structure = true;
	CHECK(O::eligibility(both, 20) == (O::ROLE_OCCLUDER | O::ROLE_OCCLUDEE));
	// the frame: DefaultOcclusionDelay + 1 while the logic frame is not above the delay (RW 0x470C27 .. 0x470C38)
	CHECK(O::occlusionFrame(0, 15) == 16u);
	CHECK(O::occlusionFrame(15, 15) == 16u);
	CHECK(O::occlusionFrame(16, 15) == 16u);
	CHECK(O::occlusionFrame(17, 15) == 17u);
}

TEST_CASE("occlude1 filter: the list limits (512 each, RW 0x642D4C): translucent first, a full occluder list falls through to the occludee test")
{
	O::Budget b;
	// a translucent drawable takes no role while the translucent list has room; the 513th is sorted as an opaque one
	for (int i = 0; i < O::kMaxVisibleObjects; ++i)
	{
		REQUIRE(b.admit(O::ROLE_OCCLUDEE, true) == O::ROLE_NONE);
	}
	CHECK(b.admit(O::ROLE_OCCLUDEE, true) == O::ROLE_OCCLUDEE);
	O::Budget c;
	for (int i = 0; i < O::kMaxVisibleObjects; ++i)
	{
		REQUIRE(c.admit(O::ROLE_OCCLUDER, false) == O::ROLE_OCCLUDER);
	}
	CHECK(c.admit(O::ROLE_OCCLUDER, false) == O::ROLE_NONE);
	CHECK(c.admit(O::ROLE_OCCLUDER | O::ROLE_OCCLUDEE, false) == O::ROLE_OCCLUDEE);
	CHECK(c.occluders == O::kMaxVisibleObjects);
	CHECK(c.occludees == 1);
	CHECK(c.admit(0, false) == O::ROLE_NONE);
}

TEST_CASE("occlude1 colour: HSV value times OccludedColorLuminanceScale, packed with alpha 127 and truncated channels (Convert_Color(rgb, 0.5))")
{
	CHECK(O::occludedColor(0xFF0000u, 0.5f) == 0x7F7F0000u); // 255 * 0.5 = 127.5 -> 127
	CHECK(O::occludedColor(0x00FF00u, 0.5f) == 0x7F007F00u);
	CHECK(O::occludedColor(0x0000FFu, 1.0f) == 0x7F0000FFu);
	CHECK(O::occludedColor(0xFFFFFFu, 0.5f) == 0x7F7F7F7Fu); // no saturation: the value only
	CHECK(O::occludedColor(0xFF0000FFu, 1.0f) == 0x7F0000FFu); // the player colour's top byte is not read
	CHECK(O::occludedColor(0x000000u, 0.5f) == 0x7F000000u);
}

TEST_CASE("occlude1 objects: the safe occlusion frame (Object + 0x444 = frame + OcclusionDelay), and which live objects the snapshot marks")
{
	combattest::CombatWorld f(kObjects);
	GameLogic &logic = *f.logic;
	REQUIRE(logic.settings().defaultOcclusionDelay == 15u);
	// past DefaultOcclusionDelay: below it the frame compared is raised to delay + 1 (an early object's frame is already reached)
	while (logic.getFrame() < 20u)
	{
		logic.runLogicFrame();
	}
	const UnsignedInt born = logic.getFrame();
	Object *tower = f.unit("OcclTower", 'A', 300.0f, 300.0f);
	Object *fence = f.unit("OcclFence", 'A', 400.0f, 300.0f);
	Object *soldier = f.unit("Dummy", 'A', 500.0f, 300.0f);
	Object *rider = f.unit("OcclRider", 'B', 600.0f, 300.0f);
	Object *marker = f.unit("OcclMarker", 'A', 700.0f, 300.0f);
	REQUIRE((tower && fence && soldier && rider && marker));
	// RW 0x69A687: the template's OcclusionDelay, the GameData default unless the object sets it (2000 ms -> 10 frames)
	CHECK(soldier->getSafeOcclusionFrame() == born + 15u);
	CHECK(rider->getSafeOcclusionFrame() == born + 10u);
	CHECK(Object::occlusionDelayOf(*rider->getTemplate(), logic) == 10u);
	CHECK(Object::occlusionDelayOf(*soldier->getTemplate(), logic) == 15u);
	// just created: the units are not occludees yet; the structure is an occluder at once, the NONOCCLUDING one never
	CHECK(snapshotBits(logic, tower) == O::ROLE_OCCLUDER);
	CHECK(snapshotBits(logic, fence) == 0);
	CHECK(snapshotBits(logic, soldier) == 0);
	CHECK(snapshotBits(logic, rider) == 0);
	while (logic.getFrame() < born + 10u)
	{
		logic.runLogicFrame();
	}
	CHECK(snapshotBits(logic, rider) == O::ROLE_OCCLUDEE);
	CHECK(snapshotBits(logic, soldier) == 0);
	while (logic.getFrame() < born + 15u)
	{
		logic.runLogicFrame();
	}
	CHECK(snapshotBits(logic, soldier) == O::ROLE_OCCLUDEE);
	CHECK(snapshotBits(logic, marker) == 0); // IGNORED_IN_GUI
	// inside a garrison, scaling a wall, effectively dead: out
	soldier->setStatus((unsigned)CombatNames::statuses().insideGarrison, true);
	CHECK(snapshotBits(logic, soldier) == 0);
	soldier->setStatus((unsigned)CombatNames::statuses().insideGarrison, false);
	soldier->setModelConditionState(CombatNames::modelCondition("SCALING_WALL"), true);
	CHECK(snapshotBits(logic, soldier) == 0);
	soldier->setModelConditionState(CombatNames::modelCondition("SCALING_WALL"), false);
	CHECK(snapshotBits(logic, soldier) == O::ROLE_OCCLUDEE);
	soldier->kill(DAMAGE_UNRESISTABLE, 0);
	CHECK(snapshotBits(logic, soldier) == 0);
}

TEST_CASE("occlude1 script: OPTIONS_SET_OCCLUSION_MODE (case 298) sets TheGameLogic + 0x99, the snapshot carries it and the GameData switch, a reset sets it again")
{
	scripttest::ScriptGame g;
	g.start();
	GameLogic &logic = *g.lw.logic;
	CHECK(logic.getShowBehindBuildingMarkers());
	ScriptActions::execute(g.engine(), scripttest::act("OPTIONS_SET_OCCLUSION_MODE", { scripttest::ip(8, 0) }));
	CHECK_FALSE(logic.getShowBehindBuildingMarkers());
	CHECK_FALSE(LogicSnapshot::build(logic, nullptr, false, 0)->showBehindBuildingMarkers);
	ScriptActions::execute(g.engine(), scripttest::act("OPTIONS_SET_OCCLUSION_MODE", { scripttest::ip(8, 7) })); // any non-zero int
	CHECK(logic.getShowBehindBuildingMarkers());
	CHECK(LogicSnapshot::build(logic, nullptr, false, 0)->showBehindBuildingMarkers);
	ScriptActions::execute(g.engine(), scripttest::act("OPTIONS_SET_OCCLUSION_MODE", { scripttest::ip(8, 0) }));
	const std::uint32_t hashOff = logic.computeStateHash();
	logic.setShowBehindBuildingMarkers(true);
	CHECK(logic.computeStateHash() == hashOff); // presentation only: not hashed
	logic.setShowBehindBuildingMarkers(false);
	logic.reset(); // RW 0x62D3D6
	CHECK(logic.getShowBehindBuildingMarkers());
	// the GameData values travel with the snapshot (the renderer never asks the logic for them)
	logic.settings().useBehindBuildingMarker = false;
	logic.settings().occludedColorLuminanceScale = 0.75f;
	const std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(logic, nullptr, false, 0);
	CHECK_FALSE(snap->useBehindBuildingMarker);
	CHECK(snap->occludedColorLuminanceScale == 0.75f);
}

TEST_CASE("occlude1 shaders: the mark pass writes the stencil in front, the colour pass draws behind where the mark is, once; the surface keeps its own")
{
	W3DShaderKey k;
	k.Skin = true;
	k.Texturing = true;
	k.Tex[0] = true;
	k.AlphaTest = true;
	const std::string base = Generate_W3D_Shader_Code(k);
	CHECK(base.find("stencil_mode") == std::string::npos);
	W3DShaderKey mk = k;
	mk.OcclusionPass = W3D_OCCLUSION_PASS_MARK;
	W3DShaderKey co = k;
	co.OcclusionPass = W3D_OCCLUSION_PASS_COLOR;
	CHECK(mk.Id() != k.Id());
	CHECK(co.Id() != mk.Id());
	const std::string mark = Generate_W3D_Shader_Code(mk);
	CHECK(mark.find("render_mode blend_mix, depth_draw_never, cull_back, unshaded, shadows_disabled;") != std::string::npos);
	CHECK(mark.find("stencil_mode write, compare_always, 255;") != std::string::npos);
	CHECK(mark.find("if ((occl & 32) == 0) { VERTEX = vec3(0.0); }") != std::string::npos);
	CHECK(mark.find("if (a < w3d_alpha_ref) { discard; }") != std::string::npos); // the surface's alpha test: its holes mark nothing
	CHECK(mark.find("ALPHA = 0.0;") != std::string::npos);
	const std::string color = Generate_W3D_Shader_Code(co);
	CHECK(color.find("render_mode blend_mix, depth_draw_never, depth_test_inverted, cull_back, unshaded, shadows_disabled;") != std::string::npos);
	CHECK(color.find("stencil_mode read, write, compare_less, 1;") != std::string::npos);
	CHECK(color.find("global uniform sampler2D w3d_occl_colors;") != std::string::npos);
	CHECK(color.find("if (slot == 0 || w3d_occl_on < 0.5) { VERTEX = vec3(0.0); }") != std::string::npos);
	CHECK(color.find("ALPHA = oc.a;") != std::string::npos);
	// the vertex code before the occlusion lines is the surface's own (the mark's depth must equal the surface's)
	const std::string head = base.substr(base.find("void vertex() {"), base.find("void fragment() {") - base.find("void vertex() {"));
	const std::string markVertex = mark.substr(mark.find("void vertex() {"));
	CHECK(markVertex.compare(0, head.size() - 2, head, 0, head.size() - 2) == 0);
	// the bits stay exact in INSTANCE_CUSTOM.w: bone 1023 + sway 15 + fogged + slot 31 + occluder < 2^24
	CHECK((1023 + 1024 * 15 + 16384 + (31 << W3D_OCCLUSION_SLOT_SHIFT) + W3D_OCCLUSION_OCCLUDER_BIT) == 2097151);
}

TEST_CASE("occlude1 binary facts: the filter's KindOf / status / condition pushes, the GameData rows, the limits and action 298 (RW_GAME_DAT)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("occlude1 binary facts (RW_GAME_DAT unset)");
		return;
	}
	CHECK(pe->hex(0x470E40, 10) == "6a0b6a096a0a6a086a00"); // RW 0x46E771(0, 8, 10, 9, 11): INFANTRY, MONSTER, CAVALRY, MACHINE
	CHECK(pe->hex(0x470E5F, 6) == "6a596a2f6a00");         // RW 0x46E72F(0, 0x2F, 0x59): IGNORED_IN_GUI, INERT
	CHECK(pe->hex(0x470E94, 2) == "6a3a");                 // testStatus(INSIDE_GARRISON)
	CHECK(pe->hex(0x470EA2, 5) == "68ba010000");           // the model condition 0x1BA (SCALING_WALL)
	CHECK(pe->hex(0x470DDE, 7) == "f6820801000080");       // template KindOf byte + 0x108 bit 7 (STRUCTURE)
	CHECK(pe->hex(0x470DF5, 7) == "f6821501000080");       // byte + 0x115 bit 7 (bit 111, NONOCCLUDING)
	CHECK(pe->hex(0x470E33, 7) == "f6815804000001");       // Object + 0x458 bit 0 (effectively dead)
	CHECK(pe->hex(0x470E80, 6) == "398144040000");         // Object + 0x444 against the frame
	CHECK(CombatNames::status("INSIDE_GARRISON") == 0x3A);
	CHECK(CombatNames::modelCondition("SCALING_WALL") == 0x1BA);
	// GameData rows: name, parser, user data, offset
	CHECK(pe->cstring(pe->u32At(0xBFF830)) == "UseBehindBuildingMarker");
	CHECK(pe->u32At(0xBFF834) == 0x42E558u);
	CHECK(pe->u32At(0xBFF83C) == 0x68u);
	CHECK(pe->cstring(pe->u32At(0xBFF9E0)) == "DefaultOcclusionDelay");
	CHECK(pe->u32At(0xBFF9E4) == 0x73A429u);
	CHECK(pe->u32At(0xBFF9EC) == 0xCCu);
	CHECK(pe->cstring(pe->u32At(0xBFFFE0)) == "OccludedColorLuminanceScale");
	CHECK(pe->u32At(0xBFFFE4) == 0x42ED00u);
	CHECK(pe->u32At(0xBFFFEC) == 0x984u);
	CHECK(pe->cstring(pe->u32At(0xDA45E8)) == "OcclusionDelay"); // the ThingTemplate row
	CHECK(pe->u32At(0xDA45F4) == 0x564u);
	// GlobalData's constructor: the four list limits 0x200, the luminance scale 0.5f
	CHECK(pe->hex(0x642D4C, 5) == "b800020000");
	CHECK(pe->hex(0xBD869C, 4) == "0000003f");
	// case 298 of the action switch (table RW 0x7CF857) and the + 0x99 store of RW 0x7BD549
	CHECK(pe->u32At(0x7CF857 + 298 * 4) == 0x7CE207u);
	CHECK(pe->hex(0x7BD553, 6) == "888199000000");
}
