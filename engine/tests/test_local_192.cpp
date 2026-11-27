// OpenBFME. GPL-3.0.
// LOCAL-192: Object audio fields SoundStealthOff / SoundStealthOn (StealthUpdate::evaluate RW 0x00777310 ->
// RW 0x007766DD key 0x2B / RW 0x007766C4 key 0x2A; DONOR BFME2 StealthUpdateLogic.cpp 0x003743A2 / 0x00374389)
// and SoundExit / SoundEnter (OpenContain::onRemoving RW 0x008680F4 -> RW 0x00865CB1 key 0x30;
// OpenContain::onContaining RW 0x00865F9A -> RW 0x00865C98 key 0x2F; DONOR BFME2 OpenContain_onContaining.cpp 0x00463097).
//
// Synthetic data only: no retail or mod file is read.
#include "doctest.h"

#include "CombatTestUtil.h"
#include "IniTestUtil.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <memory>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
struct Local192Resolver final : AudioOwnerResolver
{
	const GameLogic &logic;
	explicit Local192Resolver(const GameLogic &l) : logic(l) {}
	bool objectPosition(std::uint32_t id, Coord3D *pos) const override
	{
		if (const Object *o = logic.findObjectByID((ObjectID)id))
		{
			*pos = *o->getPosition();
			return true;
		}
		return false;
	}
	bool drawablePosition(std::uint32_t, Coord3D *) const override { return false; }
};

struct Local192Audio
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;
	Local192Resolver resolver;

	explicit Local192Audio(const GameLogic &logic) : resolver(logic)
	{
		fx.mount({ { "data\\audio\\sounds\\click.wav", "x" } });
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini",
			"AudioSettings\n"
			"  AudioRoot = Data\\Audio\n"
			"  SoundsFolder = Sounds\n"
			"  SoundsExtension = wav\n"
			"  SampleCount2D = 2\n"
			"  SampleCount3D = 8\n"
			"  StreamCount = 2\n"
			"  MinSampleVolume = 10\n"
			"End\n"
			"AudioEvent StealthOnSnd\n"
			"  Sounds = click\n"
			"  Type = world everyone\n"
			"End\n"
			"AudioEvent StealthOffSnd\n"
			"  Sounds = click\n"
			"  Type = world everyone\n"
			"End\n"
			"AudioEvent EnterSnd\n"
			"  Sounds = click\n"
			"  Type = world everyone\n"
			"End\n"
			"AudioEvent ExitSnd\n"
			"  Sounds = click\n"
			"  Type = world everyone\n"
			"End\n");
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 1024u * 1024u);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		device->setFileLength("Data\\Audio\\Sounds\\click.wav", 250.0);
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 1u);
		mgr->setOwnerResolver(&resolver);
		mgr->enableEventLog(true);
		mgr->update(0.0);
		AudioApi::install(mgr.get());
	}
	~Local192Audio() { AudioApi::install(nullptr); }

	std::vector<std::string> takeNames()
	{
		std::vector<std::string> out;
		for (const auto &e : mgr->takeEventLog())
		{
			out.push_back(e.event);
		}
		return out;
	}
	void clear() { (void)mgr->takeEventLog(); }
};

const char kLocal192Objects[] =
	"Object L192StealthUnit\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 150\n"
	"  SoundStealthOn = StealthOnSnd\n"
	"  SoundStealthOff = StealthOffSnd\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 15\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = StealthUpdate ModuleTag_Stealth\n"
	"    StealthDelay = 0\n"
	"    InnateStealth = Yes\n"
	"    StealthForbiddenConditions = TAKING_DAMAGE\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	"Object L192SilentStealthUnit\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VisionRange = 150\n"
	"  SoundStealthOn = NoSound\n"
	"  SoundStealthOff = NoSound\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 15\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = StealthUpdate ModuleTag_Stealth\n"
	"    StealthDelay = 0\n"
	"    InnateStealth = Yes\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	"Object L192Transport\n"
	"  KindOf = VEHICLE TRANSPORT SELECTABLE\n"
	"  VisionRange = 150\n"
	"  SoundEnter = EnterSnd\n"
	"  SoundExit = ExitSnd\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 15\n"
	"  GeometryMinorRadius = 15\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 300\n"
	"  End\n"
	"  Behavior = TransportContain ModuleTag_Contain\n"
	"    ContainMax = 4\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	"Object L192SilentTransport\n"
	"  KindOf = VEHICLE TRANSPORT SELECTABLE\n"
	"  VisionRange = 150\n"
	"  SoundEnter = NoSound\n"
	"  SoundExit = NoSound\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 15\n"
	"  GeometryMinorRadius = 15\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 300\n"
	"  End\n"
	"  Behavior = TransportContain ModuleTag_Contain\n"
	"    ContainMax = 4\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	"Object L192Passenger\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VisionRange = 100\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 15\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n";

int countSound(const std::vector<std::string> &names, const char *target)
{
	int n = 0;
	for (const std::string &s : names)
	{
		if (s == target)
		{
			++n;
		}
	}
	return n;
}
} // namespace

// RW 0x00777310 (StealthUpdate::evaluate):
//   - entering STEALTHED -> SoundStealthOn (RW 0x00777726 -> RW 0x007766C4, key 0x2A)
//   - leaving STEALTHED -> SoundStealthOff once (RW 0x007777D0 -> RW 0x007766DD, key 0x2B; not replayed by markAsDetected)
//   - detection while STEALTHED -> SoundStealthOff once (RW 0x00777891 -> RW 0x007766DD)
//   - clearing DETECTED -> SoundStealthOn only when isLocallyControlled() (RW 0x0077790D -> 0x0068B749, RW 0x00777921 -> 0x007766C4)
TEST_CASE("LOCAL-192 SoundStealthOff and SoundStealthOn: StealthUpdate plays template sounds on transitions")
{
	CombatWorld w(kLocal192Objects);
	Local192Audio audio(*w.logic);
	w.players.setLocalPlayer(w.teamA()->getControllingPlayer());

	Object *u = w.unit("L192StealthUnit", 'A', 100.0f, 100.0f);
	REQUIRE(u != nullptr);
	StealthUpdate *su = StealthUpdate::of(*u);
	REQUIRE(su != nullptr);

	audio.clear();
	for (int ph = 1; ph <= 6; ++ph) w.logic->update(ph);
	CHECK(u->testStatus((unsigned)CombatNames::status("STEALTHED")));
	{ auto ev = audio.takeNames(); CHECK(countSound(ev, "StealthOnSnd") == 1); CHECK(countSound(ev, "StealthOffSnd") == 0); }

	// Detected while stealthed: plays SoundStealthOff once; a second markAsDetected while already DETECTED does not replay it.
	audio.clear();
	su->markAsDetected(2u, 2, nullptr, false);
	CHECK(u->testStatus((unsigned)CombatNames::status("DETECTED")));
	CHECK(countSound(audio.takeNames(), "StealthOffSnd") == 1);
	su->markAsDetected(2u, 2, nullptr, false);
	CHECK(countSound(audio.takeNames(), "StealthOffSnd") == 0);

	// Advance until detection expires: local player hears SoundStealthOn (RW 0x0077790D / 0x00777921).
	audio.clear();
	for (int i = 0; i < 5; ++i)
	{
		for (int ph = 1; ph <= 6; ++ph) w.logic->update(ph);
	}
	CHECK(!u->testStatus((unsigned)CombatNames::status("DETECTED")));
	CHECK(countSound(audio.takeNames(), "StealthOnSnd") == 1);

	// Non-local player clearing DETECTED does NOT hear SoundStealthOn (RW 0x0077790D -> 0x0068B749).
	Object *enemy = w.unit("L192StealthUnit", 'B', 200.0f, 100.0f);
	REQUIRE(enemy != nullptr);
	StealthUpdate *esu = StealthUpdate::of(*enemy);
	REQUIRE(esu != nullptr);
	for (int ph = 1; ph <= 6; ++ph) w.logic->update(ph);
	REQUIRE(enemy->testStatus((unsigned)CombatNames::status("STEALTHED")));
	esu->markAsDetected(2u, 2, nullptr, false);
	audio.clear();
	for (int i = 0; i < 5; ++i)
	{
		for (int ph = 1; ph <= 6; ++ph) w.logic->update(ph);
	}
	CHECK(!enemy->testStatus((unsigned)CombatNames::status("DETECTED")));
	CHECK(countSound(audio.takeNames(), "StealthOnSnd") == 0);

	// Leaving STEALTHED via forbidden condition (script unstealth bit 8 RW 0x7764C7): plays SoundStealthOff once (not doubled).
	audio.clear();
	u->setScriptStatus(8u, true);
	for (int ph = 1; ph <= 6; ++ph) w.logic->update(ph);
	CHECK(!u->testStatus((unsigned)CombatNames::status("STEALTHED")));
	CHECK(countSound(audio.takeNames(), "StealthOffSnd") == 1);
}

// RW 0x00865F9A (OpenContain::onContaining -> RW 0x00865C98 key 0x2F SoundEnter) and
// RW 0x008680F4 (OpenContain::onRemoving -> RW 0x00865CB1 key 0x30 SoundExit).
TEST_CASE("LOCAL-192 SoundExit and SoundEnter: OpenContain plays container sounds on enter and exit")
{
	CombatWorld w(kLocal192Objects);
	Local192Audio audio(*w.logic);

	Object *tr = w.unit("L192Transport", 'A', 100.0f, 100.0f);
	Object *pax = w.unit("L192Passenger", 'A', 105.0f, 100.0f);
	REQUIRE(tr != nullptr);
	REQUIRE(pax != nullptr);
	OpenContain *oc = dynamic_cast<OpenContain *>(tr->getContain());
	REQUIRE(oc != nullptr);

	audio.clear();
	oc->addToContain(pax);
	{ auto ev = audio.takeNames(); CHECK(countSound(ev, "EnterSnd") == 1); CHECK(countSound(ev, "ExitSnd") == 0); }

	audio.clear();
	oc->removeFromContain(pax);
	{ auto ev = audio.takeNames(); CHECK(countSound(ev, "ExitSnd") == 1); CHECK(countSound(ev, "EnterSnd") == 0); }
}

// Neighbouring behaviour: NoSound templates stay silent on stealth transitions and container enter/exit (RW 0x0073AB45).
TEST_CASE("LOCAL-192 SoundStealthOff and SoundExit: NoSound templates emit no audio events")
{
	CombatWorld w(kLocal192Objects);
	Local192Audio audio(*w.logic);
	w.players.setLocalPlayer(w.teamA()->getControllingPlayer());

	Object *su = w.unit("L192SilentStealthUnit", 'A', 100.0f, 100.0f);
	Object *tr = w.unit("L192SilentTransport", 'A', 150.0f, 100.0f);
	Object *pax = w.unit("L192Passenger", 'A', 155.0f, 100.0f);
	REQUIRE(su != nullptr);
	REQUIRE(tr != nullptr);
	REQUIRE(pax != nullptr);

	audio.clear();
	for (int ph = 1; ph <= 6; ++ph) w.logic->update(ph);
	StealthUpdate::of(*su)->markAsDetected(2u, 2, nullptr, false);
	su->setScriptStatus(8u, true);
	for (int ph = 1; ph <= 6; ++ph) w.logic->update(ph);
	OpenContain *oc = dynamic_cast<OpenContain *>(tr->getContain());
	REQUIRE(oc != nullptr);
	oc->addToContain(pax);
	oc->removeFromContain(pax);
	CHECK(audio.takeNames().empty());
}
