// OpenBFME. FX-4 tests: TransitionDamageFX (RW 0x8BE077) and the damage-state dispatch of ActiveBody (RW 0x8C4389 / 0x8C3131), the camera shake (RW 0x486D86).
// Synthetic data (combattest::CombatWorld), no retail files; the binary facts the port relies on are read from the image when RW_GAME_DAT is set.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "PeImage.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "RetailTestMount.h"

#include "Common/Thing/RwGrammar.h"
#include "GameClient/CameraSettings.h"
#include "GameClient/TacticalCamera.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/TransitionDamageFX.h"

#include <string>
#include <vector>

using namespace combattest;
using objtest::contains;
using retailtest::PeImage;

namespace
{
// the shape of the retail blocks (ithilienbuildings.ini, moriamodules.ini, the castle doors): offsets, a bone entry, the type masks, the sub object lists
const char *kProbe = "Object TdfxProbe\n"
					 "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n    MaxHealthDamaged = 75\n    MaxHealthReallyDamaged = 50\n  End\n"
					 "  Behavior = TransitionDamageFX ModuleTag_Tdfx\n"
					 "    DamageFXTypes = ALL -WATER\n"
					 "    DamagedFXList1 = Loc: X:10 Y:0 Z:5 FXList:FX_Damaged\n"
					 "    DamagedParticleSystem2 = Loc: X:1 Y:2 Z:3 PSys:SmokeDamaged\n"
					 "    ReallyDamagedFXList1 = Loc: X:0 Y:0 Z:0 FXList:FX_ReallyDamaged\n"
					 "    ReallyDamagedParticleSystem1 = Bone:None RandomBone:No PSys:FireBuildingLarge\n"
					 "    RubbleFXList3 = Loc: X:0 Y:0 Z:0 FXList:None\n"
					 "    PristineShowSubObject = DOOR\n    PristineHideSubObject = DOOR_D1 DOOR_D2\n"
					 "    DamagedShowSubObject = DOOR_D1\n    DamagedHideSubObject = DOOR DOOR_D2\n"
					 "  End\n"
					 "  Geometry = CYLINDER\n  GeometryMajorRadius = 10\n  GeometryHeight = 10\n"
					 "End\n"
					 "Object TdfxBones\n"
					 "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n    MaxHealthDamaged = 75\n    MaxHealthReallyDamaged = 50\n  End\n"
					 "  Behavior = TransitionDamageFX ModuleTag_Tdfx\n"
					 "    DamagedFXList1 = Bone:FIRE RandomBone:Yes FXList:FX_Damaged\n"
					 "    DamagedFXList2 = Bone:SMOKE RandomBone:No FXList:FX_Damaged\n"
					 "  End\n"
					 "  Geometry = CYLINDER\n  GeometryMajorRadius = 10\n  GeometryHeight = 10\n"
					 "End\n";

struct FxWorld : CombatWorld
{
	FxWorld()
	{
		TransitionDamageFXModule::registerClass(w.modules);
		const std::string err = w.load(kProbe, INI_LOAD_OVERWRITE, "fx4.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	const TransitionDamageFXModuleData *data(const char *name)
	{
		for (const auto &n : w.get(name)->behaviorModules().nuggets())
		{
			if (n.name == "TransitionDamageFX")
			{
				return dynamic_cast<const TransitionDamageFXModuleData *>(n.data.get());
			}
		}
		return nullptr;
	}
	std::string loadError(const std::string &body)
	{
		return w.load("Object Bad\n  Behavior = TransitionDamageFX T\n    " + body + "\n  End\nEnd\n", INI_LOAD_OVERWRITE, "bad.ini");
	}
	std::vector<FXEvent> events() const { return logic->fxEvents().frameEvents(); }
};

DamageInfo hit(float amount, int type)
{
	DamageInfo d;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	return d;
}

std::vector<const FXEvent *> bySite(const std::vector<FXEvent> &all, FXEvent::Kind kind)
{
	std::vector<const FXEvent *> out;
	for (const FXEvent &e : all)
	{
		if (std::string(e.site) == "TransitionDamageFX" && e.kind == kind)
		{
			out.push_back(&e);
		}
	}
	return out;
}

class SubObjectHooks : public ObjectClientHooks
{
public:
	std::vector<std::string> calls;
	void objectCreated(Object &) override {}
	void objectDestroyed(Object &) override {}
	void showSubObject(Object &, const std::string &name, bool visible, bool permanent) override
	{
		calls.push_back(name + (visible ? " show" : " hide") + (permanent ? " permanent" : ""));
	}
};

// three FIRE bones and one SMOKE bone, model space
class FakeBones : public ProjectileLaunchOffsets
{
public:
	bool launchOffset(const Object &, int, int, float[12]) override { return false; }
	bool singleLogicalBone(const Object &, const std::string &bone, float out[12]) override
	{
		if (bone != "SMOKE")
		{
			return false;
		}
		set(out, 7.0f, 8.0f, 9.0f);
		return true;
	}
	int multiLogicalBones(const Object &, const std::string &prefix, const std::array<std::uint32_t, 19> &, int maxBones, float (*out)[12]) override
	{
		if (prefix != "FIRE")
		{
			return 0;
		}
		const int n = maxBones < 3 ? maxBones : 3;
		for (int i = 0; i < n; ++i)
		{
			set(out[i], 100.0f * (float)(i + 1), 0.0f, 1.0f);
		}
		return n;
	}

private:
	static void set(float m[12], float x, float y, float z)
	{
		for (int i = 0; i < 12; ++i)
		{
			m[i] = (i == 0 || i == 5 || i == 10) ? 1.0f : 0.0f;
		}
		m[3] = x;
		m[7] = y;
		m[11] = z;
	}
};
} // namespace

TEST_CASE("fx4 parse: TransitionDamageFX's field table is the binary's (RW 0xC08D10, 120 rows, in order)")
{
	FxWorld w;
	MultiIniFieldParse mine;
	TransitionDamageFXModuleData::buildFieldParse(mine);
	REQUIRE(mine.getCount() == 1);
	const FieldParse *binary = w.w.grammar.fieldParse("0xc08d10");
	REQUIRE(binary != nullptr);
	std::vector<std::string> a, b;
	for (const FieldParse *f = mine.getNthFieldParse(0); f->token; ++f)
	{
		a.push_back(f->token);
	}
	for (const FieldParse *f = binary; f->token; ++f)
	{
		b.push_back(f->token);
	}
	CHECK(a.size() == 120); // 3 masks, 3 x 3 x 12 entries, RubbleNeighbor, 8 sub object lists (golden field-tables.json)
	CHECK(a == b);
}

TEST_CASE("fx4 parse: the entries, the locations, the type masks and the sub object lists (RW 0x8BDA75, 0x73A5C9, 0x42E59E)")
{
	FxWorld w;
	const TransitionDamageFXModuleData *d = w.data("TdfxProbe");
	REQUIRE(d != nullptr);
	// ALL -WATER: the mask starts at ALL, WATER (index 9 of RW 0xDA3960) clears bit 9 - 1
	CHECK(d->m_damageFXTypes == 0xFFFFFEFFu);
	CHECK(d->m_damageOCLTypes == 0xFFFFFFFFu);
	CHECK(d->m_damageParticleTypes == 0xFFFFFFFFu);
	const TransitionDamageFXModuleData::Entry &fx = d->m_fxList[BODY_DAMAGED][0];
	CHECK(fx.name == "FX_Damaged");
	CHECK(fx.loc.type == TransitionDamageFXModuleData::LOC_OFFSET);
	CHECK(fx.loc.offset.x == 10.0f);
	CHECK(fx.loc.offset.y == 0.0f);
	CHECK(fx.loc.offset.z == 5.0f);
	CHECK(d->m_particleSystem[BODY_DAMAGED][1].name == "SmokeDamaged");
	const TransitionDamageFXModuleData::Entry &ps = d->m_particleSystem[BODY_REALLYDAMAGED][0];
	CHECK(ps.name == "FireBuildingLarge");
	CHECK(ps.loc.type == TransitionDamageFXModuleData::LOC_BONE);
	CHECK(ps.loc.boneName == "None");
	CHECK_FALSE(ps.loc.randomBone);
	// FXList:None stores NULL (RW 0x73A302)
	CHECK(d->m_fxList[BODY_RUBBLE][2].name.empty());
	// no Pristine rows exist: state 0 stays empty
	for (int i = 0; i < TransitionDamageFXModuleData::MAX_FX; ++i)
	{
		CHECK(d->m_fxList[BODY_PRISTINE][i].name.empty());
	}
	CHECK(d->m_showSubObjects[BODY_PRISTINE] == std::vector<std::string>{ "DOOR" });
	CHECK(d->m_hideSubObjects[BODY_PRISTINE] == std::vector<std::string>{ "DOOR_D1", "DOOR_D2" });
	CHECK(d->m_hideSubObjects[BODY_DAMAGED] == std::vector<std::string>{ "DOOR", "DOOR_D2" });
	CHECK(TransitionDamageFXModuleData::damageTypeAllowed(d->m_damageFXTypes, DAMAGE_FLAME));
	CHECK_FALSE(TransitionDamageFXModuleData::damageTypeAllowed(d->m_damageFXTypes, DAMAGE_WATER));
	// FORCE (0) tests bit (0 - 1) & 31 = 31
	CHECK(TransitionDamageFXModuleData::damageTypeAllowed(0x80000000u, DAMAGE_FORCE));
	const TransitionDamageFXModuleData *bones = w.data("TdfxBones");
	REQUIRE(bones != nullptr);
	CHECK(bones->m_fxList[BODY_DAMAGED][0].loc.boneName == "FIRE");
	CHECK(bones->m_fxList[BODY_DAMAGED][0].loc.randomBone);
}

TEST_CASE("fx4 parse: retail's errors (RW 0x8BDA75, 0x8BDBB5, 0x8BDC8C, 0x73A5C9, 0x8BE960)")
{
	FxWorld w;
	CHECK(contains(w.loadError("DamagedFXList1 = Where: X:0 Y:0 Z:0 FXList:FX_A"), "'loc' or 'bone' expected"));
	CHECK(contains(w.loadError("DamagedFXList1 = Bone:B Random:No FXList:FX_A"), "parseFXLocInfo: Bone name not followed by RandomBone specifier."));
	CHECK(contains(w.loadError("DamagedFXList1 = Loc: X:0 Y:0 Z:0 OCL:FX_A"), "'fxlist' expected"));
	CHECK(contains(w.loadError("DamagedParticleSystem1 = Loc: X:0 Y:0 Z:0 FXList:Smoke"), "'psys' expected"));
	CHECK(contains(w.loadError("DamagedOCL1 = Loc: X:0 Y:0 Z:0 PSys:OCL_A"), "'ocl' expected"));
	CHECK(contains(w.loadError("DamageFXTypes = WATER"), "ALL, NONE, + or - expected"));
	CHECK(contains(w.loadError("RubbleNeighbor NeighborOffset: X:0 Y:200 Z:0 Wall:Left"),
		"bad colon spacing, or unexpected token in TransitionDamageFXModuleData::NeighborIDSubobjectNameDataAppend"));
	// the retail RubbleNeighbor line parses (one of them in the retail data)
	CHECK(w.loadError("RubbleNeighbor NeighborOffset: X:0 Y:-200 Z:0 SubObject:BookendLeft SubObject:BookendLeftA OCL:OCL_None").empty());
}

TEST_CASE("fx4 runtime: a hit that makes the state worse plays the new state's FXLists at the object, attaches its systems, applies its sub objects")
{
	FxWorld w;
	Object *o = w.unit("TdfxProbe", 'B', 400, 300);
	REQUIRE(o != nullptr);
	SubObjectHooks hooks;
	o->friend_bindToClient(&hooks);
	const Coord3D at = *o->getPosition();
	DamageInfo first = hit(30.0f, DAMAGE_FLAME); // 100 -> 70: DAMAGED
	o->attemptDamage(first);
	REQUIRE(o->getBodyModule()->getDamageState() == BODY_DAMAGED);
	std::vector<FXEvent> ev = w.events();
	std::vector<const FXEvent *> fx = bySite(ev, FXEvent::POSITION_FX);
	REQUIRE(fx.size() == 1);
	CHECK(*fx[0]->fxList == "FX_Damaged");
	// Loc X:10 Y:0 Z:5 through the object's transform (RW 0x70BCE7; the object faces angle 0)
	CHECK(fx[0]->position.x == at.x + 10.0f);
	CHECK(fx[0]->position.y == at.y);
	CHECK(fx[0]->position.z == at.z + 5.0f);
	CHECK_FALSE(fx[0]->hasTransform); // RW 0x494615(fx, pos, NULL, 0, NULL)
	std::vector<const FXEvent *> attach = bySite(ev, FXEvent::ATTACH_PARTICLE_SYSTEM);
	REQUIRE(attach.size() == 1);
	CHECK(*attach[0]->fxList == "SmokeDamaged");
	CHECK(attach[0]->position.x == 1.0f); // model space (RW 0x5F2F2D before attachToObject)
	CHECK(attach[0]->position.y == 2.0f);
	CHECK(attach[0]->position.z == 3.0f);
	const std::uint32_t damagedSlot = attach[0]->slot;
	CHECK(bySite(ev, FXEvent::DESTROY_PARTICLE_SYSTEM).empty()); // PRISTINE had no systems
	// the new state's lists: Hide first, then Show, permanent (RW 0x8BDCF7)
	CHECK(hooks.calls == std::vector<std::string>{ "DOOR hide permanent", "DOOR_D2 hide permanent", "DOOR_D1 show permanent" });

	DamageInfo second = hit(30.0f, DAMAGE_FLAME); // 70 -> 40: REALLYDAMAGED
	const size_t before = w.events().size();
	o->attemptDamage(second);
	ev = w.events();
	ev.erase(ev.begin(), ev.begin() + (std::ptrdiff_t)before);
	std::vector<const FXEvent *> destroy = bySite(ev, FXEvent::DESTROY_PARTICLE_SYSTEM);
	REQUIRE(destroy.size() == 1); // the old state's slot (RW 0x5F5379)
	CHECK(destroy[0]->slot == damagedSlot);
	fx = bySite(ev, FXEvent::POSITION_FX);
	REQUIRE(fx.size() == 1);
	CHECK(*fx[0]->fxList == "FX_ReallyDamaged");
	attach = bySite(ev, FXEvent::ATTACH_PARTICLE_SYSTEM);
	REQUIRE(attach.size() == 1);
	CHECK(*attach[0]->fxList == "FireBuildingLarge");
	// Bone:None is not a bone of the (provider-less) object: the offset (0, 0, 0)
	CHECK(attach[0]->position.x == 0.0f);
	CHECK(attach[0]->position.z == 0.0f);
	CHECK(attach[0]->slot != damagedSlot);
	o->friend_bindToClient(nullptr);
}

TEST_CASE("fx4 runtime: a source-less frame-zero hit retains the initial damage record")
{
	FxWorld w;
	Object *o = w.unit("TdfxProbe", 'B', 400, 300);
	REQUIRE(o != nullptr);
	REQUIRE(w.logic->getFrame() == 0);
	DamageInfo water = hit(30.0f, DAMAGE_WATER);
	o->attemptDamage(water);
	int type = -1;
	REQUIRE(o->getBodyModule()->lastDamageRecordType(type));
	CHECK(type == DAMAGE_UNDEFINED); // -1 equals frame - 1 with unsigned wrap; the missing source retains the old record
	CHECK(bySite(w.events(), FXEvent::POSITION_FX).size() == 1); // UNDEFINED is not excluded by ALL -WATER
}

TEST_CASE("fx4 runtime: the damage type of the hit filters the entries (Damage*Types), the record is written before the modules hear of the change")
{
	FxWorld w;
	Object *o = w.unit("TdfxProbe", 'B', 400, 300);
	REQUIRE(o != nullptr);
	// At frame zero a source-less hit retains the initial frame -1 record (RW 0x8C4294 .. 0x8C42CB).
	// Advance one frame so this test exercises the damage-type filter with a freshly copied record.
	w.frames(1);
	DamageInfo water = hit(30.0f, DAMAGE_WATER); // retail copies it into the record (RW 0x7441E0) before the loop RW 0x8C4395
	o->attemptDamage(water);
	REQUIRE(o->getBodyModule()->getDamageState() == BODY_DAMAGED);
	std::vector<FXEvent> ev = w.events();
	CHECK(bySite(ev, FXEvent::POSITION_FX).empty());                // DamageFXTypes = ALL -WATER
	CHECK(bySite(ev, FXEvent::ATTACH_PARTICLE_SYSTEM).size() == 1); // DamageParticleTypes = ALL
	int type = -1;
	REQUIRE(o->getBodyModule()->lastDamageRecordType(type));
	CHECK(type == DAMAGE_WATER);
}

TEST_CASE("fx4 runtime: nothing while ACTIVELY_BEING_CONSTRUCTED (model condition 0x45, RW 0x8BE08B), nothing on a better state but its sub objects and the old systems")
{
	FxWorld w;
	CHECK(CombatNames::modelCondition("ACTIVELY_BEING_CONSTRUCTED") == 0x45);
	Object *o = w.unit("TdfxProbe", 'B', 400, 300);
	REQUIRE(o != nullptr);
	o->setModelConditionState(0x45, true);
	DamageInfo first = hit(30.0f, DAMAGE_FLAME);
	o->attemptDamage(first);
	REQUIRE(o->getBodyModule()->getDamageState() == BODY_DAMAGED);
	CHECK(bySite(w.events(), FXEvent::POSITION_FX).empty());
	CHECK(bySite(w.events(), FXEvent::ATTACH_PARTICLE_SYSTEM).empty());
	o->setModelConditionState(0x45, false);
	DamageInfo second = hit(30.0f, DAMAGE_FLAME);
	o->attemptDamage(second);
	REQUIRE(o->getBodyModule()->getDamageState() == BODY_REALLYDAMAGED);
	REQUIRE(bySite(w.events(), FXEvent::ATTACH_PARTICLE_SYSTEM).size() == 1);
	// healing back to DAMAGED (RW 0x8C3131): the REALLYDAMAGED systems go, no FX (the state is better)
	SubObjectHooks hooks;
	o->friend_bindToClient(&hooks);
	const size_t before = w.events().size();
	o->attemptHealing(20.0f, nullptr);
	REQUIRE(o->getBodyModule()->getDamageState() == BODY_DAMAGED);
	std::vector<FXEvent> ev = w.events();
	ev.erase(ev.begin(), ev.begin() + (std::ptrdiff_t)before);
	CHECK(bySite(ev, FXEvent::DESTROY_PARTICLE_SYSTEM).size() == 1);
	CHECK(bySite(ev, FXEvent::POSITION_FX).empty());
	CHECK(bySite(ev, FXEvent::ATTACH_PARTICLE_SYSTEM).empty());
	CHECK(hooks.calls == std::vector<std::string>{ "DOOR hide permanent", "DOOR_D2 hide permanent", "DOOR_D1 show permanent" });
	o->friend_bindToClient(nullptr);
}

TEST_CASE("fx4 dispatch: a health change outside attemptDamage / attemptHealing tells no damage module (RW 0x8C2A5D makes no such call)")
{
	FxWorld w;
	Object *o = w.unit("TdfxProbe", 'B', 400, 300);
	REQUIRE(o != nullptr);
	ActiveBody *body = dynamic_cast<ActiveBody *>(o->getBodyModule());
	REQUIRE(body != nullptr);
	body->internalChangeHealth(-60.0f); // a repair / construction / script path: 100 -> 40
	CHECK(body->getDamageState() == BODY_REALLYDAMAGED);
	CHECK(o->testModelCondition(CombatNames::modelCondition("REALLYDAMAGED"))); // the visual state still follows (vslot 0x94)
	CHECK(bySite(w.events(), FXEvent::POSITION_FX).empty());
	CHECK(bySite(w.events(), FXEvent::ATTACH_PARTICLE_SYSTEM).empty());
}

TEST_CASE("fx4 bones: RandomBone draws the logic random among the drawable's bones (RW 0x8BDDAC: GameLogicRandomValue(0, n - 1)), a plain bone does not")
{
	FxWorld w;
	FakeBones bones;
	w.combat().setLaunchOffsets(&bones);
	Object *o = w.unit("TdfxBones", 'B', 400, 300);
	REQUIRE(o != nullptr);
	const Coord3D at = *o->getPosition();
	GameLogicRandom expected = w.logic->random();
	const int pick = expected.getValue(0, 2, "TransitionDamageFX.cpp", 0x153);
	DamageInfo first = hit(30.0f, DAMAGE_FLAME);
	o->attemptDamage(first);
	const std::vector<FXEvent> events = w.events(); // the pointers bySite returns point into this copy (review r1)
	std::vector<const FXEvent *> fx = bySite(events, FXEvent::POSITION_FX);
	REQUIRE(fx.size() == 2);
	CHECK(fx[0]->position.x == at.x + 100.0f * (float)(pick + 1)); // FIREnn, the picked one
	CHECK(fx[0]->position.z == at.z + 1.0f);
	CHECK(fx[1]->position.x == at.x + 7.0f); // SMOKE, no draw
	CHECK(fx[1]->position.y == at.y + 8.0f);
	CHECK(fx[1]->position.z == at.z + 9.0f);
	// exactly one draw was made: the logic's next number is the copy's next
	CHECK(w.logic->random().getValue(0, 1000000, "t", 1) == expected.getValue(0, 1000000, "t", 1));
	w.combat().setLaunchOffsets(nullptr);
}

TEST_CASE("fx4 save: the last damage record's type round-trips in ActiveBody's xfer version 3; version 1 and 2 load with defaults")
{
	FxWorld w;
	Object *a = w.unit("TdfxProbe", 'B', 400, 300);
	Object *b = w.unit("TdfxProbe", 'B', 500, 300);
	Object *c = w.unit("TdfxProbe", 'B', 600, 300);
	REQUIRE(a != nullptr);
	REQUIRE(b != nullptr);
	REQUIRE(c != nullptr);
	w.frames(1); // at frame zero the initial -1 record survives a source-less hit (RW 0x8C4294 .. 0x8C42CB)
	DamageInfo water = hit(10.0f, DAMAGE_WATER);
	a->attemptDamage(water);
	ActiveBody *ba = dynamic_cast<ActiveBody *>(a->getBodyModule());
	ActiveBody *bb = dynamic_cast<ActiveBody *>(b->getBodyModule());
	ActiveBody *bc = dynamic_cast<ActiveBody *>(c->getBodyModule());
	REQUIRE(ba != nullptr);
	REQUIRE(bb != nullptr);
	REQUIRE(bc != nullptr);
	XferSave saved;
	ba->xfer(saved);
	REQUIRE_FALSE(saved.data().empty());
	CHECK(saved.data()[0] == 3); // includes the record's noEffect byte
	XferLoad load(saved.data());
	bb->xfer(load);
	REQUIRE(load.atEnd());
	int type = -1;
	REQUIRE(bb->lastDamageRecordType(type));
	CHECK(type == DAMAGE_WATER);
	bool noEffect = true;
	REQUIRE(bb->lastDamageRecordNoEffect(noEffect));
	CHECK_FALSE(noEffect);
	XferSave again;
	bb->xfer(again);
	CHECK(again.data() == saved.data());
	// version 2 (before Local 30): without the trailing noEffect byte (loads cleanly with default false)
	std::vector<std::uint8_t> v2 = saved.data();
	v2[0] = 2;
	v2.pop_back();
	XferLoad oldV2(v2);
	bc->xfer(oldV2);
	REQUIRE(oldV2.atEnd());
	REQUIRE(bc->lastDamageRecordType(type));
	CHECK(type == DAMAGE_WATER);
	REQUIRE(bc->lastDamageRecordNoEffect(noEffect));
	CHECK_FALSE(noEffect);
	// version 1 (before lane FX-4): without the record's damage type and noEffect
	std::vector<std::uint8_t> v1 = v2;
	v1[0] = 1;
	v1.resize(v1.size() - 4);
	XferLoad oldV1(v1);
	bc->xfer(oldV1);
	REQUIRE(oldV1.atEnd());
	REQUIRE(bc->lastDamageRecordType(type));
	CHECK(type == DAMAGE_UNDEFINED);
	REQUIRE(bc->lastDamageRecordNoEffect(noEffect));
	CHECK_FALSE(noEffect);
}

TEST_CASE("fx4 shake: W3DView::shake (RW 0x486D86) adds the type's intensity with the distance falloff, caps it and ignores far epicenters")
{
	CameraSettings gd;
	std::string err;
	// the retail GameData values (Data\INI\GameData.ini)
	REQUIRE_MESSAGE(CameraSettings::scan("GameData\n  DefaultCameraMinHeight = 120.0\n  DefaultCameraMaxHeight = 300.0\n  DefaultCameraPitchAngle = 37.5\n  DefaultCameraYawAngle = 0.0\n"
										 "  DefaultCameraScrollSpeedScalar = 1.0\n  CameraLockHeightDelta = 150.0\n  CameraTerrainSampleRadiusForHeight = 1.0\n"
										 "  UseCameraInReplay = No\n  CameraAdjustSpeed = 0.3\n  ScrollAmountCutoff = 50.0\n  EnforceMaxCameraHeight = No\n"
										 "  HorizontalScrollSpeedFactor = 0.4\n  VerticalScrollSpeedFactor = 0.5\n  ScreenEdgeScrollSpeedFactor = 1.0\n  ScreenEdgeScrollRampTime = 0.25\n"
										 "  KeyboardScrollSpeedFactor = 1.0\n  KeyboardCameraRotateSpeed = 0.1\n  PartitionCellSize = 40.0\n"
										 "  ShakeSubtleIntensity = 0.5\n  ShakeNormalIntensity = 1.0\n  ShakeStrongIntensity = 2.5\n  ShakeSevereIntensity = 5.0\n"
										 "  ShakeCineExtremeIntensity = 7.5\n  ShakeCineInsaneIntensity = 10.0\n  MaxShakeIntensity = 10.0\n  MaxShakeRange = 150.0\nEnd\n",
					   gd, &err),
		err);
	REQUIRE(gd.haveShake());
	TacticalCamera cam(gd);
	const Coord3D p = cam.position(); // the view's position before a map (RW 0x48B6A4: 870, 770)
	cam.shake(Coord3D{ p.x + 75.0f, p.y, 0.0f }, 3, 0.0f); // SEVERE 5.0 at half the range
	CHECK(cam.shakeIntensity() == 2.5f);
	cam.shake(Coord3D{ p.x + 150.0f, p.y, 0.0f }, 5, 0.0f); // at the range: (1 - 1) * 10
	CHECK(cam.shakeIntensity() == 2.5f);
	cam.shake(Coord3D{ p.x + 151.0f, p.y, 0.0f }, 5, 0.0f); // beyond it: nothing
	CHECK(cam.shakeIntensity() == 2.5f);
	cam.shake(p, 5, 0.0f);
	cam.shake(p, 5, 0.0f); // 2.5 + 10 + 10: capped at MaxShakeIntensity
	CHECK(cam.shakeIntensity() == 10.0f);
	CHECK(cam.shakesWithoutData() == 0);
	// a GameData without the shake fields does not shake (S-450)
	CameraSettings bare = gd;
	bare.shakeSeen = 0;
	TacticalCamera still(bare);
	still.shake(still.position(), 3, 0.0f);
	CHECK(still.shakeIntensity() == 0.0f);
	CHECK(still.shakesWithoutData() == 1);
}

TEST_CASE("fx4 binary facts: the TransitionDamageFX and shake addresses the port cites")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("fx4 binary facts (RW_GAME_DAT unset)");
		return;
	}
	CHECK(pe->u32At(0xC706D8 + 8) == 0x8BE077);       // the DAMAGE interface's slot 2: onBodyDamageStateChange
	CHECK(pe->hex(0x8BE08B, 2) == "6a45");            // push 0x45 (ACTIVELY_BEING_CONSTRUCTED) before RW 0x46E918
	CHECK(pe->cstring(0xC70670).find("TransitionDamageFX.cpp") != std::string::npos); // the RandomBone draw's file
	CHECK(pe->u32At(0xBDD490 + 0x1AC) == 0x486D86);   // W3DView vslot 0x1AC: shake
	CHECK(pe->u32At(0xBDD38C) == 0x40C90FDBu);        // 6.2831855f
	CHECK(pe->u32At(0xBE5600) == 0x3C23D70Au);        // 0.01f
	CHECK(pe->u32At(0xBDC628) == 0x3F400000u);        // 0.75f
	CHECK(pe->u32At(0xC72130) == 0x715914);           // ActiveBody body interface slot 0x40: the last damage record
	// the shake rows of the GameData table { name, parseReal, 0, offset }
	const char *const names[] = { "ShakeSubtleIntensity", "ShakeNormalIntensity", "ShakeStrongIntensity", "ShakeSevereIntensity", "ShakeCineExtremeIntensity",
		"ShakeCineInsaneIntensity", "MaxShakeIntensity", "MaxShakeRange" };
	for (std::uint32_t i = 0; i < 8; ++i)
	{
		const std::uint32_t row = 0xC00610 + i * 16;
		INFO(names[i]);
		CHECK(pe->cstring(pe->u32At(row)) == names[i]);
		CHECK(pe->u32At(row + 4) == 0x42ED00);
		CHECK(pe->u32At(row + 12) == 0xB74 + i * 4);
	}
}
