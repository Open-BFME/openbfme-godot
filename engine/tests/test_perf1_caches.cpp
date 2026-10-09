// OpenBFME unit tests. GPL-3.0.
// Lane PERF-1 review r1 (Sol's probes): client caches must observe every change of what they cache. Round 1 kept the radar rectangle per APT frame
// (a clip hidden or moved by a script within the frame was missed) and scanned the objects' ambient sounds once per logic frame (an object created
// between logic frames started its ambient a logic frame late); both were reverted to the uncached code, and these pin the behaviour.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/LiveGameAudio.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <functional>
#include <string>

using namespace hudtest;

TEST_CASE("perf1 caches: InGameHud::radarSquare follows a radar clip a script hides within the same APT frame")
{
	if (!haveWorld("perf1 caches: radar"))
	{
		return;
	}
	SharedWorld &s = shared();
	auto scope = s.world->enterContext();
	Rig r(s);
	RecordingShellServices services;
	InGameHud::Config cfg{ *r.game, *s.world, *s.mount->fs, services, r.view, s.mouse, s.meta, nullptr };
	InGameHud hud(cfg);
	std::string err;
	REQUIRE_MESSAGE(hud.boot(&err), err);
	for (int i = 0; i < 40; ++i)
	{
		hud.update(0.033);
		r.game->advance(0.033);
	}
	float sq[4];
	REQUIRE(hud.radarSquare(sq));
	AptRenderList list;
	hud.apt().buildRenderList(list);
	std::string path;
	for (const AptRenderCommand &c : list.commands)
	{
		if (c.nativeTag && c.symbolName == "AptPalantir::RenderRadar")
		{
			path = c.path;
			break;
		}
	}
	REQUIRE(!path.empty());
	AptCharacterInst *root = hud.apt().level(hud.palantir()->level());
	REQUIRE(root);
	std::function<AptCharacterInst *(AptCharacterInst *)> visit = [&](AptCharacterInst *inst) -> AptCharacterInst * {
		if (inst->targetPath() == path)
		{
			return inst;
		}
		if (AptSpriteInst *sprite = dynamic_cast<AptSpriteInst *>(inst))
		{
			for (AptCharacterInst *child : sprite->children())
			{
				if (AptCharacterInst *match = visit(child))
				{
					return match;
				}
			}
		}
		return nullptr;
	};
	AptCharacterInst *clip = visit(root);
	REQUIRE(clip);
	const auto frame = hud.apt().frameCount();
	clip->setMember("_visible", AptValue::boolean(false));
	hud.apt().buildRenderList(list);
	bool found = false;
	for (const AptRenderCommand &c : list.commands)
	{
		found = found || (c.nativeTag && c.symbolName == "AptPalantir::RenderRadar");
	}
	REQUIRE_FALSE(found);
	REQUIRE(hud.apt().frameCount() == frame); // the same APT frame
	CHECK_FALSE(hud.radarSquare(sq));
}

TEST_CASE("perf1 caches: LiveGameAudio starts the ambient of an object created between logic frames at the next update")
{
	if (!haveWorld("perf1 caches: ambient"))
	{
		return;
	}
	SharedWorld &s = shared();
	auto scope = s.world->enterContext();
	Rig r(s);
	AudioAssetCache cache(s.mount->fs.get(), 16u << 20);
	SimulatedAudioDevice device(&cache);
	AudioManager audio(const_cast<AudioIniState &>(s.world->audio()), cache, device, RandomAlgorithm::ZH_CarryChain, 7);
	LiveGameAudio att(*r.game, audio);
	att.update();
	const auto frame = r.logic().getFrame();
	Object *o = r.make("MordorLumberMill", 1200, 1200);
	REQUIRE(o);
	REQUIRE(!LiveGameAudio::ambientSoundFor(*o).empty());
	audio.setListenerPosition(*o->getPosition(), Coord3D{ 0, 1, 0 });
	att.update();
	REQUIRE(r.logic().getFrame() == frame); // no logic frame ran
	CHECK(att.playingAmbients().count(o->getID()) == 1);
	r.logic().runLogicFrame();
	att.update();
	CHECK(att.playingAmbients().count(o->getID()) == 1);
}
