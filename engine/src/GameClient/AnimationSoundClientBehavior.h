// OpenBFME. GPL-3.0.
//
// AnimationSoundClientBehavior (lane AUDIO-4, stop S-1242: footsteps, body falls, weapon foley). A ClientBehavior module (RotWK ModuleFactory name
// "AnimationSoundClientBehavior", string RW 0xC0A618, registered at RW 0x65BAE5 with the create procs RW 0x65292B / data RW 0x652963, type 3, interface
// mask 0x1000; module vtable RW 0xC75AB0, its second interface RW 0xC75AA4) that plays a sound when one of its drawable's animations passes a frame, and
// TheAnimationSoundModuleManager (RW 0xDE8D68, made by RW 0x646909, vtable RW 0xC53E34) that runs the modules every client frame.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * data (size 0x18, ctor RW 0x8CEB40, field table RW 0xC75C94): AnimationSound (RW 0x8CEBCB, any number of lines) into a multimap at + 8 ordered by
//     (animation, frame); MaxUpdateRangeCap (RW 0x42ED1C parseReal, + 0x14, default FLT_MAX RW 0xBD1910).
//   * an AnimationSound line (RW 0x8CEBCB): `Sound:<event>` (RW 0x42DD73; empty: "AnimationSound line: sound name cannot empty"; an event TheAudio
//     does not know: "AnimationSound line: unknown sound '%s'"), then optionally `RequiredMC: <flags>` and / or `ExcludedMC: <flags>` (each name set in
//     a local mask by RW 0x4B5DA2; an unknown one: "AnimationSound line: unknown model condition '%s' in %s list"; a repeated label is skipped), then one
//     or more `Animation: <name> Frames: <real> [<real> ...]` groups (the name upper-cased and made a name key, RW 0x49F474; every frame is an entry
//     {sound, animation, frame, required, excluded, has-conditions = either mask not empty (RW 0x4B3783)}); a missing group: "AnimationSound line:
//     expected 'Animation' next, got '%s'" ("<End of line>"). Every error is INIException 3.
//   * module (size 0x1C, ctor RW 0x8CE48D): + 0x10 the update range squared: the largest MaxRange (info + 0x98) of its entries' sounds, at most
//     MaxUpdateRangeCap, squared (0 without TheAudio); + 0x14 / + 0x18 the manager's list links. The ctor puts it at the head of the manager's
//     dirty list (RW 0x83F279); the destructor takes it out of its list (RW 0x83F21F). Slot 11 (RW 0x8CE235: three arguments, read here as
//     ZH's reactToTransformChange, INFERENCE) and the second interface's slot 0 (RW 0x8CE249) move it to the dirty list (RW 0x83F159); the second
//     interface's slot 1 (RW 0x8CE25E) to the clean list (RW 0x83F1BC). The second interface's slots are the drawable's audible flag changes
//     (+ 0x44A: RW 0x674727 / 0x67896F, called when the object becomes INAUDIBLE / audible: S-1463, never here).
//   * the manager's client update (vtable slot 10, RW 0x83F321): when the microphone (TheAudio vslot 0x120) is farther than
//     MinMicrophoneDistanceToDirty (AnimationSoundClientBehaviorGlobalSetting, RW 0xDADE88) from the position the manager stored (+ 0x20, FLT_MAX at
//     first) and the clean list is not empty, the clean list is put in front of the dirty list; when the clean list is empty the microphone position is
//     stored. Then every module of the dirty list is updated, head first (the next link is read before the update).
//   * a module's update (RW 0x8CE75B): the drawable not audible (+ 0x44A): to the clean list. Else, with TheAudio: when the squared distance from the
//     microphone to the drawable's position is at most the range: for every draw module of the drawable (RW 0x60E02D), its animation interface (slot 0xA4:
//     a model draw, + 0xC) gives the number of leading tracks with an animation (slot 0x24, RW 0x4C0B15), each track's animation (slot 0x2C) and the two
//     frame windows its last step crossed (slot 0x3C, RW 0x4C0CBC, see frameWindows); for each window (a, b) with a != b, the entries of that animation
//     with lo <= frame <= hi (lo / hi the smaller / larger end; the multimap's lower bound, RW 0x8CE413) and frame != a, whose conditions the drawable's
//     model condition flags satisfy (+ 0x258: RW 0x5DF56A, required all set and excluded none) when it has conditions, play their sound attached to
//     the drawable (RW 0x6DB62D, owner type 1, TheAudio vslot 0x64). Beyond the range: when the drawable's object is within the range it stays, else
//     to the clean list. The model draw advances its animation first when it has not been advanced in this client frame (RW 0x4BFAF4).
// The port runs the manager right after the drawables' animations were advanced in the render frame (DrawableManager::advance), every frame.

#pragma once

#include "Common/INI.h"
#include "Common/Module.h"
#include "Common/ModelState.h"
#include "Common/INIDataTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class AudioManager;
class Drawable;
class ModuleFactory;
struct W3DDrawTrack;

struct AnimationSoundEntry
{
	std::string sound;         ///< the audio event (+ 4)
	std::string animation;     ///< upper-cased "HIERARCHY.ANIM" (the name key RW 0x49F474 makes)
	float frame = 0.0f;        ///< + 8
	ModelConditionFlags required, excluded; ///< + 0xC / + 0x58
	bool hasConditions = false; ///< + 0xA4
};

class AnimationSoundClientBehaviorModuleData : public ModuleData
{
public:
	std::vector<AnimationSoundEntry> m_entries; ///< + 8: ordered by (animation, frame), equal keys in line order (a multimap's insertion)
	float m_maxUpdateRangeCap;                  ///< + 0x14
	AnimationSoundClientBehaviorModuleData();
	static void buildFieldParse(MultiIniFieldParse &p);
	static void parseAnimationSound(INI *ini, void *instance, void *store, const void *userData); // RW 0x8CEBCB
	// a test's sound name check of the parse (TheAudio's event table, RW vslot 300); unset: ContainParseHooks::audioEventExists (the world's table);
	// neither: INIException 8 (S-083's rule)
	static std::function<bool(const std::string &)> &soundExists();
};

class AnimationSoundModuleManager;

class AnimationSoundClientBehavior : public DrawableModule
{
public:
	AnimationSoundClientBehavior(Thing *thing, const AnimationSoundClientBehaviorModuleData *data);
	~AnimationSoundClientBehavior() override;

	void reactToTransformChange(const Coord3D *oldPos, float oldAngle) override; // slot 11 (RW 0x8CE235), INFERENCE
	void becameAudible();   // the second interface's slot 0 (RW 0x8CE249)
	void becameInaudible(); // slot 1 (RW 0x8CE25E)

	const AnimationSoundClientBehaviorModuleData *data() const { return m_data; }
	float rangeSquared() const { return m_rangeSq; }

	// RW 0x4C0CBC: the frame windows a track's last step crossed (`numFrames`: the animation's frame count)
	struct Window
	{
		float a = 0.0f, b = 0.0f;
	};
	static void frameWindows(const W3DDrawTrack &track, int numFrames, Window &first, Window &second);
	// RW 0x8CE8F7 .. 0x8CEA2D: the entries of `animation` a window crosses (lo <= frame <= hi, frame != a) whose conditions `flags` satisfy, in order
	static void entriesCrossed(const std::vector<AnimationSoundEntry> &entries, const std::string &animation, const Window &win, const ModelConditionFlags &flags,
		std::vector<const AnimationSoundEntry *> &out);

	static void registerClass(ModuleFactory &modules);

private:
	friend class AnimationSoundModuleManager;
	void update(AnimationSoundModuleManager &manager); // RW 0x8CE75B
	void computeRange(AudioManager *audio);     // RW 0x8CE4CC .. 0x8CE531

	const AnimationSoundClientBehaviorModuleData *m_data;
	AnimationSoundModuleManager *m_manager = nullptr;
	float m_rangeSq = 0.0f;                         // + 0x10
	const AudioManager *m_rangeFor = nullptr;       ///< the audio manager m_rangeSq was computed for (port)
	AnimationSoundClientBehavior *m_next = nullptr;  // + 0x14
	AnimationSoundClientBehavior *m_prev = nullptr;  // + 0x18
};

// TheAnimationSoundModuleManager, one per game's drawables (DrawServices::animationSounds)
class AnimationSoundModuleManager
{
public:
	// the audio the modules play on and ask for the microphone (null: nothing plays); the drawable's object position (the latest published snapshot)
	std::function<AudioManager *()> audio;
	std::function<bool(std::uint32_t objectId, Coord3D *pos)> objectPosition;

	void add(AnimationSoundClientBehavior *m);     // RW 0x83F279
	void remove(AnimationSoundClientBehavior *m);  // RW 0x83F21F
	void toDirty(AnimationSoundClientBehavior *m); // RW 0x83F159
	void toClean(AnimationSoundClientBehavior *m); // RW 0x83F1BC
	// RW 0x83F321, once per client frame after the animations advanced
	void update();

	struct Stats
	{
		std::uint64_t updates = 0, moduleUpdates = 0, played = 0, refused = 0, cleaned = 0, redirtied = 0;
	};
	const Stats &stats() const { return m_stats; }
	// lane PERF-3 (tests): the modules of the dirty / clean list from its head, in list order
	std::vector<const AnimationSoundClientBehavior *> listOrder(bool dirty) const
	{
		std::vector<const AnimationSoundClientBehavior *> out;
		for (const AnimationSoundClientBehavior *m = dirty ? m_dirtyHead : m_cleanHead; m; m = m->m_next)
		{
			out.push_back(m);
		}
		return out;
	}
	size_t moduleCount() const { return m_count; }
	size_t dirtyCount() const;
	size_t cleanCount() const;
	bool isDirty(const AnimationSoundClientBehavior *m) const;

private:
	friend class AnimationSoundClientBehavior;
	AnimationSoundClientBehavior *m_dirtyHead = nullptr, *m_dirtyTail = nullptr; // + 0xC / + 0x10
	AnimationSoundClientBehavior *m_cleanHead = nullptr, *m_cleanTail = nullptr; // + 0x14 / + 0x18
	size_t m_count = 0;                                                          // + 0x1C
	Coord3D m_storedMicrophone{ 3.4028234663852886e+38f, 3.4028234663852886e+38f, 3.4028234663852886e+38f }; // + 0x20 (RW 0xBD1910)
	Stats m_stats;
};
