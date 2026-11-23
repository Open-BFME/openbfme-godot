// OpenBFME unit tests for Local 63 (issue fork issue 508, P-129; plus P-132 CaptureFlag visibility check):
// Dozer construction completion on a CastleBehavior structure clears only AWAITING_CONSTRUCTION and extends
// PARTIALLY_CONSTRUCTED and ACTIVELY_BEING_CONSTRUCTED for LOGICFRAMES_PER_SECOND (5) frames via SMCHelper
// so structures with DefaultModelConditionState { Model = None } do not drop to an invisible model before
// CastleBehavior unpacks on the finished site.
//
// TARGET FACTS (both editions: RotWK 2.01 game.dat and DONOR BFME2 1.06 game.dat):
//   * RW 0x88DEFD..0x88DF51 / BFME2 0x88AC08..0x88AC5C (DozerAIUpdateCtor.cpp:2358-2369):
//     When DozerAIUpdate completes construction of `goalObject`, it checks goalObject->findModule("CastleBehavior").
//     - If CastleBehavior is present: clears AWAITING_CONSTRUCTION (0x43) and calls
//       setSpecialModelConditionState(PARTIALLY_CONSTRUCTED (0x44), 5) and
//       setSpecialModelConditionState(ACTIVELY_BEING_CONSTRUCTED (0x45), 5).
//     - Otherwise: clears AWAITING_CONSTRUCTION, PARTIALLY_CONSTRUCTED, and ACTIVELY_BEING_CONSTRUCTED immediately.

#include "doctest.h"
#include "HudTestUtil.h"
#include "ProdTestUtil.h"
#include "W3DDrawTestUtil.h"
#include "W3dSynth.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace
{
class NoRandom : public W3DDrawRandom
{
public:
	int value(int lo, int) override { return lo; }
	float real(float lo, float) override { return lo; }
	std::vector<std::string> unverified() const override { return { "LOCAL-63 test double" }; }
};

struct DrawCheck
{
	w3dsynth::MemoryFileSource fs;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<WW3DDrawAssets> drawAssets;
	drawtest::Harness harness;
	NoRandom random;
	W3DModelDrawModuleData *data = nullptr;
	std::unique_ptr<W3DScriptedModelDraw> draw;

	explicit DrawCheck(const char *drawIni)
	{
		using namespace w3dsynth;
		fs.Add("art\\w3d\\l6\\l63_skl.w3d", hierarchyChunk("L63_SKL", { pivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), pivot("BASE", 0, 0, 0, 0) }));
		auto makeBox = [](const char *name) {
			std::vector<std::uint8_t> bytes;
			SynthMesh m;
			m.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 } };
			m.Triangles = { 0, 1, 2 };
			m.Container = name;
			m.Name = "BODY";
			append(bytes, meshChunk(m));
			append(bytes, hlodChunk(name, "L63_SKL", { { std::string(name) + ".BODY", 1 } }));
			return bytes;
		};
		fs.Add("art\\w3d\\l6\\l63_bld.w3d", makeBox("L63_BLD"));
		fs.Add("art\\w3d\\l6\\l63_done.w3d", makeBox("L63_DONE"));
		assets.reset(new WW3DAssetManager(fs));
		drawAssets.reset(new WW3DDrawAssets(*assets));
		data = harness.parse(drawIni);
		REQUIRE_MESSAGE(data != nullptr, harness.error);
		W3DScriptedModelDraw::Options o;
		o.buildBones = false;
		draw.reset(new W3DScriptedModelDraw(*data, *drawAssets, random, nullptr, o));
	}

	std::string syncFromObject(const Object &obj)
	{
		std::string conds;
		const char *names[] = {
			"AWAITING_CONSTRUCTION",
			"PARTIALLY_CONSTRUCTED",
			"ACTIVELY_BEING_CONSTRUCTED",
			"CONSTRUCTION_COMPLETE",
		};
		for (const char *n : names)
		{
			if (obj.testModelCondition(Construction::modelConditionIndex(n)))
			{
				if (!conds.empty())
				{
					conds += ' ';
				}
				conds += n;
			}
		}
		const ModelConditionFlags f = harness.parseFlags(conds.c_str());
		REQUIRE_MESSAGE(harness.error.empty(), harness.error);
		draw->setModelConditionFlags(f);
		return draw->frame().model ? draw->frame().modelName : std::string();
	}
};

const char kSynthObjects[] =
	"Object Local63Dozer\n"
	"  KindOf = PRELOAD SELECTABLE CAN_CAST_REFLECTIONS INFANTRY DOZER\n"
	"  Behavior = DozerAIUpdate ModuleTag_DozerAI\n"
	"  End\n"
	"End\n"
	"Object Local63Unpack\n"
	"  BuildTime = 10.0\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE CASTLE_CENTER\n"
	"  Behavior = GettingBuiltBehavior ModuleTag_GB\n"
	"  End\n"
	"  Behavior = CastleBehavior ModuleTag_Castle\n"
	"    CastleToUnpackForFaction = TestFactionOther Layout_Unused\n"
	"    InstantUnpack = Yes\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    InitialHealth = 1000\n"
	"    MaxHealthDamaged = 500\n"
	"    MaxHealthReallyDamaged = 100\n"
	"  End\n"
	"End\n"
	"Object Local63Plain\n"
	"  BuildTime = 10.0\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Behavior = GettingBuiltBehavior ModuleTag_GB\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    InitialHealth = 1000\n"
	"    MaxHealthDamaged = 500\n"
	"    MaxHealthReallyDamaged = 100\n"
	"  End\n"
	"End\n";

const char kUnpackDraw[] =
	"DefaultModelConditionState\n  Model = None\nEnd\n"
	"ModelConditionState = AWAITING_CONSTRUCTION\n  Model = L63_BLD\nEnd\n"
	"ModelConditionState = ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED\n  Model = L63_BLD\nEnd\n"
	"End\n";

const char kPlainDraw[] =
	"DefaultModelConditionState\n  Model = L63_DONE\nEnd\n"
	"ModelConditionState = ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED\n  Model = L63_BLD\nEnd\n"
	"End\n";

GameMessage selectMsg(int player, ObjectID id)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(id);
	return m;
}

GameMessage dozerMsg(int player, const ThingTemplate &t, const Coord3D &loc, float angle)
{
	GameMessage m(MSG_DOZER_CONSTRUCT, player);
	m.appendIntegerArgument((int)t.getTemplateID());
	m.appendLocationArgument(loc);
	m.appendRealArgument(angle);
	return m;
}

bool isDrawableVisible(const Drawable *d, std::string *modelOut = nullptr, std::string *stateOut = nullptr)
{
	if (!d)
	{
		return false;
	}
	for (const DrawEntry &e : d->entries())
	{
		if (e.kind == W3D_DRAWKIND_MODEL && e.draw)
		{
			const W3DDrawFrame fr = e.draw->frame();
			if (modelOut)
			{
				*modelOut = fr.modelName;
			}
			if (stateOut)
			{
				*stateOut = fr.animationStateName;
			}
			// GodotGameWorld::refresh_view: shown = f.model && !e.moduleHidden && !fogHidden
			// and opacity must be positive to render visibly.
			if (fr.model != nullptr && !e.moduleHidden && d->drawOpacity() > 0.0f) // tolerance: > 0 opacity is visible
			{
				return true;
			}
		}
	}
	return false;
}
} // namespace

TEST_CASE("LOCAL-63 synthetic: Dozer completion holds PARTIALLY_CONSTRUCTED and ACTIVELY_BEING_CONSTRUCTED for 5 frames only on CastleBehavior structures (RW 0x88DEFD / BFME2 0x88AC08)")
{
	prodtest::ProdWorld w;
	w.load(kSynthObjects);

	const int awaiting = Construction::modelConditionIndex("AWAITING_CONSTRUCTION");
	const int partial = Construction::modelConditionIndex("PARTIALLY_CONSTRUCTED");
	const int active = Construction::modelConditionIndex("ACTIVELY_BEING_CONSTRUCTED");
	const int complete = Construction::modelConditionIndex("CONSTRUCTION_COMPLETE");

	Object *dozer = w.make("Local63Dozer", w.teamOf("Alice"));
	Object *unpack = w.make("Local63Unpack", w.teamOf("Alice"));
	Object *plain = w.make("Local63Plain", w.teamOf("Alice"));
	REQUIRE(dozer != nullptr);
	REQUIRE(unpack != nullptr);
	REQUIRE(plain != nullptr);

	for (Object *s : { unpack, plain })
	{
		s->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
		s->setConstructionPercent(99.0f);
		Construction::setModelConditions(*s, {}, { "AWAITING_CONSTRUCTION", "PARTIALLY_CONSTRUCTED", "ACTIVELY_BEING_CONSTRUCTED" });
	}

	DrawCheck unpackDraw(kUnpackDraw);
	DrawCheck plainDraw(kPlainDraw);
	CHECK(unpackDraw.syncFromObject(*unpack) == "L63_BLD");
	CHECK(plainDraw.syncFromObject(*plain) == "L63_BLD");

	Construction::completeConstruction(*unpack, dozer);
	Construction::completeConstruction(*plain, dozer);

	// Non-castle structure clears all three construction flags immediately and shows finished model.
	CHECK_FALSE(plain->testModelCondition(awaiting));
	CHECK_FALSE(plain->testModelCondition(partial));
	CHECK_FALSE(plain->testModelCondition(active));
	CHECK(plain->testModelCondition(complete));
	CHECK(plainDraw.syncFromObject(*plain) == "L63_DONE");

	// CastleBehavior structure clears AWAITING_CONSTRUCTION, but extends PARTIALLY_CONSTRUCTED &
	// ACTIVELY_BEING_CONSTRUCTED for 5 frames (F .. F+4), preserving build model rather than Model = None.
	CHECK_FALSE(unpack->testModelCondition(awaiting));
	CHECK(unpack->testModelCondition(partial));
	CHECK(unpack->testModelCondition(active));
	CHECK(unpack->testModelCondition(complete));
	CHECK(unpackDraw.syncFromObject(*unpack) == "L63_BLD");

	for (int f = 1; f < LOGICFRAMES_PER_SECOND; ++f)
	{
		w.logic->runLogicFrame();
		CHECK(unpack->testModelCondition(partial));
		CHECK(unpack->testModelCondition(active));
		CHECK(unpackDraw.syncFromObject(*unpack) == "L63_BLD");
	}

	// Expiry happens on frame F + 5 (LOGICFRAMES_PER_SECOND elapsed).
	w.logic->runLogicFrame();
	CHECK_FALSE(unpack->testModelCondition(partial));
	CHECK_FALSE(unpack->testModelCondition(active));
	CHECK(unpackDraw.syncFromObject(*unpack).empty());
}

