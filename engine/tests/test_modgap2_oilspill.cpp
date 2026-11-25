// Both editions: RW 0x0088F863, BFME2 0x0048C1F7.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/BfmeGame.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/OilSpillUpdate.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kOilSpillIni[] =
    "Object TestBreadcrumbObj\n"
    "  KindOf = IMMOBILE\n"
    "  Body = ActiveBody BodyTag\n"
    "    MaxHealth = 10\n"
    "  End\n"
    "End\n"
    "Object TestOilSpillObj\n"
    "  KindOf = SELECTABLE\n"
    "  Body = ActiveBody BodyTag\n"
    "    MaxHealth = 100\n"
    "  End\n"
    "  Behavior = OilSpillUpdate ModuleTag_SpillOil\n"
    "    BreadcrumbName = TestBreadcrumbObj\n"
    "    IgnitionWeaponSpacing = 60\n"
    "    AliveOnly = Yes\n"
    "    OilSpillFX = FX_OilSpill\n"
    "  End\n"
    "End\n";
} // namespace

TEST_CASE("modgap2 oilspill: module data defaults and field table (both editions: RW 0x0088F7D8 / 0x00C62AD8, BFME2 0x0048C16C / 0x00C4C2A8)")
{
	OilSpillUpdateModuleData d;
	CHECK(d.m_breadcrumbName == "");
	CHECK(d.m_ignitionWeaponName == "");
	CHECK(d.m_ignitionWeaponSpacing == 0.0f);
	CHECK(d.m_oilSpillFX == "");
	CHECK(d.m_aliveOnly == false);
}

TEST_CASE("modgap2 oilspill: parsing INI with BreadcrumbName, IgnitionWeaponSpacing, AliveOnly, OilSpillFX")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CAPTURE(gameEditionName(edition));
		combattest::CombatWorld w(kOilSpillIni);
		w.logic->setEdition(edition);

		Object *obj = w.unit("TestOilSpillObj", 'A', 100.0f, 100.0f);
		REQUIRE(obj != nullptr);

		auto *spill = dynamic_cast<OilSpillUpdate *>(obj->findModule("OilSpillUpdate"));
		REQUIRE(spill != nullptr);
		const OilSpillUpdateModuleData *data = spill->getData();
		REQUIRE(data != nullptr);
		CHECK(data->m_breadcrumbName == "TestBreadcrumbObj");
		CHECK(data->m_ignitionWeaponSpacing == 60.0f);
		CHECK(data->m_aliveOnly == true);
		CHECK(data->m_oilSpillFX == "FX_OilSpill");
	}
}

TEST_CASE("modgap2 oilspill: drops breadcrumb objects at spacing intervals and records FX (both editions: RW 0x0088F97A, BFME2 0x0048C30E)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CAPTURE(gameEditionName(edition));
		combattest::CombatWorld w(kOilSpillIni);
		w.logic->setEdition(edition);

		Object *obj = w.unit("TestOilSpillObj", 'A', 100.0f, 100.0f);
		REQUIRE(obj != nullptr);

		auto *spill = dynamic_cast<OilSpillUpdate *>(obj->findModule("OilSpillUpdate"));
		REQUIRE(spill != nullptr);

		// First update: initial breadcrumb dropped at (100, 100, 0)
		spill->update();
		CHECK(spill->breadcrumbs().size() == 1u);
		CHECK(spill->breadcrumbs()[0].x == 100.0f);
		CHECK(spill->breadcrumbs()[0].y == 100.0f);

		// Move by 30 (< spacing 60): no breadcrumb dropped
		const Coord3D pos1{ 130.0f, 100.0f, 0.0f };
		obj->setPosition(&pos1);
		spill->update();
		CHECK(spill->breadcrumbs().size() == 1u);

		// Move by another 40 (total dx = 70 >= 60): second breadcrumb dropped
		const Coord3D pos2{ 170.0f, 100.0f, 0.0f };
		obj->setPosition(&pos2);
		spill->update();
		CHECK(spill->breadcrumbs().size() == 2u);
		CHECK(spill->breadcrumbs()[1].x == 170.0f);
		CHECK(spill->breadcrumbs()[1].y == 100.0f);

		// Stop line is noted in logic
		bool stopped = false;
		for (const auto &line : w.logic->report().stops)
		{
			stopped = stopped || line == OilSpillUpdate::stopLine();
		}
		CHECK(stopped);

		// StateHash CRC verification
		StateHasher h1;
		spill->crc(h1);
		StateHasher h2;
		spill->crc(h2);
		CHECK(h1.value() == h2.value());

		// Snapshot xfer save & load
		XferSave save;
		spill->xfer(save);

		Object *obj2 = w.unit("TestOilSpillObj", 'A', 0.0f, 0.0f);
		REQUIRE(obj2 != nullptr);
		auto *spill2 = dynamic_cast<OilSpillUpdate *>(obj2->findModule("OilSpillUpdate"));
		REQUIRE(spill2 != nullptr);

		const auto &buffer = save.data();
		XferLoad load(buffer.data(), buffer.size());
		spill2->xfer(load);
		CHECK(spill2->breadcrumbs().size() == 2u);
		CHECK(spill2->breadcrumbs()[0].x == 100.0f);
		CHECK(spill2->breadcrumbs()[1].x == 170.0f);
	}
}
