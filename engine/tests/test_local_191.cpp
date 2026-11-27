// OpenBFME unit tests. GPL-3.0.
// LOCAL-191 (fork issue 984): retail 2.01 constructor defaults for LargeGroupBonusUpdateModuleData (RW 0x00893871,
// DONOR BFME2 0x004901BD, both editions).

#include "doctest.h"
#include "IniTestUtil.h"

#include "Common/INI.h"
#include "GameLogic/Module/AreaScanModules.h"

#include <string>

using namespace initest;

// Expectations from RotWK 2.01 game.dat RW 0x00893871 (DONOR BFME2 0x004901BD):
//   +0x08 (m_updateRate)   = *(uint32_t*)0x00DE93D4 = *(uint32_t*)0x00D9F608 = 5 (LOGICFRAMES_PER_SECOND)
//   +0x10 (m_count)        = 0
//   +0x14 (m_radius)       = 0.0f
//   +0x18 (m_alliesOnly)   = true (1)
//   +0x1C (m_rubOffRadius) = *(float*)0x00BDBC6C = 20.0f
TEST_CASE("LOCAL-191 LargeGroupBonusUpdateModuleData retail 2.01 defaults (RW 0x00893871)")
{
	LargeGroupBonusUpdateModuleData data;
	CHECK(data.m_updateRate == 5u);
	CHECK(data.m_count == 0);
	CHECK(data.m_radius == 0.0f);
	CHECK(data.m_alliesOnly == true);
	CHECK(data.m_rubOffRadius == 20.0f);

	// Neighboring check: INI block omitting UpdateRate/AlliesOnly/RubOffRadius preserves retail defaults;
	// explicit INI values override them.
	LargeGroupBonusUpdateModuleData omitted;
	{
		Fixture fx;
		fx.env.blocks.registerBlock("LGBBlock", [&omitted](INI *ini) {
			MultiIniFieldParse mp;
			LargeGroupBonusUpdateModuleData::buildFieldParse(mp);
			ini->initFromINIMulti(&omitted, mp);
		});
		const std::string err = loadError(fx.env, "local191_lgb_omitted.ini",
			"LGBBlock\n  Count = 50\n  Radius = 150.0\nEnd\n", INI_LOAD_OVERWRITE);
		REQUIRE(err.empty());
	}
	CHECK(omitted.m_updateRate == 5u);
	CHECK(omitted.m_count == 50);
	CHECK(omitted.m_radius == 150.0f);
	CHECK(omitted.m_alliesOnly == true);
	CHECK(omitted.m_rubOffRadius == 20.0f);

	LargeGroupBonusUpdateModuleData overridden;
	{
		Fixture fx;
		fx.env.blocks.registerBlock("LGBBlock", [&overridden](INI *ini) {
			MultiIniFieldParse mp;
			LargeGroupBonusUpdateModuleData::buildFieldParse(mp);
			ini->initFromINIMulti(&overridden, mp);
		});
		const std::string err = loadError(fx.env, "local191_lgb_overridden.ini",
			"LGBBlock\n  UpdateRate = 2000\n  AlliesOnly = No\n  RubOffRadius = 45.0\nEnd\n", INI_LOAD_OVERWRITE);
		REQUIRE(err.empty());
	}
	CHECK(overridden.m_updateRate == 10u); // 2000 ms -> 10 logic frames
	CHECK(overridden.m_alliesOnly == false);
	CHECK(overridden.m_rubOffRadius == 45.0f);
}
