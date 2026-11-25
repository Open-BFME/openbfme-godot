// OpenBFME. GPL-3.0.
// PartTheHeavensUpdate + FunctionCurve + ProjectedShadowManager tests (both editions; RW 0x8B022E / BFME2 0x4ACBB3).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/FunctionCurve.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/ProjectedShadowManager.h"
#include "GameLogic/Module/PartTheHeavensUpdate.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kPartHeavensIni[] =
	"Object TestPartHeavens\n"
	"  KindOf = INERT IMMOBILE\n"
	"  Behavior = PartTheHeavensUpdate HeavensTag\n"
	"    Texture = EXSunRay\n"
	"    Color = R:255 G:200 B:100\n"
	"    Radius = FCurve\n"
	"      InPadding = HOLD\n"
	"      OutPadding = HOLD\n"
	"      Key = T:0 V:10\n"
	"      Key = T:4 V:50\n"
	"    End\n"
	"    Opacity = FCurve\n"
	"      InPadding = HOLD\n"
	"      OutPadding = HOLD\n"
	"      Key = T:0 V:1.0\n"
	"      Key = T:4 V:0.0\n"
	"    End\n"
	"    Angle = 90\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("part the heavens: samples FunctionCurve radius/angle/opacity, updates ProjectedShadowManager, and round-trips xfer in both editions")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		ProjectedShadowManager shadowMgr;
		combattest::CombatWorld w(kPartHeavensIni);
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));

		Object *obj = w.unit("TestPartHeavens", 'A', 200.0f, 200.0f);
		REQUIRE(obj != nullptr);
		auto *mod = dynamic_cast<PartTheHeavensUpdate *>(obj->findModule("PartTheHeavensUpdate"));
		REQUIRE(mod != nullptr);

		// Frame 0 (elapsed == 0): creates emitter and samples t = 0
		mod->update();
		REQUIRE(mod->shadow() != nullptr);
		CHECK(shadowMgr.shadows().size() == 1);
		CHECK(mod->currentRadius() == doctest::Approx(20.0f)); // tolerance: Hermite curve float evaluation
		CHECK(mod->currentOpacity() == 255);
		CHECK(mod->shadow()->m_58->m_sizeX == doctest::Approx(20.0f)); // tolerance: float decal size
		CHECK(mod->shadow()->m_58->m_angle == doctest::Approx(1.5707963f).epsilon(1e-4f)); // tolerance: deg-to-rad conversion
		CHECK(mod->shadow()->m_5c->m_angle == doctest::Approx(-1.5707963f).epsilon(1e-4f)); // tolerance: negated child angle

		// Advance to t = 4: radius = 2 * 50 = 100, opacity = 0
		w.frames(4);
		CHECK(mod->currentRadius() == doctest::Approx(100.0f)); // tolerance: Hermite curve float evaluation
		CHECK(mod->currentOpacity() == 0);

		// xfer round-trip
		const std::uint32_t expectedStart = mod->startFrame();
		XferSave save;
		mod->xfer(save);
		REQUIRE(!save.data().empty());
		XferLoad load(save.data());
		mod->xfer(load);
		CHECK(mod->startFrame() == expectedStart);
	}
}
