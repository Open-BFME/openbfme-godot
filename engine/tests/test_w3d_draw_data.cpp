// OpenBFME unit tests: draw module data (lane DRAW-1, spec w3d-and-draw.md 4.3-4.4, checklist step 14).
// Every expectation is derived by hand from the INI text in the test (and from the RotWK tables in
// engine/tests/data/draw/draw_field_tables.json), never from running the code under test.

#include "doctest.h"

#include "Common/MiniJson.h"
#include "ObjectTestUtil.h"
#include "RetailTestMount.h"
#include "W3DDrawTestUtil.h"

#include <cmath>

using namespace drawtest;

namespace
{
std::string trimmed(const std::string &s)
{
	const size_t b = s.find_first_not_of(" \t");
	const size_t e = s.find_last_not_of(" \t");
	return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

const char *kBasicDefault = "DefaultModelConditionState\n  Model = M\nEnd\n";
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// the tables
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
INIFieldParseProc proc(unsigned va)
{
	switch (va)
	{
	case 0x42e558: return INI::parseBool;
	case 0x42ec5e: return INI::parseInt;
	case 0x42ed00: return INI::parseReal;
	case 0x42ee5e: return INI::parseAsciiString;
	case 0x42e956: return INI::parseIndexList;
	case 0x42e840: return INI::parseBitString32;
	case 0x42eed6: return INI::parseAsciiStringVector;
	case 0x42e59e: return INI::parseAsciiStringVectorAppend;
	case 0x73a4b6: return INI::parseVelocityReal;
	case 0x73a429: return INI::parseDurationUnsignedInt;
	case 0x42ed1c: return INI::parsePositiveNonZeroReal;
	case 0x42eefa: return INI::parsePercentToReal;
	default: return nullptr;
	}
}

unsigned hexValue(const std::string &s) { return (unsigned)std::stoul(s, nullptr, 16); }

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
		const unsigned fn = hexValue(row.get("parse")->string);
		if (INIFieldParseProc generic = proc(fn))
		{
			CHECK_MESSAGE(f->parse == generic, name << " row " << i << " (" << f->token << ") uses the parser at RW " << row.get("parse")->string);
		}
	}
	CHECK_MESSAGE(i == rows->array.size(), name << ": fewer rows than the binary's table");
}

void compareList(const JsonValue &doc, const char *key, const char *const *mine)
{
	const JsonValue *lists = doc.get("lists");
	REQUIRE(lists != nullptr);
	const JsonValue *list = lists->get(key);
	REQUIRE_MESSAGE(list != nullptr, key);
	size_t i = 0;
	for (const char *const *n = mine; *n; ++n, ++i)
	{
		REQUIRE_MESSAGE(i < list->array.size(), key);
		CHECK_MESSAGE(std::string(*n) == list->array[i].string, key << " entry " << i);
	}
	CHECK_MESSAGE(i == list->array.size(), key);
}
} // namespace

TEST_CASE("draw module tables equal the RotWK tables row by row (names, order, generic parsers) and the name lists equal the binary's")
{
	std::vector<unsigned char> bytes;
	std::string error;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/draw/draw_field_tables.json", bytes, &error), error);
	JsonValue doc;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), doc, &error), error);

	compareTable(doc, "W3DScriptedModelDraw", W3DDrawTables::moduleTable());
	compareTable(doc, "ModelConditionState", W3DDrawTables::modelCondition());
	compareTable(doc, "AnimationState", W3DDrawTables::animationState());
	compareTable(doc, "Animation", W3DDrawTables::animation());
	compareTable(doc, "LodOptions.row", W3DDrawTables::lodOptionsRow());
	compareTable(doc, "W3DHordeModelDraw.LodOptions", W3DDrawTables::hordeLodOptions());

	compareList(doc, "WeaponSlotType", W3DDrawTables::weaponSlotNames());
	compareList(doc, "AnimationState.Flags", W3DDrawTables::animationStateFlagNames());
	compareList(doc, "Animation.AnimationMode", W3DDrawTables::animationModeNames());
	compareList(doc, "ShadowType", W3DDrawTables::shadowTypeNames());
	compareList(doc, "StaticGameLODLevel", W3DDrawTables::staticGameLODNames());
	compareList(doc, "ParticleSysBone.FXTrigger", W3DDrawTables::fxTriggerNames());
	compareList(doc, "ParticleSysBone.Persist", W3DDrawTables::persistNames());
	compareList(doc, "TimeOfDay", W3DDrawTables::timeOfDayNames());
}

TEST_CASE("draw module tables: the module classes use the registered field sets")
{
	// Scripted: the one table (RW 0x4C893A). Horde: LodOptions, then the same table (RW 0x478C74 -> 0x4C893A). Default: no table.
	{
		MultiIniFieldParse p;
		W3DScriptedModelDrawModuleData::buildFieldParse(p);
		CHECK(p.getCount() == 1);
		CHECK(p.getNthFieldParse(0) == W3DDrawTables::moduleTable());
	}
	{
		MultiIniFieldParse p;
		W3DHordeModelDrawModuleData::buildFieldParse(p);
		CHECK(p.getCount() == 2);
		CHECK(p.getNthFieldParse(0) == W3DDrawTables::hordeLodOptions());
		CHECK(p.getNthFieldParse(1) == W3DDrawTables::moduleTable());
	}
	{
		MultiIniFieldParse p;
		W3DDefaultDrawModuleData::buildFieldParse(p);
		CHECK(p.getCount() == 0);
	}
	CHECK(W3DDrawModuleClassFromName("W3DScriptedModelDraw") == W3D_DRAW_SCRIPTED_MODEL);
	CHECK(W3DDrawModuleClassFromName("W3DHordeModelDraw") == W3D_DRAW_HORDE_MODEL);
	CHECK(W3DDrawModuleClassFromName("W3DDefaultDraw") == W3D_DRAW_DEFAULT);
	CHECK(W3DDrawModuleClassFromName("W3DModelDraw") == W3D_DRAW_NONE); // not a registered RotWK name
	CHECK(W3DDrawModuleClassFromName("w3dscriptedmodeldraw") == W3D_DRAW_NONE); // strcmp, case sensitive
}

// ---------------------------------------------------------------------------------------------------------------------
// ModelConditionState
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("ModelConditionState: states start as a copy of the default, then take their own fields")
{
	Harness h;
	W3DModelDrawModuleData *d = h.parse(
		"DefaultModelConditionState\n"
		"  Model = HeroMdl\n"
		"  Skeleton = HeroSkl\n"
		"  WeaponLaunchBone = PRIMARY Arrow01\n"
		"End\n"
		"ModelConditionState = DYING\n"
		"  Model = DeadMdl\n"
		"End\n"
		"ModelConditionState = MOVING ATTACKING\n"
		"  Skeleton = OtherSkl\n"
		"End\n"
		"End\n");
	REQUIRE_MESSAGE(d, h.error);
	REQUIRE(d->m_conditionStates.size() == 3);
	CHECK(d->m_defaultState == 0);
	const ModelConditionInfo &def = d->m_conditionStates[0];
	CHECK(def.conditions.count() == 0);
	CHECK(def.modelNames == std::vector<std::string>{ "HeroMdl" }); // the name is stored as written
	CHECK(def.skeleton == "heroskl");                                // Skeleton is lower-cased
	REQUIRE(def.weaponLaunchBone.size() == 1);
	CHECK(def.weaponLaunchBone[0] == "arrow01");
	CHECK(def.publicBones == std::vector<std::string>{ "arrow01" });

	const ModelConditionInfo &dying = d->m_conditionStates[1];
	CHECK(dying.conditions == flagsOf({ "DYING" }));
	CHECK(dying.modelNames == std::vector<std::string>{ "DeadMdl" });
	CHECK(dying.skeleton == "heroskl"); // inherited
	CHECK(dying.weaponLaunchBone[0] == "arrow01");
	CHECK(dying.publicBones == std::vector<std::string>{ "arrow01" });

	const ModelConditionInfo &mv = d->m_conditionStates[2];
	CHECK(mv.conditions == flagsOf({ "MOVING", "ATTACKING" }));
	CHECK(mv.modelNames == std::vector<std::string>{ "HeroMdl" }); // inherited
	CHECK(mv.skeleton == "otherskl");

	// selection (RW 0x4B4379: the first state in definition order whose conditions are all in the query; else the default):
	// {MOVING, ATTACKING} -> state 2; {MOVING} lacks ATTACKING for state 2 and DYING for state 1 -> the default;
	// {DYING, MOVING} -> state 1 (state 2 would need ATTACKING); {} -> the default (RW 0x4B43A2)
	CHECK(d->findBestInfo(flagsOf({ "MOVING", "ATTACKING" })) == &d->m_conditionStates[2]);
	CHECK(d->findBestInfo(flagsOf({ "MOVING" })) == &d->m_conditionStates[0]);
	CHECK(d->findBestInfo(flagsOf({ "DYING", "MOVING" })) == &d->m_conditionStates[1]);
	CHECK(d->findBestInfo(flagsOf({ "MOVING", "ATTACKING", "DYING" })) == &d->m_conditionStates[1]); // both match: the earlier one wins
	CHECK(d->findBestInfo(flagsOf({})) == &d->m_conditionStates[0]);
	CHECK(d->validate().empty());
}

TEST_CASE("ModelConditionState: Model, ExtraMesh and NONE")
{
	Harness h;
	W3DModelDrawModuleData *d = h.parse(
		"DefaultModelConditionState\n  Model = A\n  Model = B ExtraMesh Yes\nEnd\n"
		"ModelConditionState = DYING\n  Model = A\n  Model = B ExtraMesh Yes\n  Model = C\nEnd\n"
		"ModelConditionState = MOVING\n  Model = None\nEnd\n"
		"ModelConditionState = ATTACKING\n  Model = D ExtraMesh No\nEnd\n"
		"End\n");
	REQUIRE_MESSAGE(d, h.error);
	CHECK(d->m_conditionStates[0].modelNames == std::vector<std::string>{ "A", "B" });
	CHECK(d->m_conditionStates[1].modelNames == std::vector<std::string>{ "C" }); // a plain Model replaces the list
	CHECK(d->m_conditionStates[2].modelNames.empty());                           // Model = NONE: no model (RW 0x4C821F)
	CHECK(d->m_conditionStates[3].modelNames == std::vector<std::string>{ "D" });  // ExtraMesh No does not keep
}

TEST_CASE("ModelConditionState: the retail error cases")
{
	Harness h;
	// RW 0xBE0988
	h.parse("ModelConditionState = MOVING\n  Model = X\nEnd\nEnd\n");
	CHECK(h.error.find("the first ConditionState must be for NONE") != std::string::npos);
	// a Conditions=None first state is fine without a default (it acts as the default)
	W3DModelDrawModuleData *d = h.parse("ModelConditionState = NONE\n  Model = X\nEnd\nEnd\n");
	CHECK_MESSAGE(d, h.error);
	// RW 0xBE0D08
	h.parse(std::string(kBasicDefault) + kBasicDefault + "End\n");
	CHECK(h.error.find("you may have only one default state!") != std::string::npos);
	// RW 0xBE0CE4: DefaultModelConditionState takes no condition words
	h.parse("DefaultModelConditionState MOVING\n  Model = X\nEnd\nEnd\n");
	CHECK(h.error.find("unknown keyword") != std::string::npos);
	// RW 0xBE0C88: the default must be first
	h.parse("ModelConditionState = NONE\n  Model = X\nEnd\n" + std::string(kBasicDefault) + "End\n");
	CHECK(h.error.find("it must be the first state listed") != std::string::npos);
	// RW 0xBE09F0
	h.parse(std::string(kBasicDefault) + "ModelConditionState = NONE\n  Model = X\nEnd\nEnd\n");
	CHECK(h.error.find("you may not specify both a Default state and a Conditions=None state") != std::string::npos);
	h.parse(std::string(kBasicDefault) + "ModelConditionState\n  Model = X\nEnd\nEnd\n"); // no words = no conditions
	CHECK(h.error.find("you may not specify both a Default state and a Conditions=None state") != std::string::npos);
	// RW 0xBE0938
	h.parse(std::string(kBasicDefault) + "ModelConditionState = MOVING\n  Model = X\nEnd\nModelConditionState = MOVING\n  Model = Y\nEnd\nEnd\n");
	CHECK(h.error.find("duplicate condition states are not currently allowed") != std::string::npos);
	// RW 0xBE0A48: every state needs a model (the default has none to give)
	h.parse("DefaultModelConditionState\n  Skeleton = S\nEnd\nEnd\n");
	CHECK(h.error.find("you must specify a model name") != std::string::npos);
	// unknown fields and unknown flag names are errors
	h.parse(std::string(kBasicDefault) + "ModelConditionState = BOGUS\n  Model = X\nEnd\nEnd\n");
	CHECK(h.error.find("Token 'BOGUS' is not a valid member of the index list") != std::string::npos);
	h.parse("DefaultModelConditionState\n  Model = X\n  HideSubObject = A\nEnd\nEnd\n"); // not a RotWK field (spec was wrong)
	CHECK(h.error.find("Unknown field 'HideSubObject'") != std::string::npos);
	// no End
	h.parse("DefaultModelConditionState\n  Model = X\nEnd\n");
	CHECK(h.error.find("Missing") != std::string::npos);
}

TEST_CASE("ModelConditionState: bones, particle systems, FX events, shadow, turrets")
{
	Harness h;
	W3DModelDrawModuleData *d = h.parse(
		"DefaultModelConditionState\n"
		"  Model = M\n"
		"  WeaponFireFXBone = SECONDARY FireBone\n"
		"  WeaponRecoilBone = PRIMARY None\n"
		"  ParticleSysBone = FireBone FireSystem FollowBone: Yes Persist: HOLD PersistID: 3 OnlyIfOnWater: Yes\n"
		"  FXEvent = Frame:12 Name: FX_Step Bone: foot FrameStep: 4 FireWhenSkipped\n"
		"  ShadowSizeX = 99\n"
		"  Shadow = SHADOW_VOLUME\n"
		"  ShadowSizeX = 40\n"
		"  ShadowTexture = decal\n"
		"  ShadowOpacityStart = 5\n"
		"  Texture = Src.tga Dst.tga\n"
		"  Turret = TurretBone\n"
		"  TurretPitch = PitchBone\n"
		"  TurretArtAngle = 90\n"
		"  OverrideTooltip = Tip\n"
		"  RetainSubObjects = Yes\n"
		"End\n"
		"End\n");
	REQUIRE_MESSAGE(d, h.error);
	const ModelConditionInfo &s = d->m_conditionStates[0];
	REQUIRE(s.weaponFireFXBone.size() == 2);
	CHECK(s.weaponFireFXBone[0].empty());
	CHECK(s.weaponFireFXBone[1] == "firebone"); // slot SECONDARY = index 1
	REQUIRE(s.weaponRecoilBone.size() == 1);
	CHECK(s.weaponRecoilBone[0].empty()); // None clears
	REQUIRE(s.particleSysBones.size() == 1);
	CHECK(s.particleSysBones[0].boneName == "firebone");
	CHECK(s.particleSysBones[0].systemName == "FireSystem");
	CHECK(s.particleSysBones[0].followBone);
	CHECK(!s.particleSysBones[0].houseColor);
	CHECK(s.particleSysBones[0].persist == 1); // NONE HOLD KILL SPAWN
	CHECK(s.particleSysBones[0].persistID == 3);
	CHECK(s.particleSysBones[0].onlyIfOnWater);
	REQUIRE(s.fxEvents.size() == 1);
	CHECK(s.fxEvents[0].frame == 12);
	CHECK(s.fxEvents[0].frameStep == 4);
	CHECK(s.fxEvents[0].fxListName == "FX_Step");
	CHECK(s.fxEvents[0].bone == "foot");
	CHECK(s.fxEvents[0].fireWhenSkipped);
	CHECK(s.hasShadow);
	CHECK(s.shadow.type == 2); // SHADOW_VOLUME is entry 1 of RW 0xD99EB0 -> bit 1
	CHECK(s.shadow.sizeX == doctest::Approx(40.0f)); // the 99 before Shadow was skipped, not stored
	CHECK(s.shadow.texture == "decal");
	CHECK(s.shadowOpacityStart == 5u);
	REQUIRE(s.textures.size() == 1);
	CHECK(s.textures[0].first == "Src.tga");
	CHECK(s.textures[0].second == "Dst.tga");
	REQUIRE(s.turrets.size() == 1);
	CHECK(s.turrets[0].angleBone == "turretbone");
	CHECK(s.turrets[0].pitchBone == "pitchbone");
	CHECK(s.turrets[0].artAngle == doctest::Approx(1.5707963f)); // 90 degrees
	CHECK(s.overrideTooltip == "Tip");
	CHECK(s.retainSubObjects);
	// public bones: weapon bone, turret bones, in first-use order (ZH addPublicBone)
	CHECK(s.publicBones == std::vector<std::string>{ "firebone", "turretbone", "pitchbone" });

	// Shadow = NONE creates no shadow and the size fields are then skipped
	d = h.parse("DefaultModelConditionState\n  Model = M\n  Shadow = NONE\n  ShadowSizeX = 7\nEnd\nEnd\n");
	REQUIRE_MESSAGE(d, h.error);
	CHECK(!d->m_conditionStates[0].hasShadow);

	h.parse("DefaultModelConditionState\n  Model = M\n  TurretArtAngle = 90\nEnd\nEnd\n");
	CHECK(h.error.find("TurretArtAngle needs a Turret") != std::string::npos);
	// Texture reads two ascii strings, and an ascii string with no token left is empty, not an error (RW 0x4C2EEC calls
	// RW 0x42EE5E twice): one retail ModelConditionState has a one word Texture line
	d = h.parse("DefaultModelConditionState\n  Model = M\n  Texture = OnlyOne\nEnd\nEnd\n");
	REQUIRE_MESSAGE(d, h.error);
	CHECK(d->m_conditionStates[0].textures[0].first == "OnlyOne");
	CHECK(d->m_conditionStates[0].textures[0].second.empty());
	h.parse("DefaultModelConditionState\n  Model = M\n  WeaponFireFXBone = SIXTH Bone\nEnd\nEnd\n");
	CHECK(h.error.find("Token 'SIXTH' is not a valid member of the index list") != std::string::npos);
	h.parse("DefaultModelConditionState\n  Model = M\n  ParticleSysBone = B S Persist: SOMETIMES\nEnd\nEnd\n");
	CHECK(h.error.find("Token 'SOMETIMES' is not a valid member of the index list") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------------------------------
// AnimationState
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("AnimationState: blocks, defaults, flags, clamping, idle at the front")
{
	Harness h;
	W3DModelDrawModuleData *d = h.parse(
		std::string(kBasicDefault) +
		"AnimationState = MOVING\n"
		"  StateName = STATE_Moving\n"
		"  Animation = RUNA\n"
		"    AnimationName = RunAnim\n"
		"    AnimationMode = LOOP\n"
		"    Distance = 25\n"
		"    AnimationBlendTime = 10\n"
		"    AnimationSpeedFactorRange = 0.9 1.1\n"
		"    AnimationPriority = 500\n"
		"    UseWeaponTiming = Yes\n"
		"    AnimationMustCompleteBlend = Yes\n"
		"    FadeBeginFrame = 3\n"
		"    FadeEndFrame = 9\n"
		"    FadingIn = Yes\n"
		"  End\n"
		"  Animation\n"
		"    AnimationName = RunB\n"
		"    AnimationPriority = -4\n"
		"  End\n"
		"  Flags = RANDOMSTART RESTART_ANIM_WHEN_COMPLETE\n"
		"  ShareAnimation = Yes\n"
		"  EnteringStateFX = FX_Dust\n"
		"  FrameForPristineBonePositions = 7\n"
		"End\n"
		"IdleAnimationState\n"
		"  StateName = STATE_Idle\n"
		"  Animation = IDLA\n"
		"    AnimationName = Idle1 Idle2\n"
		"  End\n"
		"End\n"
		"TransitionState = TRANS_Foo\n"
		"  Animation\n"
		"    AnimationName = None\n"
		"  End\n"
		"  Animation\n"
		"    AnimationName = Trans1\n"
		"    AnimationMode = ONCE_BACKWARDS\n"
		"  End\n"
		"End\n"
		"End\n");
	REQUIRE_MESSAGE(d, h.error);
	REQUIRE(d->m_animationStates.size() == 4);
	// [0] idle (inserted in front), [1] the constructor's <DefaultEmptyIdleAnimationState> (RW 0x4C87DD), then the parsed states

	// IdleAnimationState goes to the FRONT of the list (RW 0x4C88F0), the transition is appended
	const AnimationStateInfo &idle = d->m_animationStates[0];
	CHECK(idle.kind == AnimationStateInfo::KIND_IDLE);
	CHECK(idle.stateName == "STATE_Idle");
	CHECK(!idle.conditions.any());
	REQUIRE(idle.animations.size() == 1);
	CHECK(idle.animations[0].label == "idla");
	CHECK(idle.animations[0].labelOriginal == "IDLA");
	CHECK(idle.animations[0].animationNames == std::vector<std::string>{ "Idle1", "Idle2" }); // the list is kept
	CHECK(idle.animations[0].clipName() == "Idle1");
	CHECK(idle.animations[0].mode == W3D_ANIM_MODE_ONCE); // idle animations default to ONCE (RW 0x4C7AF7)

	const AnimationStateInfo &mv = d->m_animationStates[2];
	CHECK(mv.kind == AnimationStateInfo::KIND_NORMAL);
	CHECK(mv.conditions == flagsOf({ "MOVING" }));
	CHECK(mv.stateName == "STATE_Moving");
	REQUIRE(mv.animations.size() == 2);
	const W3DAnimationInfo &a0 = mv.animations[0];
	CHECK(a0.label == "runa");
	CHECK(a0.animationNames == std::vector<std::string>{ "RunAnim" });
	CHECK(a0.mode == W3D_ANIM_MODE_LOOP);
	CHECK(a0.distance == doctest::Approx(25.0f));
	CHECK(a0.blendTime == doctest::Approx(10.0f));
	CHECK(a0.speedFactorMin == doctest::Approx(0.9f));
	CHECK(a0.speedFactorMax == doctest::Approx(1.1f));
	CHECK(a0.priority == 100); // 500 clamps to 100 (RW 0x4C7B20)
	CHECK(a0.useWeaponTiming);
	CHECK(a0.mustCompleteBlend);
	CHECK(a0.fadeBeginFrame == doctest::Approx(3.0f));
	CHECK(a0.fadeEndFrame == doctest::Approx(9.0f));
	CHECK(a0.fadingIn);
	const W3DAnimationInfo &a1 = mv.animations[1];
	CHECK(a1.label.empty()); // the label word is optional
	CHECK(a1.mode == W3D_ANIM_MODE_LOOP);          // defaults: LOOP, blend 5, speed 1..1, priority 1 (clamped up from -4 to 0)
	CHECK(a1.blendTime == doctest::Approx(5.0f));
	CHECK(a1.speedFactorMin == doctest::Approx(1.0f));
	CHECK(a1.speedFactorMax == doctest::Approx(1.0f));
	CHECK(a1.priority == 0);
	CHECK(a1.fadeBeginFrame == doctest::Approx(-1.0f));
	CHECK(mv.flags == ((1 << W3D_ACF_RANDOMSTART) | (1 << W3D_ACF_RESTART_ANIM_WHEN_COMPLETE)));
	CHECK(mv.flags == 33);
	CHECK(mv.testFlag(W3D_ACF_RESTART_ANIM_WHEN_COMPLETE));
	CHECK(!mv.testFlag(W3D_ACF_START_FRAME_LAST));
	CHECK(mv.shareAnimation);
	CHECK(mv.enteringStateFX == "FX_Dust");
	CHECK(mv.frameForPristineBonePositions == 7);

	const AnimationStateInfo &tr = d->m_animationStates[3];
	CHECK(tr.kind == AnimationStateInfo::KIND_TRANSITION);
	CHECK(tr.stateName == "TRANS_Foo");
	CHECK(!tr.conditions.any());
	REQUIRE(tr.animations.size() == 1); // AnimationName = None is dropped (RW 0x4C7B49)
	CHECK(tr.animations[0].mode == W3D_ANIM_MODE_ONCE_BACKWARDS);

	// matching is RW 0x4B4443: pass 1 the first state with conditions that are all in the query ({MOVING} -> moving); pass 2 the
	// first state without conditions, which is the idle state because parse put it first ({} and {DYING} -> idle, the
	// TransitionState has no conditions either but comes later)
	CHECK(d->findBestAnimationState(flagsOf({ "MOVING" })) == &d->m_animationStates[2]);
	CHECK(d->findBestAnimationState(flagsOf({})) == &d->m_animationStates[0]);
	CHECK(d->findBestAnimationState(flagsOf({ "DYING" })) == &d->m_animationStates[0]);
	// transitions are found by name, case-insensitively, and by no condition
	CHECK(d->findTransitionState("trans_foo") == &d->m_animationStates[3]);
	CHECK(d->findTransitionState("STATE_Idle") == nullptr);
	CHECK(d->findStateByName("state_idle") == &d->m_animationStates[0]);
}

TEST_CASE("AnimationState: BeginScript bodies are stored verbatim and a second one replaces the first")
{
	Harness h;
	W3DModelDrawModuleData *d = h.parse(
		std::string(kBasicDefault) +
		"AnimationState = SELECTED\n"
		"  BeginScript\n"
		"    Prev = CurDrawablePrevAnimationState()\n"
		"    if Prev == \"STATE_Idle\" then CurDrawableSetTransitionAnimState(\"TRANS_X\") end\n"
		"  EndScript\n"
		"  BeginScript\n"
		"    if x then\n"
		"      y()\n"
		"    end\n"
		"  EndScript\n"
		"  StateName = STATE_Selected\n"
		"End\n"
		"End\n");
	REQUIRE_MESSAGE(d, h.error);
	const AnimationStateInfo &s = d->m_animationStates[1];
	CHECK(s.stateName == "STATE_Selected"); // fields after the script are still read
	// a lone "end" line (Lua) does not end the body: only ENDSCRIPT does (RW 0x42D400 compares with INI+0x42C)
	REQUIRE(s.beginScriptLines.size() == 3);
	CHECK(trimmed(s.beginScriptLines[0]) == "if x then");
	CHECK(trimmed(s.beginScriptLines[1]) == "y()");
	CHECK(trimmed(s.beginScriptLines[2]) == "end");
	// concatenated without separators
	CHECK(s.beginScript == s.beginScriptLines[0] + s.beginScriptLines[1] + s.beginScriptLines[2]);
	CHECK(s.beginScript.find("Prev") == std::string::npos);

	h.parse(std::string(kBasicDefault) + "AnimationState = MOVING\n  BeginScript\n    foo()\n");
	CHECK(h.error.find("ENDSCRIPT") != std::string::npos);
	// ENDSCRIPT is matched without case
	d = h.parse(std::string(kBasicDefault) + "AnimationState = MOVING\n  BeginScript\n    foo()\n  endscript\nEnd\nEnd\n");
	CHECK_MESSAGE(d, h.error);
}

TEST_CASE("AnimationState: events and error cases")
{
	Harness h;
	W3DModelDrawModuleData *d = h.parse(
		std::string(kBasicDefault) +
		"AnimationState = MOVING\n"
		"  FXEvent = Frame:2 Name:FX_SplatDust\n"
		"  LuaEvent = Frame: 5 Data: doIt OnStateEnter\n"
		"  ParticleSysBone = DUSTBONE01 FireBuildingSmall\n"
		"  SimilarRestart = Yes\n"
		"  AllowRepeatInRandomPick = Yes\n"
		"End\n"
		"End\n");
	REQUIRE_MESSAGE(d, h.error);
	const AnimationStateInfo &s = d->m_animationStates[1];
	REQUIRE(s.fxEvents.size() == 1);
	CHECK(s.fxEvents[0].frame == 2);
	CHECK(s.fxEvents[0].fxListName == "FX_SplatDust");
	REQUIRE(s.luaEvents.size() == 1);
	CHECK(s.luaEvents[0].frame == 5);
	CHECK(s.luaEvents[0].data == "doIt");
	CHECK(s.luaEvents[0].when == 1);
	REQUIRE(s.particleSysBones.size() == 1);
	CHECK(s.particleSysBones[0].boneName == "dustbone01");
	CHECK(s.similarRestart);
	CHECK(s.allowRepeatInRandomPick);

	h.parse(std::string(kBasicDefault) + "AnimationState = MOVING\n  Bogus = 1\nEnd\nEnd\n");
	CHECK(h.error.find("Unknown field 'Bogus'") != std::string::npos);
	h.parse(std::string(kBasicDefault) + "AnimationState = MOVING\n  Flags = RANDOMSTART PRISTINE_BONE_POS_IN_FINAL_FRAME\nEnd\nEnd\n"); // the ZH bit is not in RotWK
	CHECK(h.error.find("Token 'PRISTINE_BONE_POS_IN_FINAL_FRAME' is not a valid member of the index list") != std::string::npos);
	h.parse(std::string(kBasicDefault) + "AnimationState = MOVING\n  Animation\n    AnimationMode = BACKWARDS\n  End\nEnd\nEnd\n");
	CHECK(h.error.find("Token 'BACKWARDS' is not a valid member of the index list") != std::string::npos);
	h.parse(std::string(kBasicDefault) + "AnimationState = MOVING\n  Animation\n    AnimationSpeedFactorRange = 1\n  End\nEnd\nEnd\n"); // both words required
	CHECK(!h.error.empty());
}

TEST_CASE("AnimationState: the flag bits, the mode numbers and the weapon slots are the binary's")
{
	// RW 0xD99F18: bit i = index i (the ZH PRISTINE_BONE_POS_IN_FINAL_FRAME bit is absent, so RESTART is bit 5 as the BFME1
	// decompile's state flag test uses)
	CHECK(std::string(W3DDrawTables::animationStateFlagNames()[W3D_ACF_RESTART_ANIM_WHEN_COMPLETE]) == "RESTART_ANIM_WHEN_COMPLETE");
	CHECK(std::string(W3DDrawTables::animationStateFlagNames()[W3D_ACF_MAINTAIN_FRAME_ACROSS_STATES]) == "MAINTAIN_FRAME_ACROSS_STATES");
	// RW 0xD99EF8: the stepper's mode numbers (Open-BFME-1 Rva0076C080AdvanceAnimation.cpp:385-411)
	CHECK(std::string(W3DDrawTables::animationModeNames()[W3D_ANIM_MODE_ONCE]) == "ONCE");
	CHECK(std::string(W3DDrawTables::animationModeNames()[W3D_ANIM_MODE_LOOP_PINGPONG]) == "LOOP_PINGPONG");
	CHECK(std::string(W3DDrawTables::animationModeNames()[W3D_ANIM_MODE_PLAY_TO_FRAME]) == "PLAY_TO_FRAME");
	CHECK(std::string(W3DDrawTables::animationModeNames()[W3D_ANIM_MODE_ONCE_BACKWARDS]) == "ONCE_BACKWARDS");
	CHECK(W3D_WEAPONSLOT_COUNT == 5);
}

// ---------------------------------------------------------------------------------------------------------------------
// module level fields, Horde, Default
// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("module fields: scalars, lists, RandomTexture, AttachModel")
{
	Harness h;
	W3DModelDrawModuleData *d = h.parse(
		std::string(kBasicDefault) +
		"OkToChangeModelColor = Yes\n"
		"StaticModelLODMode = Yes\n"
		"UseStandardModelNames = Yes\n"
		"MinLODRequired = High\n"
		"ExtraPublicBone = ARROW_01\n"
		"ExtraPublicBone = ARROW_02 ARROW_03\n"
		"TrackMarks = EXTracks.tga\n"
		"InitialRecoilSpeed = 100\n"
		"WadingParticleSys = WaterRipplesTrail\n"
		"AlphaCameraFadeOuterRadius = 200\n"
		"AlphaCameraFadeInnerRadius = 100\n"
		"AlphaCameraAtInnerRadius = 25%\n"
		"AlphaRefRange = X:1 Y:2\n"
		"StaticSortLevelWhileFading = 7\n"
		"BirthFadeTime = 1000\n"
		"ProjectileBoneFeedbackEnabledSlots = PRIMARY TERTIARY\n"
		"DependencySharedModelFlags = MOVING ATTACKING\n"
		"RandomTexture = A.tga 0 B.tga\n"
		"RandomTexture = a.tga 5 C.tga\n"
		"RandomTexture = Z.tga 1\n"
		"AttachModel\n"
		"  Bone = BONE01\n"
		"  Offset = X:1 Y:2 Z:3\n"
		"  Model = Arrow prob:30\n"
		"  Model = Arrow2\n"
		"End\n"
		"End\n");
	REQUIRE_MESSAGE(d, h.error);
	CHECK(d->m_okToChangeModelColor);
	CHECK(d->m_staticModelLODMode);
	CHECK(d->m_useStandardModelNames);
	CHECK(d->m_minLODRequired == 2); // Low Medium High
	CHECK(d->m_extraPublicBones == std::vector<std::string>{ "ARROW_01", "ARROW_02", "ARROW_03" });
	CHECK(d->m_trackFile == "extracks.tga");
	CHECK(d->m_initialRecoil == doctest::Approx(20.0f)); // 100 / 5 (velocity: per second -> per logic frame, 0.2f)
	CHECK(d->m_wadingParticleSys == "WaterRipplesTrail");
	CHECK(d->m_alphaCameraFadeOuterRadius == doctest::Approx(200.0f));
	CHECK(d->m_alphaCameraFadeInnerRadius == doctest::Approx(100.0f));
	CHECK(d->m_alphaCameraAtInnerRadius == doctest::Approx(0.25f));
	CHECK(d->m_alphaRefRangeX == 1);
	CHECK(d->m_alphaRefRangeY == 2);
	CHECK(d->m_staticSortLevelWhileFading == 7);
	CHECK(d->m_birthFadeTime == 5u); // 1000 ms = 5 logic frames
	CHECK(d->m_projectileBoneFeedbackEnabledSlots == 5); // PRIMARY bit 0 + TERTIARY bit 2
	CHECK(d->m_dependencySharedModelFlags == flagsOf({ "MOVING", "ATTACKING" }));
	REQUIRE(d->m_randomTextures.size() == 2); // A.tga and a.tga share one record
	CHECK(d->m_randomTextures[0].name == "A.tga");
	REQUIRE(d->m_randomTextures[0].entries.size() == 2);
	CHECK(d->m_randomTextures[0].entries[0].replacement == "B.tga");
	CHECK(d->m_randomTextures[0].entries[1].weight == 5);
	CHECK(d->m_randomTextures[1].name == "Z.tga");
	CHECK(d->m_randomTextures[1].entries[0].replacement.empty());
	REQUIRE(d->m_attachModels.size() == 1);
	CHECK(d->m_attachModels[0].bone == "BONE01");
	CHECK(d->m_attachModels[0].offset.z == doctest::Approx(3.0f));
	REQUIRE(d->m_attachModels[0].models.size() == 2);
	CHECK(d->m_attachModels[0].models[0].second == 30);
	CHECK(d->m_attachModels[0].models[1].second == 70); // unassigned entries share what is left of 100

	h.parse(std::string(kBasicDefault) + "AlphaCameraFadeOuterRadius = 0\nEnd\n");
	CHECK(h.error.find("invalid Real value") != std::string::npos);
	h.parse(std::string(kBasicDefault) + "MinLODRequired = Ultra\nEnd\n");
	CHECK(h.error.find("Token 'Ultra' is not a valid member of the index list") != std::string::npos);
	h.parse(std::string(kBasicDefault) + "AttachModel\n  Bone = B1\n  Model = A prob:60\n  Model = B prob:60\nEnd\nEnd\n");
	CHECK(h.error.find("combined probability may not be higher than 100") != std::string::npos);
	h.parse(std::string(kBasicDefault) + "AttachModel\n  Model = A\nEnd\nEnd\n");
	CHECK(h.error.find("you must specify then bone name") != std::string::npos);
}

TEST_CASE("W3DHordeModelDraw LodOptions")
{
	Harness h;
	W3DModelDrawModuleData *base = h.parse(
		std::string(kBasicDefault) +
		"LodOptions = MEDIUM\n"
		"  AllowMultipleModels = Yes\n"
		"  MaxRandomTextures = 3\n"
		"  MaxRandomAnimations = 4\n"
		"  MaxAnimFrameDelta = 1.5\n"
		"  RandomStartFramePercent = 50\n"
		"End\n"
		"LodOptions = low\n"
		"  MaxRandomAnimations = 2\n"
		"End\n"
		"End\n",
		W3D_DRAW_HORDE_MODEL);
	REQUIRE_MESSAGE(base, h.error);
	W3DHordeModelDrawModuleData *d = dynamic_cast<W3DHordeModelDrawModuleData *>(base);
	REQUIRE(d != nullptr);
	CHECK(d->m_lodOptions[1].allowMultipleModels);
	CHECK(d->m_lodOptions[1].maxRandomTextures == 3);
	CHECK(d->m_lodOptions[1].maxRandomAnimations == 4);
	CHECK(d->m_lodOptions[1].maxAnimFrameDelta == doctest::Approx(1.5f));
	CHECK(d->m_lodOptions[1].randomStartFramePercent == 50);
	CHECK(d->m_lodOptions[0].maxRandomAnimations == 2); // LOW matches without case (stricmp)
	// the rows keep the constructor defaults of RW 0x478289 for every field the INI does not set
	CHECK(d->m_lodOptions[2].maxRandomAnimations == 999);
	CHECK(d->m_lodOptions[2].maxRandomTextures == 999);
	CHECK(d->m_lodOptions[2].allowMultipleModels);
	CHECK(d->m_lodOptions[2].maxAnimFrameDelta == doctest::Approx(0.5f));
	CHECK(d->m_lodOptions[2].randomStartFramePercent == 100);
	CHECK(d->m_lodOptions[0].maxRandomTextures == 1);
	CHECK(!d->m_lodOptions[0].allowMultipleModels);
	CHECK(d->m_lodOptions[0].maxAnimFrameDelta == doctest::Approx(15.0f));
	CHECK(d->m_lodOptions[0].randomStartFramePercent == 0);
	CHECK(d->m_lodOptions[1].randomStartFramePercent == 50); // set above
	{
		W3DHordeModelDrawModuleData fresh;
		CHECK(fresh.m_lodOptions[1].maxRandomAnimations == 2);
		CHECK(fresh.m_lodOptions[1].maxAnimFrameDelta == doctest::Approx(8.0f));
		CHECK(fresh.m_lodOptions[1].randomStartFramePercent == 50);
	}

	h.parse(std::string(kBasicDefault) + "LodOptions = ULTRA\nEnd\nEnd\n", W3D_DRAW_HORDE_MODEL);
	CHECK(h.error.find("Expected LOW, MEDIUM, or HIGH") != std::string::npos);
	// W3DScriptedModelDraw has no LodOptions (RW 0x4C893A adds only the one table)
	h.parse(std::string(kBasicDefault) + "LodOptions = LOW\nEnd\nEnd\n", W3D_DRAW_SCRIPTED_MODEL);
	CHECK(h.error.find("Unknown field 'LodOptions'") != std::string::npos);
}

TEST_CASE("W3DDefaultDraw takes no fields")
{
	Harness h;
	W3DDefaultDrawModuleData *d = h.parseDefault("End\n");
	CHECK_MESSAGE(d, h.error);
	h.parseDefault("OkToChangeModelColor = Yes\nEnd\n");
	CHECK(h.error.find("Unknown field 'OkToChangeModelColor'") != std::string::npos);
}

TEST_CASE("selection rules (RW 0x4B4379 / 0x4B4443): the first state in definition order whose conditions are in the query wins, not the best match")
{
	Harness h;
	// model states: 0 = NONE (model Z), 1 = {MOVING, ATTACKING} (A), 2 = {MOVING} (B)
	W3DModelDrawModuleData *d = h.parse(
		"ModelConditionState = NONE\n  Model = Z\nEnd\n"
		"ModelConditionState = MOVING ATTACKING\n  Model = A\nEnd\n"
		"ModelConditionState = MOVING\n  Model = B\nEnd\n"
		"End\n");
	REQUIRE_MESSAGE(d, h.error);
	CHECK(d->m_defaultState == -1);
	auto model = [&](std::initializer_list<const char *> names) { return d->findBestInfo(flagsOf(names))->modelName(); };
	CHECK(model({ "MOVING", "ATTACKING" }) == "A"); // state 1 is the first whose conditions are all in the query
	CHECK(model({ "MOVING" }) == "B");              // state 1 needs ATTACKING; state 2 matches
	CHECK(model({ "MOVING", "ATTACKING", "DYING" }) == "A"); // extra bits in the query are ignored
	CHECK(model({}) == "Z");                        // empty query, no default: the first state without conditions (RW 0x4B43E1)
	CHECK(model({ "DYING" }) == "Z");               // nothing matches, no default: the last state without conditions (RW 0x4B43F4)

	// the same states in the other order: B comes first and matches both queries. A best-match rule (ZH SparseMatchFinder)
	// would have chosen A for {MOVING, ATTACKING}.
	d = h.parse(
		"ModelConditionState = NONE\n  Model = Z\nEnd\n"
		"ModelConditionState = MOVING\n  Model = B\nEnd\n"
		"ModelConditionState = MOVING ATTACKING\n  Model = A\nEnd\n"
		"End\n");
	REQUIRE_MESSAGE(d, h.error);
	CHECK(d->findBestInfo(flagsOf({ "MOVING", "ATTACKING" }))->modelName() == "B");

	// with a default state: it answers an empty query at once and every query nothing matches
	d = h.parse("DefaultModelConditionState\n  Model = D\nEnd\nModelConditionState = MOVING\n  Model = B\nEnd\nEnd\n");
	REQUIRE_MESSAGE(d, h.error);
	CHECK(d->findBestInfo(flagsOf({}))->modelName() == "D");
	CHECK(d->findBestInfo(flagsOf({ "DYING" }))->modelName() == "D");
	CHECK(d->findBestInfo(flagsOf({ "MOVING", "DYING" }))->modelName() == "B");

	// animation states: pass 1 first subset in order, pass 2 first state without conditions. The constructor's
	// <DefaultEmptyIdleAnimationState> (RW 0x4C87DD) is first in the list, so a TransitionState never becomes the fallback.
	d = h.parse(
		std::string(kBasicDefault) +
		"TransitionState = TRANS_A\n  Animation\n    AnimationName = T\n  End\nEnd\n"
		"AnimationState = MOVING ATTACKING\n  StateName = S_MA\nEnd\n"
		"AnimationState = MOVING\n  StateName = S_M\nEnd\n"
		"End\n");
	REQUIRE_MESSAGE(d, h.error);
	CHECK(d->findBestAnimationState(flagsOf({ "MOVING", "ATTACKING" }))->stateName == "S_MA");
	CHECK(d->findBestAnimationState(flagsOf({ "MOVING" }))->stateName == "S_M");
	CHECK(d->findBestAnimationState(flagsOf({}))->stateName == "<DefaultEmptyIdleAnimationState>"); // no idle state
	CHECK(d->findBestAnimationState(flagsOf({ "DYING" }))->stateName == "<DefaultEmptyIdleAnimationState>");
	// with an IdleAnimationState (parse puts it first) the idle state is the fallback
	d = h.parse(
		std::string(kBasicDefault) +
		"TransitionState = TRANS_A\n  Animation\n    AnimationName = T\n  End\nEnd\n"
		"IdleAnimationState\n  StateName = S_IDLE\nEnd\n"
		"End\n");
	REQUIRE_MESSAGE(d, h.error);
	CHECK(d->findBestAnimationState(flagsOf({}))->stateName == "S_IDLE");
	W3DModelDrawModuleData empty;
	CHECK(empty.findBestAnimationState(flagsOf({}))->stateName == "<DefaultEmptyIdleAnimationState>");
	CHECK(empty.findBestInfo(flagsOf({})) == nullptr);
}

TEST_CASE("factory: the three draw classes bind as typed data through ModuleFactory::bindTypedData and parse through newModuleDataFromINI")
{
	objtest::World w;
	w.modules.bindTypedData<W3DScriptedModelDrawModuleData>("W3DScriptedModelDraw", MODULETYPE_DRAW);
	w.modules.bindTypedData<W3DHordeModelDrawModuleData>("W3DHordeModelDraw", MODULETYPE_DRAW);
	w.modules.bindTypedData<W3DDefaultDrawModuleData>("W3DDefaultDraw", MODULETYPE_DRAW);
	CHECK(w.modules.typedCount() == 3);
	const std::string text =
		"Object Foo\n"
		"  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
		"    DefaultModelConditionState\n      Model = FOOMDL\n    End\n"
		"    IdleAnimationState\n      StateName = STATE_Idle\n    End\n"
		"  End\n"
		"End\n"
		"Object Bar\n"
		"  Draw = W3DHordeModelDraw ModuleTag_Draw\n"
		"    LodOptions = MEDIUM\n      MaxRandomAnimations = 3\n    End\n"
		"    DefaultModelConditionState\n      Model = BARMDL\n    End\n"
		"  End\n"
		"End\n"
		"Object Baz\n"
		"  Draw = W3DDefaultDraw ModuleTag_Draw\n"
		"  End\n"
		"End\n";
	REQUIRE(w.load(text).empty());
	{
		const auto &n = w.get("Foo")->drawModules().nuggets();
		REQUIRE(n.size() == 1);
		const W3DScriptedModelDrawModuleData *d = dynamic_cast<const W3DScriptedModelDrawModuleData *>(n[0].data.get());
		REQUIRE(d != nullptr);
		CHECK(dynamic_cast<const W3DHordeModelDrawModuleData *>(n[0].data.get()) == nullptr);
		REQUIRE(d->m_conditionStates.size() == 1);
		CHECK(d->m_conditionStates[0].modelName() == "FOOMDL");
		CHECK(d->m_animationStates[0].stateName == "STATE_Idle");
		CHECK(n[0].interfaceMask == MODULEINTERFACE_DRAW); // the registry's mask still applies
	}
	{
		const auto &n = w.get("Bar")->drawModules().nuggets();
		REQUIRE(n.size() == 1);
		const W3DHordeModelDrawModuleData *d = dynamic_cast<const W3DHordeModelDrawModuleData *>(n[0].data.get());
		REQUIRE(d != nullptr);
		CHECK(d->m_lodOptions[1].maxRandomAnimations == 3);
		CHECK(d->m_lodOptions[2].maxRandomAnimations == 999); // constructor default (RW 0x478289)
	}
	{
		const auto &n = w.get("Baz")->drawModules().nuggets();
		REQUIRE(n.size() == 1);
		CHECK(dynamic_cast<const W3DDefaultDrawModuleData *>(n[0].data.get()) != nullptr);
	}
	// the Scripted table has no LodOptions, the Default one has no fields: errors come from the typed tables
	CHECK(objtest::contains(w.load("Object Q1\n  Draw = W3DScriptedModelDraw T\n    LodOptions = LOW\n    End\n  End\nEnd\n"), "Unknown field 'LodOptions'"));
	CHECK(objtest::contains(w.load("Object Q2\n  Draw = W3DDefaultDraw T\n    Nope = 1\n  End\nEnd\n"), "Unknown field 'Nope'"));
}
