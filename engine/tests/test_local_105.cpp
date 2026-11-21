// OpenBFME. LOCAL-105 tests (fork issue 776):
//   - FadeAndDieOrnamentUpdate (RW 0x007A772B, 0x007A7801, 0x007A76CF, 0x007A77EA, 0x007A7796, 0x008A7734;
//     OrnamentEnvelopeData RW 0x004C894B / 0x0073A74B / 0x00740659; OCL FADE_AND_DIE_ORNAMENT RW 0x005F2287).

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/FadeAndDieOrnamentUpdate.h"
#include "GameLogic/ObjectCreationList.h"

#include <cmath>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
struct Local105World : CombatWorld
{
	ObjectCreationListStore oclStore{ w.keys };
	ObjectCreationListStore *savedOcl = TheObjectCreationListStore;
	Local105World()
	{
		TheObjectCreationListStore = &oclStore;
		w.fx.env.blocks.registerBlock("ObjectCreationList", [](INI *ini) { ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(ini); });
		FadeAndDieOrnamentUpdate::registerClass(w.modules);
	}
	~Local105World()
	{
		TheObjectCreationListStore = savedOcl;
	}
};

struct OpacityRecorder : public ObjectClientHooks
{
	std::vector<float> opacities;
	void objectCreated(Object &obj) override { obj.friend_bindToClient(this); }
	void objectDestroyed(Object &) override {}
	void setDrawableOpacity(Object &, float opacity) override { opacities.push_back(opacity); }
};

const char *kOrnamentIni =
	"Object Local105Parent\n"
	"  KindOf = HERO\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 15\n"
	"End\n"
	"\n"
	"Object Local105OrnamentFollow\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 10\n"
	"  End\n"
	"  Behavior = FadeAndDieOrnamentUpdate ModuleTag_01\n"
	"    Envelope InitialOpacity = 0.0 PeakOpacity = 1.0 SustainOpacity = 0.5 InitialDelay = 200 AttackTime = 400 DecayTime = 400 SustainTime = 200 ReleaseTime = 400\n"
	"    FollowTarget = Yes\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Die\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 4\n"
	"  GeometryHeight = 10\n"
	"End\n"
	"\n"
	"Object Local105OrnamentStatic\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 10\n"
	"  End\n"
	"  Behavior = FadeAndDieOrnamentUpdate ModuleTag_01\n"
	"    Envelope InitialOpacity = 0.0 PeakOpacity = 1.0 SustainOpacity = 1.0 InitialDelay = 0 AttackTime = 400 DecayTime = 0 SustainTime = 200 ReleaseTime = 400\n"
	"    FollowTarget = No\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Die\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 4\n"
	"  GeometryHeight = 10\n"
	"End\n"
	"\n"
	"ObjectCreationList OCL_Local105Ornament\n"
	"  CreateObject\n"
	"    ObjectNames = Local105OrnamentFollow\n"
	"    Offset = X:0.0 Y:0.0 Z:0.0\n"
	"    Count = 1\n"
	"    Disposition = FADE_AND_DIE_ORNAMENT\n"
	"  End\n"
	"End\n";

FadeAndDieOrnamentUpdate *findOrnament(Object &o)
{
	return dynamic_cast<FadeAndDieOrnamentUpdate *>(o.findModule("FadeAndDieOrnamentUpdate"));
}
} // namespace

TEST_CASE("LOCAL-105 FadeAndDieOrnamentUpdate: ADSR envelope steps opacity and pushes to client hooks (RW 0x007A7801, RW 0x00740659)")
{
	// Expectations from RW 0x00740659 (OrnamentEnvelopeData::update) and RW 0x0073A74B (parseEnvelope at 5 FPS: 200ms = 1 frame, 400ms = 2 frames).
	OpacityRecorder rec;
	Local105World cw;
	const std::string err = cw.w.load(kOrnamentIni, INI_LOAD_OVERWRITE, "local105_ornament.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	cw.logic->setClientHooks(&rec);

	const ThingTemplate *ornTmpl = cw.w.things.findTemplate("Local105OrnamentStatic");
	REQUIRE(ornTmpl != nullptr);
	Object *orn = cw.logic->newObject(ornTmpl, cw.logic->players().getNthPlayer(0)->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(orn != nullptr);
	FadeAndDieOrnamentUpdate *mod = findOrnament(*orn);
	REQUIRE(mod != nullptr);

	// Frame 1: INITIAL_DELAY (0 frames) -> prev(0) >= 0 -> transitions to ATTACK, retail falls through to opacity = 0.0 (RW 0x007407DE)
	cw.logic->runLogicFrame();
	CHECK(mod->envelopeState() == OrnamentEnvelopeData::STATE_ATTACK);
	CHECK(mod->opacity() == 0.0f);

	// Frame 2: ATTACK (2 frames, step = 0.5) -> opacity = 0.5
	cw.logic->runLogicFrame();
	CHECK(mod->envelopeState() == OrnamentEnvelopeData::STATE_ATTACK);
	CHECK(mod->opacity() == 0.5f);

	// Frame 3: ATTACK -> opacity = 1.0, transitions to DECAY
	cw.logic->runLogicFrame();
	CHECK(mod->envelopeState() == OrnamentEnvelopeData::STATE_DECAY);
	CHECK(mod->opacity() == 1.0f);

	// Frame 4: DECAY (decayTime=0 clamped to 1, peak==sustain==1.0) -> transitions to SUSTAIN
	cw.logic->runLogicFrame();
	CHECK(mod->envelopeState() == OrnamentEnvelopeData::STATE_SUSTAIN);
	CHECK(mod->opacity() == 1.0f);

	// Frame 5-6: SUSTAIN (sustainTime=1 frame: prev=0 < 1 stays SUSTAIN; prev=1 >= 1 -> RELEASE)
	for (int i = 0; i < 2; ++i) cw.logic->runLogicFrame();
	CHECK(mod->envelopeState() == OrnamentEnvelopeData::STATE_RELEASE);
	CHECK(mod->opacity() == 1.0f);

	// Frame 7-8: RELEASE (releaseTime=2 frames, step=0.5) -> 0.5 then 0.0 (STATE_DONE)
	cw.logic->runLogicFrame();
	CHECK(mod->opacity() == 0.5f);
	cw.logic->runLogicFrame();
	CHECK(mod->envelopeState() == OrnamentEnvelopeData::STATE_DONE);
	CHECK(mod->opacity() == 0.0f);
	REQUIRE(rec.opacities.size() == 8);
	CHECK(rec.opacities.back() == 0.0f);
	cw.logic->setClientHooks(nullptr);
}

TEST_CASE("LOCAL-105 FadeAndDieOrnamentUpdate: OCL FADE_AND_DIE_ORNAMENT links parent, follows position, kills on parent death (RW 0x005F2287, RW 0x007A76CF, RW 0x007A7801)")
{
	// Expectations from RW 0x005F2287 (OCL -> setParent RW 0x007A76CF) and RW 0x007A7801 (FollowTarget branch).
	Local105World cw;
	const std::string err = cw.w.load(kOrnamentIni, INI_LOAD_OVERWRITE, "local105_ocl.ini");
	REQUIRE_MESSAGE(err.empty(), err);

	Object *parent = cw.unit("Local105Parent", 'A', 200.0f, 300.0f);
	REQUIRE(parent != nullptr);

	const ObjectCreationList *ocl = TheObjectCreationListStore->findObjectCreationList("OCL_Local105Ornament");
	REQUIRE(ocl != nullptr);
	std::vector<Object *> made = ocl->create(*cw.logic, parent, *parent->getPosition());
	REQUIRE(made.size() == 1);
	Object *orn = made[0];
	FadeAndDieOrnamentUpdate *mod = findOrnament(*orn);
	REQUIRE(mod != nullptr);
	CHECK(mod->parentId() == parent->getID());
	CHECK(mod->getParentObject() == parent);

	// Move parent and step: FollowTarget=Yes copies parent's position (RW 0x007A782D).
	Coord3D moved{ 260.0f, 340.0f, 0.0f };
	parent->setPosition(&moved);
	cw.logic->runLogicFrame();
	CHECK(orn->getPosition()->x == 260.0f);
	CHECK(orn->getPosition()->y == 340.0f);
	CHECK(!orn->isEffectivelyDead());

	// Neighbouring behaviour: FollowTarget=No neither follows parent nor dies when parent dies.
	Object *staticOrn = cw.unit("Local105OrnamentStatic", 'A', 50.0f, 50.0f);
	FadeAndDieOrnamentUpdate *staticMod = findOrnament(*staticOrn);
	REQUIRE(staticMod != nullptr);
	staticMod->setParent(parent);

	// Kill parent: FollowTarget=Yes ornament kills itself (RW 0x007A783A -> kill(8, 0)), static ornament survives.
	parent->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	cw.logic->runLogicFrame();
	CHECK(orn->isEffectivelyDead());
	CHECK(!staticOrn->isEffectivelyDead());
	CHECK(staticOrn->getPosition()->x == 50.0f);
}

TEST_CASE("LOCAL-105 FadeAndDieOrnamentUpdate: xfer round-trip preserves parent, opacity and envelope state (RW 0x007A7796, RW 0x008A7734)")
{
	// Expectations from RW 0x007A7796 (xfer) and RW 0x008A7734 (loadPostProcess).
	Local105World cw;
	const std::string err = cw.w.load(kOrnamentIni, INI_LOAD_OVERWRITE, "local105_xfer.ini");
	REQUIRE_MESSAGE(err.empty(), err);

	Object *parent = cw.unit("Local105Parent", 'A', 100.0f, 100.0f);
	Object *orn = cw.unit("Local105OrnamentFollow", 'A', 100.0f, 100.0f);
	FadeAndDieOrnamentUpdate *mod = findOrnament(*orn);
	REQUIRE(mod != nullptr);
	mod->setParent(parent);

	for (int i = 0; i < 3; ++i) cw.logic->runLogicFrame(); // advance into ATTACK
	const float savedOpacity = mod->opacity();
	const int savedState = mod->envelopeState();
	StateHasher h1;
	mod->crc(h1);

	XferSave save;
	mod->xfer(save);

	for (int i = 0; i < 4; ++i) cw.logic->runLogicFrame();
	CHECK((mod->opacity() != savedOpacity || mod->envelopeState() != savedState));

	{
		XferLoad load(save.data());
		mod->xfer(load);
	}
	CHECK(mod->parentId() == parent->getID());
	CHECK(mod->opacity() == savedOpacity);
	CHECK(mod->envelopeState() == savedState);
	StateHasher h2;
	mod->crc(h2);
	CHECK(h2.value() == h1.value());

	// Truncated buffer throws XferException.
	std::vector<std::uint8_t> truncated = save.data();
	truncated.pop_back();
	XferLoad bad(truncated);
	CHECK_THROWS_AS(mod->xfer(bad), XferException);
}
