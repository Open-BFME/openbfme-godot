// PORT-68: Weapon/FXTrigger (weapon template +0x10).
// Synthetic INI and synthetic objects only: no retail data, no ROTWK_INSTALL needed.
//
// Expected values come from the disassembly of RotWK 2.01 (game.dat, caveat S-001, read with tools/mod/rw.py):
//   * TheFXTriggerNames RW 0xDA14FC (the Weapon block's FXTrigger list) and the FXList ParticleSysBone list RW 0xD9DE5C hold the same three strings:
//     RW 0xDA08FC and 0xDA14FC both point at 0xBD1584 "NONE", 0xBD155C "CATAPULT_ROCK", 0xBD154C "TREBUCHET_ROCK".
//   * Object::doKnockback RW 0x692223 picks the projectile index at RW 0x6922E5 .. 0x692307: `xor edi, edi`, then three AsciiString::compareNoCase
//     RW 0x406585 calls against RW 0xDA08FC (`inc edi` / `cmp edi, 3`), `mov ebx, edi` on a hit; no hit leaves ebx = 0 (= NONE). RW 0x68B56A then
//     stores it at Drawable + 0x360 (RW 0x68B574 / 0x68B578 `mov dword ptr [eax + 0x360], ecx`).
//   * Object's shockwave handler RW 0x6968BC clears Drawable + 0x360 with 0 at entry (RW 0x6968D3 `push 0` / RW 0x6968D7, before the amount and radius
//     tests at RW 0x6968E5 / 0x6968F9) and writes DamageInfo + 0x2C there at RW 0x696E39 (just before the return at RW 0x696E43).
//   * LiveFX's ParticleSysBone creation path RW 0x4C6514 skips an entry whose FXTrigger is neither 0 nor the drawable's +0x360: RW 0x4C662D
//     `cmp dword ptr [ebp - 0x58], edi` (0 = NONE, no test at all) then RW 0x4C6632 `mov eax, [eax + 0x360]` / `cmp eax, [ebp - 0x58]` / `jne 0x4C6D34`.
// GPL-3.0.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "LogicTestUtil.h"

#include "Common/AsciiString.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/LiveFX.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Weapon.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace combattest;

namespace
{

// ---- the name list --------------------------------------------------------------------------------------------------------------
// RW 0xDA14FC (Weapon/FXTrigger) and RW 0xD9DE5C (FXList ParticleSysBone): one list of three names, NONE at 0.
TEST_CASE("PORT-68 names: the FXTrigger list is NONE / CATAPULT_ROCK / TREBUCHET_ROCK and ends (RW 0xDA14FC, RW 0xDA08FC)")
{
	REQUIRE(TheFXTriggerNames[0] != nullptr);
	CHECK(std::string(TheFXTriggerNames[0]) == "NONE");
	REQUIRE(TheFXTriggerNames[1] != nullptr);
	CHECK(std::string(TheFXTriggerNames[1]) == "CATAPULT_ROCK");
	REQUIRE(TheFXTriggerNames[2] != nullptr);
	CHECK(std::string(TheFXTriggerNames[2]) == "TREBUCHET_ROCK");
	CHECK(TheFXTriggerNames[3] == nullptr); // RW 0x6922FB `cmp edi, 3`: the scan knows exactly three names
	// the weapon block's list and the FXList's list are the same three strings in retail (the doctest of test_weapon_data pins the raw pointer table)
	CHECK(AsciiStringUtil::compareNoCase("trebuchet_rock", TheFXTriggerNames[2]) == 0);
}

// ---- the client event pipeline ---------------------------------------------------------------------------------------------------
// The logic's call (Object::setDrawableFXTrigger RW 0x68B56A writes Drawable + 0x360) travels the port's SMOOTH-1 seam: the object records one
// FX_TRIGGER event through the client hooks, and the renderer applies it to its drawable.
TEST_CASE("PORT-68 events: setDrawableFXTrigger records one FX_TRIGGER event and the drawable takes the index (RW 0x68B56A)")
{
	logictest::LogicWorld lw;
	REQUIRE(lw.loadError.empty());
	W3DDrawModules::registerTypedDrawModuleData(lw.w.modules);
	REQUIRE(lw.w.load("Object Pfx68Soldier\n"
					  "  KindOf = INFANTRY SELECTABLE\n"
					  "  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
					  "End\n")
				.empty());
	ArchiveW3DFileSource source(lw.w.fx.fsys);
	WW3DAssetManager assets(source);
	DrawableManager dm(assets, *lw.logic);
	ClientEventRecorder rec(*lw.logic);
	lw.logic->setClientHooks(&rec);
	Object *o = lw.make("Pfx68Soldier");
	dm.applyEvents(rec.take());
	Drawable *d = dm.findByObject(o->getID());
	REQUIRE(d);
	REQUIRE(d->getFXTrigger() == 0); // Drawable + 0x360 starts at 0 (NONE)

	o->clientHooks()->setDrawableFXTrigger(*o, 2);
	const std::vector<ClientEvent> events = rec.take();
	REQUIRE(events.size() == 1);
	CHECK(events[0].kind == ClientEvent::FX_TRIGGER);
	CHECK(events[0].object == o->getID());
	CHECK(events[0].fxTrigger == 2);
	dm.applyEvents(events);
	CHECK(d->getFXTrigger() == 2);
	o->clientHooks()->setDrawableFXTrigger(*o, 0);
	dm.applyEvents(rec.take());
	CHECK(d->getFXTrigger() == 0);
	lw.logic->setClientHooks(nullptr);
}

// ---- Object::doKnockback, RW 0x692223 ------------------------------------------------------------------------------------------
// A crush knockback names its projectile type as a string ("NONE" from SquishCollide RW 0x68B5xx, a data name elsewhere); RW matches it against
// TheFXTriggerNames without case and writes the index to the drawable.
struct FxTriggerRecorder : ObjectClientHooks
{
	std::vector<int> triggers;
	std::string log;
	void objectCreated(Object &obj) override
	{
		log += "created#" + std::to_string(obj.getID()) + ";";
		obj.friend_bindToClient(this); // as ClientEventRecorder::objectCreated does: Object + 0xFC, RW 0x628882
	}
	void objectDestroyed(Object &obj) override { log += "destroyed#" + std::to_string(obj.getID()) + ";"; }
	void setDrawableFXTrigger(Object &obj, int fxTrigger) override
	{
		(void)obj;
		triggers.push_back(fxTrigger);
	}
};

const char kPfx68KnockObjects[] =
	"Object Pfx68PunchingBag\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

TEST_CASE("PORT-68 knockback: ObjectKnockback::apply maps the projectile name to the FXTrigger index and clears it on an unknown name (RW 0x692223)")
{
	CombatWorld f(kPfx68KnockObjects);
	FxTriggerRecorder hooks;
	f.logic->setClientHooks(&hooks);
	struct Detach { GameLogic *l; ~Detach() { l->setClientHooks(nullptr); } } detach{ f.logic.get() };
	// RW 0x6922E7 .. 0x692302: the three names, compareNoCase; a hit moves the index into the value RW 0x68B56A stores, no hit leaves 0.
	// `apply` stuns the object, so every case takes a fresh victim (a stunned object bails at RW 0x69223E before the store).
	struct Case
	{
		const char *projectile;
		int expected;
	};
	for (const Case &c : { Case{ "CATAPULT_ROCK", 1 }, Case{ "trebuchet_rock", 2 }, Case{ "NONE", 0 }, Case{ "none", 0 }, Case{ "TREBUCHET_ROCKS", 0 },
			 Case{ nullptr, 0 } })
	{
		CAPTURE(c.projectile ? c.projectile : "(null)");
		Object *o = f.unit("Pfx68PunchingBag", 'A', 300.0f, 300.0f);
		REQUIRE(o);
		hooks.triggers.clear();
		REQUIRE(ObjectKnockback::apply(*o, 0.0f, 8.0f, 1.0f, c.projectile));
		REQUIRE(hooks.triggers.size() == 1);
		CHECK(hooks.triggers[0] == c.expected);
		// the knockback itself still runs: the object is stunned and flailing
		CHECK(o->testModelCondition(CombatNames::modelCondition("STUNNED_FLAILING")));
		// a second knockback on the stunned object bails before the store (RW 0x69223E), so nothing new is recorded
		hooks.triggers.clear();
		CHECK_FALSE(ObjectKnockback::apply(*o, 0.0f, 8.0f, 1.0f, "CATAPULT_ROCK"));
		CHECK(hooks.triggers.empty());
	}
}

// ---- Object's shockwave handler, RW 0x6968BC -------------------------------------------------------------------------------------
// Entry clears Drawable + 0x360 with NONE before anything else (RW 0x6968D3 / 0x6968D7); the stun success writes DamageInfo + 0x2C at RW 0x696E39.
// Every other exit of the function (amount or radius 0, no PhysicsBehavior, shockwave resistance, resist >= 1) leaves the cleared value.
DamageInfo shockHit(int fxTrigger)
{
	DamageInfo hit;
	hit.m_input.m_damageType = DAMAGE_CRUSH;
	hit.m_input.m_shockWaveVector = Coord3D{ 3.0f, 4.0f, 0.0f };
	hit.m_input.m_shockWaveAmount = 6.0f;
	hit.m_input.m_shockWaveZMult = 1.5f;
	hit.m_input.m_shockWaveRadius = 10.0f;
	hit.m_input.m_shockWaveTaperOff = 0.75f;
	hit.m_input.m_fxTrigger = fxTrigger;
	return hit;
}

TEST_CASE("PORT-68 shockwave: the handler clears the drawable's FXTrigger at entry and sets DamageInfo +0x2C on the stun (RW 0x6968D7 / 0x696E39)")
{
	CombatWorld f(kPfx68KnockObjects);
	FxTriggerRecorder hooks;
	f.logic->setClientHooks(&hooks);
	struct Detach { GameLogic *l; ~Detach() { l->setClientHooks(nullptr); } } detach{ f.logic.get() };
	Object *a = f.unit("Pfx68PunchingBag", 'A', 300.0f, 300.0f);
	Object *b = f.unit("Pfx68PunchingBag", 'A', 320.0f, 300.0f);
	REQUIRE(a);
	REQUIRE(b);
	hooks.triggers.clear();

	// a hit with no shockwave: amount and radius 0 -> the entry clear alone, then RW 0x696E46's exit
	DamageInfo plain;
	plain.m_input.m_damageType = DAMAGE_CRUSH;
	REQUIRE(ObjectKnockback::shockWave(*a, plain));
	CHECK(hooks.triggers == std::vector<int>{ 0 });

	// a real shockwave: clear at entry, then the hit's own index at the end
	hooks.triggers.clear();
	DamageInfo hit = shockHit(2);
	REQUIRE(ObjectKnockback::shockWave(*b, hit));
	REQUIRE(hooks.triggers.size() == 2);
	CHECK(hooks.triggers[0] == 0);
	CHECK(hooks.triggers[1] == 2);

	// the second hit finds the object already stunned (RW 0x69693B): still the entry clear, and `true` (RW 0x696E46) without the final store
	hooks.triggers.clear();
	DamageInfo again = shockHit(1);
	REQUIRE(ObjectKnockback::shockWave(*b, again));
	CHECK(hooks.triggers == std::vector<int>{ 0 });
}

// ---- LiveFX's ParticleSysBone gate, RW 0x4C662D -----------------------------------------------------------------------------------
// An entry with FXTrigger 0 is never tested (every drawable plays it); otherwise the drawable's +0x360 must be equal or the entry is skipped.
TEST_CASE("PORT-68 ParticleSysBone: the FXTrigger gate reads the drawable's index, and 0 always passes (RW 0x4C662D)")
{
	logictest::LogicWorld lw;
	REQUIRE(lw.loadError.empty());
	W3DDrawModules::registerTypedDrawModuleData(lw.w.modules);
	REQUIRE(lw.w.load("Object Pfx68Gate\n"
					  "  KindOf = INFANTRY\n"
					  "  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
					  "End\n")
				.empty());
	ArchiveW3DFileSource source(lw.w.fx.fsys);
	WW3DAssetManager assets(source);
	DrawableManager dm(assets, *lw.logic);
	ClientEventRecorder rec(*lw.logic);
	lw.logic->setClientHooks(&rec);
	Object *o = lw.make("Pfx68Gate");
	dm.applyEvents(rec.take());
	Drawable *d = dm.findByObject(o->getID());
	REQUIRE(d);

	ParticleSysBoneInfo none;
	none.fxTrigger = 0;
	ParticleSysBoneInfo rock;
	rock.fxTrigger = 1;
	ParticleSysBoneInfo trebuchet;
	trebuchet.fxTrigger = 2;

	CHECK(LiveFX::allowsParticleSysBoneFXTrigger(*d, none)); // the NONE entry is never compared
	CHECK_FALSE(LiveFX::allowsParticleSysBoneFXTrigger(*d, rock));
	CHECK_FALSE(LiveFX::allowsParticleSysBoneFXTrigger(*d, trebuchet));
	d->setFXTrigger(1);
	CHECK(LiveFX::allowsParticleSysBoneFXTrigger(*d, none));
	CHECK(LiveFX::allowsParticleSysBoneFXTrigger(*d, rock));
	CHECK_FALSE(LiveFX::allowsParticleSysBoneFXTrigger(*d, trebuchet));
	d->setFXTrigger(2);
	CHECK_FALSE(LiveFX::allowsParticleSysBoneFXTrigger(*d, rock));
	CHECK(LiveFX::allowsParticleSysBoneFXTrigger(*d, trebuchet));
	lw.logic->setClientHooks(nullptr);
}

// FillDamageInfo (RW 0x90E28C step 9) copies the weapon's +0x10 into DamageInfo + 0x2C; the victim's shockwave handler writes DamageInfo + 0x2C
// to Drawable + 0x360 (RW 0x696E39) after clearing it at entry; the ParticleSysBone gate of RW 0x4C662D then lets exactly the CATAPULT_ROCK
// entries through. The parsed weapon block and the real damage core are used; only the objects are synthetic.
namespace
{
// the minimal NuggetDamageHost of a plain direct hit (every modifier absent, no contain, no flank, no self hit)
struct Pfx68NuggetHost : NuggetDamageHost
{
	Coord3D at{};
	bool hasVictim() override { return true; }
	float victimDistanceSqr2D(const Coord3D &) override { return 0.0f; }
	Coord3D victimPosition() override { return at; }
	Coord3D sourcePosition() override { return at; }
	bool sourceHasContain() override { return false; }
	int sourcePassengerCount() override { return 0; }
	bool filterAllowsVictim(const ObjectFilter &) override { return false; }
	bool sourceAdditive(int, float &) override { return false; }
	bool sourceMultiplicative(int, bool, float &out) override
	{
		out = 1.0f;
		return false;
	}
	bool victimFlankedBySource() override { return false; }
	bool sourceFlankedByVictim() override { return false; }
	bool weaponSourceIsVictim() override { return false; }
	std::uint32_t sourcePlayerMask() override { return 1u; }
	unsigned weaponSourceID() override { return 1u; }
};
} // namespace

TEST_CASE("PORT-68 path: the weapon's FXTrigger reaches the damage record, the shockwave writes it to the drawable, the gate selects the bones (RW 0x90E28C -> 0x696E39 -> 0x4C662D)")
{
	CombatWorld f(kPfx68KnockObjects);
	REQUIRE(f.w.load("Weapon Pfx68CatapultHit\n"
					 "  AttackRange = 11.5\n"
					 "  FXTrigger = CATAPULT_ROCK\n"
					 "  MetaImpactNugget\n"
					 "    ShockWaveAmount = 6.0\n"
					 "    ShockWaveRadius = 20.0\n"
					 "    ShockWaveTaperOff = 0.75\n"
					 "  End\n"
					 "  DamageNugget\n"
					 "    Damage = 10\n"
					 "    Radius = 0.0\n"
					 "    DamageType = PIERCE\n"
					 "    DeathType = NORMAL\n"
					 "  End\n"
					 "End\n",
				 INI_LOAD_OVERWRITE, "port68_fxtrigger.ini")
				.empty());
	const WeaponTemplate *catapult = TheWeaponStore->findWeaponTemplate("Pfx68CatapultHit");
	REQUIRE(catapult);
	REQUIRE(catapult->m_fxTrigger == 1); // FXTrigger CATAPULT_ROCK = TheFXTriggerNames[1]

	// FillDamageInfo (RW 0x90E28C step 9): in.m_fxTrigger = the weapon's +0x10
	const DamageNugget *damage = nullptr;
	for (const std::shared_ptr<WeaponNugget> &n : catapult->m_nuggets)
	{
		if (n->kind() == NUGGET_DAMAGE)
		{
			damage = static_cast<const DamageNugget *>(n.get());
		}
	}
	REQUIRE(damage);
	Pfx68NuggetHost host;
	DamageInfo record;
	REQUIRE(FillDamageInfo(*damage, *catapult, host, false, nullptr, record));
	REQUIRE(record.m_input.m_fxTrigger == 1);

	FxTriggerRecorder hooks;
	f.logic->setClientHooks(&hooks);
	struct Detach { GameLogic *l; ~Detach() { l->setClientHooks(nullptr); } } detach{ f.logic.get() };
	Object *victim = f.unit("Pfx68PunchingBag", 'A', 300.0f, 300.0f);
	REQUIRE(victim);
	hooks.triggers.clear();

	// the shockwave half of the record, then the handler: the entry clear, then the record's own index
	DamageInfo hit = record;
	hit.m_input.m_shockWaveVector = Coord3D{ 1.0f, 0.0f, 0.0f };
	hit.m_input.m_shockWaveAmount = 6.0f;
	hit.m_input.m_shockWaveRadius = 20.0f;
	hit.m_input.m_shockWaveTaperOff = 0.75f;
	REQUIRE(ObjectKnockback::shockWave(*victim, hit));
	REQUIRE(hooks.triggers.size() == 2);
	CHECK(hooks.triggers[0] == 0); // RW 0x6968D7, the entry clear
	CHECK(hooks.triggers[1] == 1); // RW 0x696E39, DamageInfo + 0x2C
	CHECK(hooks.triggers[1] == record.m_input.m_fxTrigger);
}

// The gate itself: with the drawable at 1 the CATAPULT_ROCK entries play and the TREBUCHET_ROCK ones do not; a NONE entry always plays.
TEST_CASE("PORT-68 path: the ParticleSysBone gate selects the entries whose FXTrigger is NONE or the drawable's own (RW 0x4C662D)")
{
	ParticleSysBoneInfo none;      none.fxTrigger = 0;
	ParticleSysBoneInfo catapult;  catapult.fxTrigger = 1;
	ParticleSysBoneInfo trebuchet; trebuchet.fxTrigger = 2;
	// the drawable is the only input besides the entry, so a bare object of the port's drawable type stands in: the FXTrigger field is all that is read
	logictest::LogicWorld lw;
	REQUIRE(lw.loadError.empty());
	W3DDrawModules::registerTypedDrawModuleData(lw.w.modules);
	REQUIRE(lw.w.load("Object Pfx68GateVictim\n"
					  "  KindOf = INFANTRY\n"
					  "  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
					  "End\n")
				.empty());
	ArchiveW3DFileSource source(lw.w.fx.fsys);
	WW3DAssetManager assets(source);
	DrawableManager dm(assets, *lw.logic);
	ClientEventRecorder rec(*lw.logic);
	lw.logic->setClientHooks(&rec);
	Object *o = lw.make("Pfx68GateVictim");
	dm.applyEvents(rec.take());
	Drawable *d = dm.findByObject(o->getID());
	REQUIRE(d);
	for (int trigger : { 0, 1, 2 })
	{
		CAPTURE(trigger);
		d->setFXTrigger(trigger);
		CHECK(LiveFX::allowsParticleSysBoneFXTrigger(*d, none));
		CHECK(LiveFX::allowsParticleSysBoneFXTrigger(*d, catapult) == (trigger == 1));
		CHECK(LiveFX::allowsParticleSysBoneFXTrigger(*d, trebuchet) == (trigger == 2));
	}
	lw.logic->setClientHooks(nullptr);
}

} // namespace
