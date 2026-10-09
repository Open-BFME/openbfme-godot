// OpenBFME. GPL-3.0.
// See GameClient/AnimationSoundClientBehavior.h for the target facts and the addresses (lane AUDIO-4). Client presentation: no simulation state.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameClient/AnimationSoundClientBehavior.h"
#include "GameClient/ClientBehaviorModules.h"

#include "Common/AsciiString.h"
#include "Common/Audio/AudioEventRTS.h"
#include "Common/Audio/GameAudio.h"
#include "Common/INIException.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameClient/Drawable.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <stdexcept>

namespace
{
bool same(const char *a, const char *b)
{
	return AsciiStringUtil::compareNoCase(a, b) == 0; // RW _strcmpi
}

std::string upper(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::toupper((unsigned char)c);
	}
	return s;
}

const float kNoFrame = -9.999999747378752e-06f; // RW 0xBDFF24

#define AS_OFF(member) (int)offsetof(AnimationSoundClientBehaviorModuleData, member)
// RW 0xC75C94
const FieldParse kAnimationSoundFieldParse[] = {
	{ "AnimationSound", AnimationSoundClientBehaviorModuleData::parseAnimationSound, nullptr, 0 },
	{ "MaxUpdateRangeCap", INI::parseReal, nullptr, AS_OFF(m_maxUpdateRangeCap) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef AS_OFF
} // namespace

AnimationSoundClientBehaviorModuleData::AnimationSoundClientBehaviorModuleData()
	: m_maxUpdateRangeCap(3.4028234663852886e+38f) // RW 0x8CEB63: FLT_MAX (RW 0xBD1910)
{
}

void AnimationSoundClientBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kAnimationSoundFieldParse);
}

std::function<bool(const std::string &)> &AnimationSoundClientBehaviorModuleData::soundExists()
{
	static thread_local std::function<bool(const std::string &)> check;
	return check;
}

void AnimationSoundClientBehaviorModuleData::parseAnimationSound(INI *ini, void *instance, void *, const void *)
{
	AnimationSoundClientBehaviorModuleData *data = static_cast<AnimationSoundClientBehaviorModuleData *>(instance);
	if (!data)
	{
		return; // RW 0x8CEBDB: no instance, nothing
	}
	const std::string sound = ini->getNextSubToken("Sound");
	if (sound.empty())
	{
		throw INIException(3, "AnimationSound line: sound name cannot empty");
	}
	// TheAudio's event table (RW vslot 300): the world's parsed table during an object load (ContainParseHooks::audioEventExists, RetailObjectWorld), or a
	// test's own; neither installed is a loud error, never an accepted name (the contain parsers' rule, S-083)
	const std::function<bool(const std::string &)> &exists = soundExists() ? soundExists() : TheContainParseHooks().audioEventExists;
	if (!exists)
	{
		throw INIException(8, "AnimationSound line: TheAudio's event table is not installed");
	}
	if (!exists(sound))
	{
		throw INIException(3, "AnimationSound line: unknown sound '%s'", sound.c_str());
	}
	const char *seps = ini->getSepsColon();
	ModelConditionFlags required, excluded;
	auto setFlag = [](ModelConditionFlags &mask, const char *name) {
		const int bit = ModelCondition::indexOf(name); // RW 0x4B5DA2
		if (bit < 0)
		{
			return false;
		}
		mask.set(bit);
		return true;
	};
	auto expected = [](const char *got) -> void { throw INIException(3, "AnimationSound line: expected '%s' next, got '%s'", "Animation", got ? got : "<End of line>"); };
	// RW 0x8CED04 .. 0x8CEE0D: the optional RequiredMC / ExcludedMC lists (a repeated label inside its own list is skipped)
	const char *tok = ini->getNextTokenOrNull(seps);
	if (tok && same(tok, "RequiredMC"))
	{
		tok = ini->getNextToken(seps);
		for (;;)
		{
			if (!setFlag(required, tok))
			{
				throw INIException(3, "AnimationSound line: unknown model condition '%s' in %s list", tok, "RequiredMC");
			}
			tok = ini->getNextTokenOrNull(seps);
			if (tok && same(tok, "RequiredMC"))
			{
				tok = ini->getNextTokenOrNull(seps);
			}
			if (!tok)
			{
				expected(nullptr);
			}
			if (same(tok, "ExcludedMC") || same(tok, "Animation") || same(tok, "Frames"))
			{
				break;
			}
		}
	}
	if (tok && same(tok, "ExcludedMC"))
	{
		tok = ini->getNextToken(seps);
		for (;;)
		{
			if (!setFlag(excluded, tok))
			{
				throw INIException(3, "AnimationSound line: unknown model condition '%s' in %s list", tok, "ExcludedMC");
			}
			tok = ini->getNextTokenOrNull(seps);
			if (tok && same(tok, "ExcludedMC"))
			{
				tok = ini->getNextTokenOrNull(seps);
			}
			if (!tok)
			{
				expected(nullptr);
			}
			if (same(tok, "Animation") || same(tok, "Frames"))
			{
				break;
			}
		}
	}
	if (!tok)
	{
		expected(nullptr);
	}
	const bool hasConditions = required.any() || excluded.any(); // RW 0x8CE2E5 .. 0x8CE2FC
	// RW 0x8CEE0F .. 0x8CEEF8: the Animation / Frames groups
	while (tok)
	{
		if (!same(tok, "Animation"))
		{
			expected(tok);
		}
		const std::string animation = upper(ini->getNextToken(seps));
		const char *frameTok = ini->getNextSubToken("Frames");
		for (;;)
		{
			AnimationSoundEntry e;
			e.sound = sound;
			e.animation = animation;
			e.frame = ini->scanReal(frameTok); // RW 0x42EAAD
			e.required = required;
			e.excluded = excluded;
			e.hasConditions = hasConditions;
			// a multimap insert (RW 0x8CEABE): after every entry of an equal key
			auto at = std::upper_bound(data->m_entries.begin(), data->m_entries.end(), e, [](const AnimationSoundEntry &x, const AnimationSoundEntry &y) {
				return x.animation != y.animation ? x.animation < y.animation : x.frame < y.frame;
			});
			data->m_entries.insert(at, e);
			tok = ini->getNextTokenOrNull(seps);
			if (!tok || same(tok, "Animation"))
			{
				break;
			}
			frameTok = tok;
		}
	}
}

AnimationSoundClientBehavior::AnimationSoundClientBehavior(Thing *thing, const AnimationSoundClientBehaviorModuleData *data)
	: DrawableModule(thing, data)
	, m_data(data)
{
	Drawable *d = getDrawable();
	m_manager = d ? d->drawServices().animationSounds : nullptr;
	// RW 0x8CE4CC .. 0x8CE531: the range from TheAudio at construction (0 without it)
	computeRange(m_manager && m_manager->audio ? m_manager->audio() : nullptr);
	if (m_manager)
	{
		m_manager->add(this); // RW 0x8CE542
	}
}

void AnimationSoundClientBehavior::computeRange(AudioManager *audio)
{
	// RW 0x8CE4CC .. 0x8CE531: the largest MaxRange of the entries' sounds, at most MaxUpdateRangeCap, squared; 0 without TheAudio. Retail's TheAudio
	// exists before any drawable; the port's audio manager may attach after drawables exist (review r1): the range is computed again for the manager the
	// module plays on whenever it is not the one the range was computed for
	m_rangeFor = audio;
	m_rangeSq = 0.0f;
	if (!audio)
	{
		return;
	}
	float range = 0.0f;
	for (const AnimationSoundEntry &e : m_data->m_entries)
	{
		if (const std::shared_ptr<AudioEventInfo> info = audio->ini().infos.find(e.sound))
		{
			if (info->maxRange > range)
			{
				range = info->maxRange;
			}
		}
	}
	if (range > m_data->m_maxUpdateRangeCap)
	{
		range = m_data->m_maxUpdateRangeCap;
	}
	m_rangeSq = range * range;
}

AnimationSoundClientBehavior::~AnimationSoundClientBehavior()
{
	if (m_manager)
	{
		m_manager->remove(this); // RW 0x8CE20C
	}
}

void AnimationSoundClientBehavior::reactToTransformChange(const Coord3D *, float)
{
	if (m_manager)
	{
		m_manager->toDirty(this);
	}
}

void AnimationSoundClientBehavior::becameAudible()
{
	if (m_manager)
	{
		m_manager->toDirty(this);
	}
}

void AnimationSoundClientBehavior::becameInaudible()
{
	if (m_manager)
	{
		m_manager->toClean(this);
	}
}

void AnimationSoundClientBehavior::frameWindows(const W3DDrawTrack &t, int numFrames, Window &first, Window &second)
{
	first = Window();
	second = Window();
	if (!t.anim)
	{
		return;
	}
	const float frames = (float)numFrames; // cvtsi2ss of the animation's slot 0x14
	if (!t.completed)
	{
		first = { t.prevFrame, t.frame };
		return;
	}
	switch (t.mode)
	{
		case W3D_ANIM_MODE_LOOP:
			first = { t.prevFrame, frames };
			second = { kNoFrame, t.frame };
			return;
		case W3D_ANIM_MODE_LOOP_PINGPONG:
			if (t.direction == 1)
			{
				if (t.prevFrame <= t.frame)
				{
					first = { kNoFrame, t.frame };
				}
				else
				{
					first = { t.prevFrame, 0.0f };
				}
			}
			else if (t.frame <= t.prevFrame)
			{
				first = { frames - 0.9999899864196777f, t.frame }; // RW 0xBE01AC
			}
			else
			{
				first = { t.prevFrame, frames - 1.0f };
			}
			return;
		case W3D_ANIM_MODE_LOOP_BACKWARDS:
			first = { t.prevFrame, 0.0f };
			second = { frames, t.frame };
			return;
		default:
			first = { t.prevFrame, t.frame };
			return;
	}
}

void AnimationSoundClientBehavior::update(AnimationSoundModuleManager &manager)
{
	Drawable *d = getDrawable();
	if (!d)
	{
		return;
	}
	// the drawable's audible flag (+ 0x44A) is cleared only by INAUDIBLE objects (RW 0x674727), never in the port (S-1463)
	AudioManager *audio = manager.audio ? manager.audio() : nullptr;
	if (!audio)
	{
		return;
	}
	if (audio != m_rangeFor)
	{
		computeRange(audio); // before the range test (review r1: a drawable made before the audio attached kept range 0 and was culled)
	}
	const Coord3D mic = audio->getListenerPosition(); // TheAudio vslot 0x120
	const Coord3D &p = *d->getPosition();
	const float dx = mic.x - p.x, dy = mic.y - p.y, dz = mic.z - p.z;
	const float d2 = dz * dz + dy * dy + dx * dx;
	if (!(d2 <= m_rangeSq))
	{
		// RW 0x8CEA2F .. 0x8CEA6B: the object within the range keeps the module in the dirty list
		Coord3D o;
		if (d->getObjectID() != 0 && manager.objectPosition && manager.objectPosition((std::uint32_t)d->getObjectID(), &o))
		{
			const float ox = mic.x - o.x, oy = mic.y - o.y, oz = mic.z - o.z;
			if (oz * oz + oy * oy + ox * ox <= m_rangeSq)
			{
				return;
			}
		}
		manager.toClean(this);
		++manager.m_stats.cleaned;
		return;
	}
	const std::vector<AnimationSoundEntry> &entries = m_data->m_entries;
	W3DDrawFrame frame;
	for (const DrawEntry &e : d->entries())
	{
		if (!e.draw)
		{
			continue; // not a model draw: no animation interface (slot 0xA4)
		}
		e.draw->frame(frame);
		for (int i = 0; i < frame.trackCount && i < 3; ++i)
		{
			const W3DDrawTrack &t = frame.tracks[i];
			if (!t.anim)
			{
				break; // RW 0x4C0B29: the leading tracks with an animation
			}
			Window w[2];
			frameWindows(t, t.anim->Get_Num_Frames(), w[0], w[1]);
			const std::string anim = upper(t.anim->Get_Name());
			for (const Window &win : w)
			{
				std::vector<const AnimationSoundEntry *> hits;
				entriesCrossed(entries, anim, win, d->getModelConditionFlags(), hits);
				for (const AnimationSoundEntry *hit : hits)
				{
					const AudioHandle h = audio->playSoundForDrawable(hit->sound, (std::uint32_t)d->getID()); // RW 0x6DB62D: owner type 1, the drawable
					if (h >= AHSV_FirstHandle)
					{
						++manager.m_stats.played;
					}
					else
					{
						++manager.m_stats.refused;
					}
				}
			}
		}
	}
}

void AnimationSoundClientBehavior::entriesCrossed(const std::vector<AnimationSoundEntry> &entries, const std::string &animation, const Window &win,
	const ModelConditionFlags &flags, std::vector<const AnimationSoundEntry *> &out)
{
	if (win.a == win.b)
	{
		return;
	}
	const float hi = win.a <= win.b ? win.b : win.a;
	const float lo = (win.a <= win.b && win.b != win.a) ? win.a : win.b;
	// RW 0x8CE413: the multimap's lower bound of (animation, lo), then while the animation matches and frame <= hi
	auto it = std::lower_bound(entries.begin(), entries.end(), std::make_pair(animation, lo), [](const AnimationSoundEntry &x, const std::pair<std::string, float> &k) {
		return x.animation != k.first ? x.animation < k.first : x.frame < k.second;
	});
	for (; it != entries.end() && it->animation == animation && !(hi < it->frame); ++it)
	{
		if (it->frame == win.a)
		{
			continue; // RW 0x8CE99F: the window's first end itself does not play
		}
		if (it->hasConditions && !(flags.testForAll(it->required) && flags.testForNone(it->excluded))) // RW 0x5DF56A on the drawable's + 0x258
		{
			continue;
		}
		out.push_back(&*it);
	}
}

void AnimationSoundClientBehavior::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<AnimationSoundClientBehaviorModuleData>("AnimationSoundClientBehavior", MODULETYPE_CLIENT_BEHAVIOR);
	modules.bindModuleProc("AnimationSoundClientBehavior", MODULETYPE_CLIENT_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const AnimationSoundClientBehaviorModuleData *typed = dynamic_cast<const AnimationSoundClientBehaviorModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("AnimationSoundClientBehavior: the module data is not typed");
			}
			return std::make_unique<AnimationSoundClientBehavior>(thing, typed);
		});
}

// ---- TheAnimationSoundModuleManager --------------------------------------------------------------------------------------------------------------

void AnimationSoundModuleManager::add(AnimationSoundClientBehavior *m)
{
	// RW 0x83F279: at the head of the dirty list
	if (m_dirtyHead)
	{
		m_dirtyHead->m_prev = m;
	}
	m->m_prev = nullptr;
	m->m_next = m_dirtyHead;
	m_dirtyHead = m;
	if (!m_dirtyTail)
	{
		m_dirtyTail = m;
	}
	++m_count;
}

void AnimationSoundModuleManager::remove(AnimationSoundClientBehavior *m)
{
	// RW 0x83F21F: out of whichever list holds it
	if (m_dirtyHead == m)
	{
		m_dirtyHead = m->m_next;
	}
	if (m_dirtyTail == m)
	{
		m_dirtyTail = m->m_prev;
	}
	if (m_cleanHead == m)
	{
		m_cleanHead = m->m_next;
	}
	if (m_cleanTail == m)
	{
		m_cleanTail = m->m_prev;
	}
	if (m->m_next)
	{
		m->m_next->m_prev = m->m_prev;
	}
	if (m->m_prev)
	{
		m->m_prev->m_next = m->m_next;
	}
	m->m_next = m->m_prev = nullptr;
	--m_count;
}

void AnimationSoundModuleManager::toDirty(AnimationSoundClientBehavior *m)
{
	// RW 0x83F159: unless it is the dirty list's head or tail, unlink it (from the clean list) and put it at the dirty list's head
	if (m_dirtyHead == m || m_dirtyTail == m)
	{
		return;
	}
	if (m_cleanHead == m)
	{
		m_cleanHead = m->m_next;
	}
	if (m_cleanTail == m)
	{
		m_cleanTail = m->m_prev;
	}
	if (m->m_next)
	{
		m->m_next->m_prev = m->m_prev;
	}
	if (m->m_prev)
	{
		m->m_prev->m_next = m->m_next;
	}
	if (m_dirtyHead)
	{
		m_dirtyHead->m_prev = m;
	}
	m->m_next = m_dirtyHead;
	m_dirtyHead = m;
	m->m_prev = nullptr;
	if (!m_dirtyTail)
	{
		m_dirtyTail = m;
	}
}

void AnimationSoundModuleManager::toClean(AnimationSoundClientBehavior *m)
{
	// RW 0x83F1BC: unless it is the clean list's head or tail, unlink it (from the dirty list) and put it at the clean list's head
	if (m_cleanHead == m || m_cleanTail == m)
	{
		return;
	}
	if (m_dirtyHead == m)
	{
		m_dirtyHead = m->m_next;
	}
	if (m_dirtyTail == m)
	{
		m_dirtyTail = m->m_prev;
	}
	if (m->m_next)
	{
		m->m_next->m_prev = m->m_prev;
	}
	if (m->m_prev)
	{
		m->m_prev->m_next = m->m_next;
	}
	if (m_cleanHead)
	{
		m_cleanHead->m_prev = m;
	}
	m->m_next = m_cleanHead;
	m_cleanHead = m;
	m->m_prev = nullptr;
	if (!m_cleanTail)
	{
		m_cleanTail = m;
	}
}

void AnimationSoundModuleManager::update()
{
	AudioManager *a = audio ? audio() : nullptr;
	if (!a)
	{
		return; // RW calls TheAudio unconditionally; without an audio manager there is no microphone and nothing plays
	}
	++m_stats.updates;
	const Coord3D mic = a->getListenerPosition(); // TheAudio vslot 0x120
	// RW 0x83F345 .. 0x83F382: squared distance from the stored position against MinMicrophoneDistanceToDirty squared (strictly farther)
	const float dx = mic.x - m_storedMicrophone.x, dy = mic.y - m_storedMicrophone.y, dz = mic.z - m_storedMicrophone.z;
	const float d2 = dz * dz + dy * dy + dx * dx;
	const float minDist = a->ini().minMicrophoneDistanceToDirty; // RW 0xDADE88
	if (d2 > minDist * minDist && m_cleanTail)
	{
		// RW 0x83F384 .. 0x83F3B4: the clean list goes in front of the dirty list
		if (m_dirtyHead)
		{
			m_cleanTail->m_next = m_dirtyHead;
			m_dirtyHead->m_prev = m_cleanTail;
		}
		m_dirtyHead = m_cleanHead;
		if (!m_dirtyTail)
		{
			m_dirtyTail = m_cleanTail;
		}
		m_cleanHead = m_cleanTail = nullptr;
		++m_stats.redirtied;
	}
	if (!m_cleanHead)
	{
		m_storedMicrophone = mic; // RW 0x83F3B8 .. 0x83F3D0
	}
	for (AnimationSoundClientBehavior *m = m_dirtyHead; m;)
	{
		AnimationSoundClientBehavior *next = m->m_next; // RW 0x83F3D8: read before the update
		++m_stats.moduleUpdates;
		m->update(*this);
		m = next;
	}
}

size_t AnimationSoundModuleManager::dirtyCount() const
{
	size_t n = 0;
	for (const AnimationSoundClientBehavior *m = m_dirtyHead; m; m = m->m_next)
	{
		++n;
	}
	return n;
}

size_t AnimationSoundModuleManager::cleanCount() const
{
	size_t n = 0;
	for (const AnimationSoundClientBehavior *m = m_cleanHead; m; m = m->m_next)
	{
		++n;
	}
	return n;
}

bool AnimationSoundModuleManager::isDirty(const AnimationSoundClientBehavior *m) const
{
	for (const AnimationSoundClientBehavior *x = m_dirtyHead; x; x = x->m_next)
	{
		if (x == m)
		{
			return true;
		}
	}
	return false;
}

void ClientBehaviorModules::registerAll(ModuleFactory &modules)
{
	AnimationSoundClientBehavior::registerClass(modules);
}
