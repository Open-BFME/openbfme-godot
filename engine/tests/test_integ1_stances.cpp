// OpenBFME. GPL-3.0.
// Lane INTEG-1 without retail data: the StanceTemplate grammar (RW 0x835967 / 0x83555A): the stance names (RW 0xC53660), the entries' AttributeModifier and MeleeBehavior,
// a duplicate template (RW 0xC5373C), an unknown stance name; the stance classes (RW 0x861D8E). The retail stances run in test_hud_horde2_retail.cpp.
#include "doctest.h"
#include "IniTestUtil.h"
#include "LogicTestUtil.h"

#include "Common/INIException.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Module/StancesBehavior.h"

#include <cstring>

namespace
{
struct StanceFixture
{
	initest::Fixture fx;
	StanceTemplateStore store;
	StanceTemplateStore *saved = TheStanceTemplateStore;
	StanceFixture()
	{
		store.registerBlock(fx.env.blocks);
		TheStanceTemplateStore = &store;
	}
	~StanceFixture() { TheStanceTemplateStore = saved; }
	std::string load(const std::string &text, int *code = nullptr) { return initest::loadError(fx.env, "stances.ini", text, INI_LOAD_OVERWRITE, code); }
};

const char *kStances =
	"StanceTemplate PikeHorde\n"
	"    Stance Aggressive\n"
	"        AttributeModifier  PikeHordeStanceAggressive\n"
	"        MeleeBehavior = Amoeba\n"
	"        End\n"
	"    End\n"
	"    Stance HoldGroundMoving\n"
	"        AttributeModifier   PikeHordeStanceHoldGround\n"
	"    End\n"
	"    Stance Porcupine\n"
	"        AttributeModifier PikeHordeStancePorcupine\n"
	"        MeleeBehavior = HoldGround\n"
	"        End\n"
	"    End\n"
	"End\n";
} // namespace

TEST_CASE("integ1 stances: the stance names are the binary's table (RW 0xC53660) and the classes follow RW 0x861D8E")
{
	const char *expected[] = { "Uninitialized", "Battle", "Aggressive", "HoldGround", "Porcupine", "HoldGroundMoving" };
	for (int i = 0; i < STANCE_COUNT; ++i)
	{
		CHECK(std::strcmp(TheStanceNames[i], expected[i]) == 0);
	}
	CHECK(TheStanceNames[STANCE_COUNT] == nullptr);
	CHECK(StancesBehavior::stanceClass(STANCE_UNINITIALIZED) == 1);
	CHECK(StancesBehavior::stanceClass(STANCE_BATTLE) == 1);
	CHECK(StancesBehavior::stanceClass(STANCE_AGGRESSIVE) == 2);
	CHECK(StancesBehavior::stanceClass(STANCE_HOLD_GROUND) == 3);
	CHECK(StancesBehavior::stanceClass(STANCE_PORCUPINE) == 3);
	CHECK(StancesBehavior::stanceClass(STANCE_HOLD_GROUND_MOVING) == 3);
}

TEST_CASE("integ1 stances: a StanceTemplate fills one entry per named stance with its ModifierList and MeleeBehavior (RW 0x835967 / 0x83555A)")
{
	StanceFixture f;
	REQUIRE(f.load(kStances).empty());
	const StanceTemplate *t = f.store.find("PikeHorde");
	REQUIRE(t);
	CHECK(t->entries[STANCE_BATTLE].attributeModifier.empty()); // no Battle entry: no list, no MeleeBehavior
	CHECK_FALSE(static_cast<bool>(t->entries[STANCE_BATTLE].meleeBehavior));
	CHECK(t->entries[STANCE_AGGRESSIVE].attributeModifier == "PikeHordeStanceAggressive");
	REQUIRE(static_cast<bool>(t->entries[STANCE_AGGRESSIVE].meleeBehavior));
	CHECK(t->entries[STANCE_AGGRESSIVE].meleeBehavior->m_kind == MeleeBehaviorModuleData::AMOEBA);
	CHECK(t->entries[STANCE_HOLD_GROUND_MOVING].attributeModifier == "PikeHordeStanceHoldGround");
	CHECK_FALSE(static_cast<bool>(t->entries[STANCE_HOLD_GROUND_MOVING].meleeBehavior));
	REQUIRE(static_cast<bool>(t->entries[STANCE_PORCUPINE].meleeBehavior));
	CHECK(t->entries[STANCE_PORCUPINE].meleeBehavior->m_kind == MeleeBehaviorModuleData::HOLD_GROUND);
	CHECK(f.store.find("NoSuchTemplate") == nullptr);
}

TEST_CASE("integ1 stances: a second StanceTemplate of the same name and an unknown stance name are INI errors")
{
	StanceFixture f;
	REQUIRE(f.load(kStances).empty());
	int code = 0;
	const std::string dup = f.load("StanceTemplate PikeHorde\nEnd\n", &code);
	CHECK(dup.find("already defined") != std::string::npos); // RW 0xC5373C "%s(%d) : Stance %s already defined"
	CHECK(code == 3);
	StanceFixture g;
	CHECK_FALSE(g.load("StanceTemplate X\n    Stance Defensive\n    End\nEnd\n").empty()); // not in RW 0xC53660
}

namespace
{
struct StanceRuntimeFixture
{
	StanceFixture templates;
	logictest::LogicWorld world;
	StancesBehaviorModuleData data;
	Object *object;
	std::unique_ptr<StancesBehavior> stance;

	StanceRuntimeFixture()
	{
		REQUIRE(templates.load("StanceTemplate SyntheticStances\nEnd\n").empty());
		REQUIRE(world.w.load("Object SyntheticStanceUnit\nEnd\n").empty());
		object = world.make("SyntheticStanceUnit");
		data.m_stanceTemplate = "SyntheticStances";
		stance = std::make_unique<StancesBehavior>(object, &data);
	}
};
}

TEST_CASE("stance1: factory initialization defaults to Battle and move notifications preserve Battle")
{
	StanceRuntimeFixture f;
	f.object->setStatus(0x5A, true);
	f.stance->update(); // RW 0x8622E9, tier A: wait while leaving the factory.
	CHECK(f.stance->getStance() == STANCE_UNINITIALIZED);
	f.object->setStatus(0x5A, false);
	f.stance->update();
	CHECK(f.stance->getStance() == STANCE_BATTLE);
	for (int n = 0; n < 3; ++n)
	{
		f.stance->onMoveStart();
		f.stance->onMoveEnd();
		f.stance->update();
		CHECK(f.stance->getStance() == STANCE_BATTLE);
	}
}

TEST_CASE("stance1: explicit HoldGround alone enters HoldGroundMoving and returns to HoldGround")
{
	StanceRuntimeFixture f;
	f.stance->update();
	REQUIRE(f.stance->setStance(STANCE_HOLD_GROUND, "test:explicit-player-choice"));
	f.stance->onMoveStart(); // RW 0x862275, tier A: 3 -> 5; Battle is not changed.
	CHECK(f.stance->getStance() == STANCE_HOLD_GROUND_MOVING);
	f.stance->onMoveEnd(); // Existing port; RW 0x86228E is tier B, target diff still required.
	CHECK(f.stance->getStance() == STANCE_HOLD_GROUND);
	f.stance->update();
	CHECK(f.stance->getStance() == STANCE_HOLD_GROUND);
	REQUIRE(f.stance->setStance(STANCE_AGGRESSIVE, "test:explicit-player-choice"));
	f.stance->onMoveStart();
	f.stance->onMoveEnd();
	f.stance->update();
	CHECK(f.stance->getStance() == STANCE_AGGRESSIVE);
}

TEST_CASE("stance1: native snapshots preserve every stance without invoking setter side effects")
{
	StanceRuntimeFixture f;
	for (int value = STANCE_UNINITIALIZED; value < STANCE_COUNT; ++value)
	{
		f.stance->setStance((StanceType)value, "test:snapshot-setup");
		XferSave saved;
		f.stance->xfer(saved);
		StateHasher before;
		f.stance->crc(before);
		f.stance->setStance(value == STANCE_BATTLE ? STANCE_AGGRESSIVE : STANCE_BATTLE, "test:snapshot-disturb");
		const auto notices = StancesBehavior::stats().listenerNotices.load();
		XferLoad load(saved.data());
		f.stance->xfer(load);
		CHECK(f.stance->getStance() == (StanceType)value);
		CHECK(StancesBehavior::stats().listenerNotices.load() == notices);
		StateHasher after;
		f.stance->crc(after);
		CHECK(after.value() == before.value());
		XferSave again;
		f.stance->xfer(again);
		CHECK(again.data() == saved.data());
	}
}
