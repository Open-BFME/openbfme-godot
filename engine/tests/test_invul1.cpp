// OpenBFME. INVUL-1 tests: ActiveBody::attemptDamage returns at once for an object in model condition INVULNERABLE (RotWK RW 0x8C3FE7
// `push 0x14A; call 0x46E918`, before the UNATTACKABLE test; donor Open-BFME-2 ActiveBody.cpp:2189, BFME2 0x8BFE07, same shape: both editions).
// The condition comes from ProductionUpdate's UnitInvulnerableTime (RW 0x8A275F; the Mumakil Pen and the Isengard Siege Works use 6000 ms) and the
// castle unpack (RW 0x798238). Synthetic data (combattest::CombatWorld), no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "ProdTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/ProductionUpdate.h"

using namespace combattest;

namespace
{
const char kInvulObjects[] =
	"Object InvulPen\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  CommandSet = InvulPenSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"    UnitInvulnerableTime = 6000\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
	"    UnitCreatePoint = X:0.0 Y:-30.0 Z:0.0\n"
	"    NaturalRallyPoint = X:0.0 Y:-60.0 Z:0.0\n"
	"    ExitDelay = 0\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n";

DamageInfo hit(int type, float amount)
{
	DamageInfo d;
	d.m_input.m_damageType = type;
	d.m_input.m_amount = amount;
	return d;
}

const char kInvulCommands[] = "CommandButton Command_InvulBuildDummy\n  Command = UNIT_BUILD\n  Object = Dummy\nEnd\n"
							   "CommandSet InvulPenSet\n  1 = Command_InvulBuildDummy\nEnd\n";

// the combat world with a CommandStore (canMakeUnit reads TheCommandStore, as ProdTestUtil.h's ProdWorld sets it)
struct InvulProdWorld : CombatWorld
{
	CommandStore commands;
	CommandStore *savedStore = nullptr;
	InvulProdWorld()
		: CombatWorld(kInvulObjects)
	{
		savedStore = TheCommandStore;
		TheCommandStore = &commands;
		commands.setThingFactory(&w.things);
		w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		const std::string err = w.load(kInvulCommands, INI_LOAD_OVERWRITE, "invul1_commands.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		logic->productionSettings() = prodtest::defaultSettings();
	}
	~InvulProdWorld() { TheCommandStore = savedStore; }
};

int invulnerableBit() { return CombatNames::modelCondition("INVULNERABLE"); }
} // namespace

TEST_CASE("invul1: model condition INVULNERABLE is model condition 0x14A, the index RW 0x8C3FE7 pushes")
{
	CHECK(invulnerableBit() == 0x14A);
}

TEST_CASE("invul1: an INVULNERABLE object takes no damage of any type, UNRESISTABLE included, until the timed condition expires")
{
	CombatWorld w(kInvulObjects);
	Object *u = w.unit("Dummy", 'A', 500.0f, 500.0f);
	REQUIRE(u);
	const float full = w.health(u);
	u->setSpecialModelConditionState(invulnerableBit(), 180);
	REQUIRE(u->testModelCondition(invulnerableBit()));
	for (int type : { (int)DAMAGE_UNRESISTABLE, (int)DAMAGE_SLASH, (int)DAMAGE_CRUSH })
	{
		DamageInfo d = hit(type, full * 10.0f);
		d.m_output.m_actualDamageDealt = -1.0f; // the early return leaves the outputs untouched, as retail's
		u->attemptDamage(d);
		CHECK(d.m_output.m_actualDamageDealt == -1.0f);
		CHECK(w.health(u) == full);
		CHECK_FALSE(u->isEffectivelyDead());
	}
	w.frames(181);
	REQUIRE_FALSE(u->testModelCondition(invulnerableBit()));
	DamageInfo d = hit(DAMAGE_UNRESISTABLE, 25.0f);
	u->attemptDamage(d);
	CHECK(d.m_output.m_actualDamageDealt == 25.0f);
	CHECK(w.health(u) == full - 25.0f);
}

TEST_CASE("invul1: a castle unpacking (INVULNERABLE + UNATTACKABLE, RW 0x798238) ignores UNRESISTABLE damage; UNATTACKABLE alone takes it")
{
	CombatWorld w(kInvulObjects);
	Object *u = w.unit("Dummy", 'A', 500.0f, 500.0f);
	REQUIRE(u);
	const float full = w.health(u);
	u->setStatus((unsigned)CombatNames::statuses().unattackable, true);
	u->setModelConditionState(invulnerableBit(), true);
	DamageInfo d = hit(DAMAGE_UNRESISTABLE, 30.0f);
	u->attemptDamage(d);
	CHECK(w.health(u) == full);
	u->setModelConditionState(invulnerableBit(), false);
	DamageInfo d2 = hit(DAMAGE_UNRESISTABLE, 30.0f);
	u->attemptDamage(d2);
	CHECK(w.health(u) == full - 30.0f);
}

TEST_CASE("invul1: a unit produced with UnitInvulnerableTime 6000 (30 logic frames at 5 per second) survives a lethal hit inside the window and dies to it after")
{
	InvulProdWorld w;
	Object *pen = w.unit("InvulPen", 'A', 1000.0f, 1000.0f);
	REQUIRE(pen);
	ProductionUpdateInterface *pu = pen->getProductionUpdate();
	REQUIRE(pu);
	const ThingTemplate *dummy = w.w.get("Dummy");
	REQUIRE(dummy);
	REQUIRE(pu->queueCreateUnit(dummy, -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
	Object *made = nullptr;
	UnsignedInt madeAt = 0;
	for (int i = 0; i < 400 && !made; ++i)
	{
		w.frames(1);
		for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getProducerID() == pen->getID() && o->getTemplate()->getName() == "Dummy")
			{
				made = o;
				madeAt = w.logic->getFrame();
			}
		}
	}
	REQUIRE(made);
	CHECK(made->testModelCondition(invulnerableBit()));
	w.frames(15); // half the window (6 s = 30 frames, LOGICFRAMES_PER_SECOND 5)
	REQUIRE(made->testModelCondition(invulnerableBit()));
	const float full = w.health(made);
	DamageInfo lethal = hit(DAMAGE_UNRESISTABLE, full * 10.0f);
	made->attemptDamage(lethal);
	CHECK(w.health(made) == full);
	CHECK_FALSE(made->isEffectivelyDead());
	while (made->testModelCondition(invulnerableBit()) && w.logic->getFrame() < madeAt + 60)
	{
		w.frames(1);
	}
	CHECK(w.logic->getFrame() - madeAt == 30); // the window is the 30 frames of the data
	REQUIRE_FALSE(made->testModelCondition(invulnerableBit()));
	DamageInfo lethal2 = hit(DAMAGE_UNRESISTABLE, full * 10.0f);
	made->attemptDamage(lethal2);
	CHECK(made->isEffectivelyDead());
}
