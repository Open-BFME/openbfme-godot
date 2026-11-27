// OpenBFME unit tests (lane OBJSOUND-1, stop S-707): the object sounds of a damage state change (ActiveBody::attemptDamage RW 0x8C4422 .. 0x8C44C2:
// SoundOnDamaged / SoundOnReallyDamaged) on synthetic data, the retail templates' rows against the retail audio table, and the binary facts the port
// relies on (RW_GAME_DAT), among them that the caller found for SoundPromotedVeteran / Elite / Hero is unreferenced.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "HudTestUtil.h"
#include "IniTestUtil.h"
#include "PeImage.h"
#include "RetailTestMount.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "GameClient/SoundSelectorClientBehavior.h"
#include "GameLogic/Module/ActiveBody.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace combattest;
using retailtest::PeImage;

namespace
{
std::string silentWav(double ms)
{
	const std::uint32_t rate = 22050, frames = (std::uint32_t)(ms * rate / 1000.0);
	std::string out;
	auto u32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
		{
			out.push_back((char)(v >> (8 * i)));
		}
	};
	auto u16 = [&](std::uint16_t v) {
		out.push_back((char)v);
		out.push_back((char)(v >> 8));
	};
	out += "RIFF";
	u32(36 + frames * 2);
	out += "WAVEfmt ";
	u32(16);
	u16(1);
	u16(1);
	u32(rate);
	u32(rate * 2);
	u16(2);
	u16(16);
	out += "data";
	u32(frames * 2);
	out.append(frames * 2, '\0');
	return out;
}

const char kAudio[] =
	"AudioSettings\n  AudioRoot = Data\\Audio\n  SoundsFolder = Sounds\n  MusicFolder = Tracks\n  StreamingFolder = Speech\n  AmbientStreamFolder = AmbientStreams\n  SoundsExtension = wav\n"
	"  SampleCount2D = 4\n  SampleCount3D = 4\n  StreamCount = 3\n  MinSampleVolume = 2%\n  GlobalMinRange = 5000\n  GlobalMaxRange = 5000000\n  TimeToFadeAudio = 300\n"
	"  AmbientStreamHysteresisVolume = 10\n  DefaultSoundVolume = 100%\n  DefaultVoiceVolume = 100%\n  DefaultMusicVolume = 100%\n  DefaultAmbientVolume = 100%\n  DefaultMovieVolume = 100%\nEnd\n"
	"AudioEvent DefaultSoundEffect\n  Volume = 100\n  Priority = high\n  Limit = 4\n  MinRange = 160\n  MaxRange = 640\n  PitchShift = 0 0\n  PlayPercent = 100\n  Type = world everyone\n  Type = +DEFAULT\nEnd\n"
	"MusicTrack DefaultMusicTrack\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  SubmixSlider = music\nEnd\n"
	"DialogEvent DefaultDialog\n  Volume = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n"
	"AmbientStream DefaultAmbientStream\n  Filename = NoFilename\n  Volume = 100\n  Type = DEFAULT\n  MinRange = 100\n  MaxRange = 1000\n  SubmixSlider = ambient\nEnd\n"
	"AudioEvent ProbeHurt\n  Sounds = probe\n  Limit = 0\nEnd\n"
	"AudioEvent ProbeDying\n  Sounds = probe\n  Limit = 0\nEnd\n"
	"AudioEvent ProbeVeteran\n  Sounds = probe\n  Limit = 0\nEnd\n";

// the shapes of the retail rows: a plain event, the +SOUND: form (RW 0x73AC14), NoSound, a template without the rows
const char kProbes[] =
	"Object SoundProbe\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n    MaxHealthDamaged = 75\n    MaxHealthReallyDamaged = 50\n  End\n"
	"  SoundOnDamaged = ProbeHurt\n"
	"  SoundOnReallyDamaged = +SOUND:ProbeDying\n"
	"  SoundPromotedVeteran = ProbeVeteran\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 10\n  GeometryHeight = 10\n"
	"End\n"
	"Object SilentProbe\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n    MaxHealthDamaged = 75\n    MaxHealthReallyDamaged = 50\n  End\n"
	"  SoundOnDamaged = NoSound\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 10\n  GeometryHeight = 10\n"
	"End\n";

// an installed AudioManager whose request log shows what the logic asked for
struct SoundRig
{
	initest::Fixture fx;
	AudioIniState ini;
	std::unique_ptr<AudioAssetCache> cache;
	std::unique_ptr<SimulatedAudioDevice> device;
	std::unique_ptr<AudioManager> mgr;
	AudioManager *saved = nullptr;
	SoundRig()
	{
		fx.mount({ { "data\\audio\\sounds\\probe.wav", silentWav(300) } });
		ini.registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "audio.ini", kAudio);
		REQUIRE_MESSAGE(err.empty(), err);
		cache = std::make_unique<AudioAssetCache>(&fx.fsys, 16u << 20);
		device = std::make_unique<SimulatedAudioDevice>(cache.get());
		mgr = std::make_unique<AudioManager>(ini, *cache, *device, RandomAlgorithm::ZH_CarryChain, 7);
		mgr->update(0.0);
		mgr->enableEventLog(true);
		saved = AudioApi::current();
		AudioApi::install(mgr.get());
	}
	~SoundRig() { AudioApi::install(saved); }
	// the request lines (play == false) since the last call
	std::vector<AudioLogEntry> requests()
	{
		std::vector<AudioLogEntry> out;
		for (AudioLogEntry &e : mgr->takeEventLog())
		{
			if (!e.play && !e.volume)
			{
				out.push_back(std::move(e));
			}
		}
		return out;
	}
};

DamageInfo hit(float amount, int type)
{
	DamageInfo d;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	return d;
}
} // namespace

TEST_CASE("object sounds: damageStateSound reads SoundOnDamaged / SoundOnReallyDamaged by the new state, NoSound and +SOUND: as RW 0x73AB45 left them")
{
	CombatWorld w(kProbes);
	const ThingTemplate &probe = *w.w.get("SoundProbe");
	CHECK(ActiveBody::damageStateSound(probe, BODY_PRISTINE).empty());
	CHECK(ActiveBody::damageStateSound(probe, BODY_DAMAGED) == "ProbeHurt");
	CHECK(ActiveBody::damageStateSound(probe, BODY_REALLYDAMAGED) == "ProbeDying"); // the +SOUND: prefix is dropped (RW 0x73AC14)
	CHECK(ActiveBody::damageStateSound(probe, BODY_RUBBLE).empty());               // RW 0x8C4460: only states 1 and 2 have a row
	// the retail case counts SoundPromoted* rows through findField and finds none: a set row is found that way
	const FieldValue *promotedRow = probe.findField("SoundPromotedVeteran");
	REQUIRE(promotedRow != nullptr);
	REQUIRE(std::get_if<RawTokens>(promotedRow) != nullptr);
	CHECK(std::get_if<RawTokens>(promotedRow)->tokens.front() == "ProbeVeteran");
	const ThingTemplate &silent = *w.w.get("SilentProbe");
	CHECK(ActiveBody::damageStateSound(silent, BODY_DAMAGED).empty());       // NoSound
	CHECK(ActiveBody::damageStateSound(silent, BODY_REALLYDAMAGED).empty()); // never set
}

TEST_CASE("object sounds: a hit that changes the damage state plays the new state's sound at the object for its owner (RW 0x8C4422 .. 0x8C44C2)")
{
	SoundRig r;
	CombatWorld w(kProbes);
	Object *o = w.unit("SoundProbe", 'B', 400, 300);
	REQUIRE(o != nullptr);
	r.requests(); // whatever the spawn asked for
	const int bob = w.playerOf('B')->getPlayerIndex();

	DamageInfo scratch = hit(10.0f, DAMAGE_SLASH); // 100 -> 90: still PRISTINE
	o->attemptDamage(scratch);
	CHECK(r.requests().empty());

	DamageInfo first = hit(20.0f, DAMAGE_SLASH); // 90 -> 70: DAMAGED
	o->attemptDamage(first);
	REQUIRE(o->getBodyModule()->getDamageState() == BODY_DAMAGED);
	std::vector<AudioLogEntry> req = r.requests();
	REQUIRE(req.size() == 1);
	CHECK(req[0].event == "ProbeHurt");
	CHECK(req[0].objectId == o->getID()); // attached to the object (setObjectID RW 0x6DA6A7)
	CHECK(req[0].player == bob);          // the object's controlling player (RW 0x6DB566)

	DamageInfo again = hit(5.0f, DAMAGE_SLASH); // 70 -> 65: DAMAGED stays, no sound
	o->attemptDamage(again);
	CHECK(r.requests().empty());

	DamageInfo second = hit(30.0f, DAMAGE_SLASH); // 65 -> 35: REALLYDAMAGED
	o->attemptDamage(second);
	req = r.requests();
	REQUIRE(req.size() == 1);
	CHECK(req[0].event == "ProbeDying");
	CHECK(req[0].objectId == o->getID());

	// healing back to DAMAGED and PRISTINE plays nothing: attemptHealing (RW 0x8C3131) has no such call
	DamageInfo heal = hit(60.0f, DAMAGE_HEALING);
	o->attemptDamage(heal);
	CHECK(o->getBodyModule()->getDamageState() == BODY_PRISTINE);
	CHECK(r.requests().empty());
}

TEST_CASE("object sounds: a hit straight from PRISTINE to REALLYDAMAGED plays only SoundOnReallyDamaged; a state change outside attemptDamage plays nothing")
{
	SoundRig r;
	CombatWorld w(kProbes);
	Object *o = w.unit("SoundProbe", 'B', 400, 300);
	REQUIRE(o != nullptr);
	r.requests();
	DamageInfo big = hit(60.0f, DAMAGE_SLASH); // 100 -> 40
	o->attemptDamage(big);
	REQUIRE(o->getBodyModule()->getDamageState() == BODY_REALLYDAMAGED);
	std::vector<AudioLogEntry> req = r.requests();
	REQUIRE(req.size() == 1);
	CHECK(req[0].event == "ProbeDying");

	Object *p = w.unit("SoundProbe", 'A', 500, 300);
	REQUIRE(p != nullptr);
	r.requests();
	ActiveBody *body = dynamic_cast<ActiveBody *>(p->getBodyModule());
	REQUIRE(body != nullptr);
	body->internalChangeHealth(-60.0f); // construction / repair / scripts: setCorrectDamageState (RW 0x8C2A5D) alone
	CHECK(body->getDamageState() == BODY_REALLYDAMAGED);
	CHECK(r.requests().empty());

	Object *s = w.unit("SilentProbe", 'A', 600, 300);
	REQUIRE(s != nullptr);
	r.requests();
	DamageInfo cut = hit(30.0f, DAMAGE_SLASH); // DAMAGED with SoundOnDamaged = NoSound
	s->attemptDamage(cut);
	CHECK(s->getBodyModule()->getDamageState() == BODY_DAMAGED);
	CHECK(r.requests().empty());
}

TEST_CASE("object sounds retail: the damage rows name retail audio events, and no sound selector overrides them (S-707)")
{
	if (!hudtest::haveWorld("object sounds retail"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	const AudioIniState &audio = s.world->audio();
	const int rows[] = { SoundSelector::rowIndex("SoundOnDamaged"), SoundSelector::rowIndex("SoundOnReallyDamaged"), SoundSelector::rowIndex("SoundPromotedVeteran"),
		SoundSelector::rowIndex("SoundPromotedElite"), SoundSelector::rowIndex("SoundPromotedHero") };
	REQUIRE(rows[0] == 45); // RW 0x8C22C0 pushes 0x2D
	REQUIRE(rows[1] == 46); // RW 0x8C22D9 pushes 0x2E
	REQUIRE(rows[2] == 49); // RW 0x8C22F2 pushes 0x31
	REQUIRE(rows[3] == 50); // RW 0x8C230B pushes 0x32
	REQUIRE(rows[4] == 51); // RW 0x8C2324 pushes 0x33
	size_t damaged = 0, reallyDamaged = 0, either = 0, promoted = 0, selectorOverrides = 0;
	std::vector<std::string> unknown;
	std::map<std::string, size_t> eventTypes; // the Type flags of the damage events (who may hear them: shouldPlayLocally RW 0x4533A9)
	std::map<std::string, size_t> eventNames;
	for (const ThingTemplate *tt : s.world->things().templates())
	{
		const std::string d = ActiveBody::damageStateSound(*tt, BODY_DAMAGED);
		const std::string rd = ActiveBody::damageStateSound(*tt, BODY_REALLYDAMAGED);
		damaged += d.empty() ? 0 : 1;
		reallyDamaged += rd.empty() ? 0 : 1;
		either += d.empty() && rd.empty() ? 0 : 1;
		for (const std::string &e : { d, rd })
		{
			if (e.empty())
			{
				continue;
			}
			const auto info = audio.infos.find(e);
			if (!info)
			{
				unknown.push_back(tt->getName() + "=" + e);
				continue;
			}
			char flags[16];
			std::snprintf(flags, sizeof flags, "0x%x", (unsigned)info->type);
			++eventTypes[flags];
			++eventNames[e];
		}
		for (const char *p : { "SoundPromotedVeteran", "SoundPromotedElite", "SoundPromotedHero" })
		{
			const FieldValue *f = tt->findField(p);
			const RawTokens *raw = f ? std::get_if<RawTokens>(f) : nullptr;
			promoted += raw && !raw->tokens.empty() && AsciiStringUtil::compareNoCase(raw->tokens.front(), "NoSound") != 0 ? 1 : 0;
		}
		// RW 0x676A8A asks the drawable's selectors first: any selector entry that sets one of these rows would win over the template
		for (const ThingTemplate::Nugget &n : tt->getFinalOverride()->clientBehaviorModules().nuggets())
		{
			const ModuleData *data = n.data.get();
			auto sets = [&](const SoundSelector::SoundSet &set) {
				for (int row : rows)
				{
					selectorOverrides += set.rows[(size_t)row].sound.empty() ? 0 : 1;
				}
			};
			if (const auto *mc = dynamic_cast<const ModelConditionSoundSelectorClientBehaviorModuleData *>(data))
			{
				for (const auto &e : mc->m_entries)
				{
					sets(e.sounds);
				}
			}
			else if (const auto *up = dynamic_cast<const UpgradeSoundSelectorClientBehaviorModuleData *>(data))
			{
				for (const auto &e : up->m_entries)
				{
					sets(e.sounds);
				}
			}
			else if (const auto *rnd = dynamic_cast<const RandomSoundSelectorClientBehaviorModuleData *>(data))
			{
				sets(rnd->m_sounds);
			}
		}
	}
	std::printf("object sounds retail: %zu templates with SoundOnDamaged, %zu with SoundOnReallyDamaged (%zu either), %zu SoundPromoted* rows (not ported), "
				"%zu selector overrides, %zu unknown events\n",
		damaged, reallyDamaged, either, promoted, selectorOverrides, unknown.size());
	for (const auto &kv : eventTypes)
	{
		std::printf("  damage event Type %s: %zu\n", kv.first.c_str(), kv.second);
	}
	std::vector<std::pair<size_t, std::string>> common;
	for (const auto &kv : eventNames)
	{
		common.emplace_back(kv.second, kv.first);
	}
	std::sort(common.rbegin(), common.rend());
	std::printf("  %zu distinct damage events, the most used:", eventNames.size());
	for (size_t i = 0; i < common.size() && i < 6; ++i)
	{
		std::printf(" %s (%zu)", common[i].second.c_str(), common[i].first);
	}
	std::printf("\n");
	for (size_t i = 0; i < unknown.size() && i < 10; ++i)
	{
		std::printf("  unknown %s\n", unknown[i].c_str());
	}
	CHECK(damaged == 1062); // RotWK 2.01
	CHECK(reallyDamaged == 1065);
	CHECK(either == 1069);
	CHECK(promoted == 0);
	CHECK(unknown.empty());          // retail validates a sound row at parse time (RW 0x73AC45 "Invalid Sound")
	CHECK(selectorOverrides == 0);   // the port reads the template row (ActiveBody::damageStateSound)
}

TEST_CASE("object sounds binary facts: the damage rows' call sites, and the caller found for SoundPromoted* is unreferenced (RW_GAME_DAT)")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("object sounds binary facts (RW_GAME_DAT unset)");
		return;
	}
	// the drawable's row getters: push ebp; mov ebp, esp; push ecx; and [ebp-4], 0; push <row>; push [ebp+8]; call RW 0x676A8A
	CHECK(pe->hex(0x8C22C0, 14) == "558bec518365fc006a2dff7508e8"); // SoundOnDamaged (45)
	CHECK(pe->hex(0x8C22D9, 14) == "558bec518365fc006a2eff7508e8"); // SoundOnReallyDamaged (46)
	CHECK(pe->hex(0x8C22F2, 14) == "558bec518365fc006a31ff7508e8"); // SoundPromotedVeteran (49)
	CHECK(pe->hex(0x8C230B, 14) == "558bec518365fc006a32ff7508e8"); // SoundPromotedElite (50)
	CHECK(pe->hex(0x8C2324, 14) == "558bec518365fc006a33ff7508e8"); // SoundPromotedHero (51)
	// attemptDamage: mov edx, [esi+0x20] (the new state); cmp edx, 1; jne -> SoundOnDamaged; cmp edx, 2 -> SoundOnReallyDamaged; TheAudio vslot 0x64
	CHECK(pe->hex(0x8C4436, 6) == "8b562083fa01");
	CHECK(pe->hex(0x8C4442, 5) == "e879deffff"); // call RW 0x8C22C0
	CHECK(pe->hex(0x8C4460, 3) == "83fa02");
	CHECK(pe->hex(0x8C4469, 5) == "e86bdeffff"); // call RW 0x8C22D9
	CHECK(pe->hex(0x8C44B5, 3) == "ff5064");
	// the promoted rows: ActiveBody's body interface slot 0x30 (RW 0x8C261D, the vtable at RW 0xC71ED0 whose slot 0x5C is setMaxHealth RW 0x8C1CD5)
	CHECK(pe->u32At(0xC71ED0 + 0x30) == 0x8C261D);
	CHECK(pe->u32At(0xC71ED0 + 0x5C) == 0x8C1CD5);
	CHECK(pe->hex(0x8C268F, 5) == "e890fcffff"); // call RW 0x8C2324
	CHECK(pe->hex(0x8C26B2, 5) == "e854fcffff"); // call RW 0x8C230B
	CHECK(pe->hex(0x8C26D5, 5) == "e818fcffff"); // call RW 0x8C22F2
	// the caller found: Object + 0x25C -> vslot 0x30 in RW 0x6957B9 (ZH Object::onVeterancyLevelChanged), without a direct call, jump or pointer to it
	CHECK(pe->hex(0x6957FD, 6) == "ff7508ff5030");
	const std::uint32_t textStart = 0x401000, textSize = 0x7CF000;
	std::vector<std::uint8_t> text;
	REQUIRE(pe->read(textStart, textSize, &text));
	size_t calls = 0;
	for (size_t i = 0; i + 5 <= text.size(); ++i)
	{
		if (text[i] != 0xE8 && text[i] != 0xE9)
		{
			continue;
		}
		const std::int32_t rel = (std::int32_t)((std::uint32_t)text[i + 1] | ((std::uint32_t)text[i + 2] << 8) | ((std::uint32_t)text[i + 3] << 16) | ((std::uint32_t)text[i + 4] << 24));
		calls += textStart + (std::uint32_t)i + 5u + (std::uint32_t)rel == 0x6957B9u ? 1 : 0;
	}
	CHECK(calls == 0);
	size_t pointers = 0;
	for (size_t i = 0; i + 4 <= text.size(); ++i)
	{
		pointers += ((std::uint32_t)text[i] | ((std::uint32_t)text[i + 1] << 8) | ((std::uint32_t)text[i + 2] << 16) | ((std::uint32_t)text[i + 3] << 24)) == 0x6957B9u ? 1 : 0;
	}
	CHECK(pointers == 0);
	std::vector<std::uint8_t> rdata; // the vtables
	REQUIRE(pe->read(0xBD0000, 0x1B9000, &rdata));
	size_t slots = 0;
	for (size_t i = 0; i + 4 <= rdata.size(); ++i)
	{
		slots += ((std::uint32_t)rdata[i] | ((std::uint32_t)rdata[i + 1] << 8) | ((std::uint32_t)rdata[i + 2] << 16) | ((std::uint32_t)rdata[i + 3] << 24)) == 0x6957B9u ? 1 : 0;
	}
	CHECK(slots == 0);
}
