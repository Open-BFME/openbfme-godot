// OpenBFME - lane VISUAL-2: the drawable's shroud look, the client's per-frame reading of its object's shroud status.
//
// TARGET FACTS (RotWK 2.01; both functions map to byte-matched BFME2 1.06 decomp C++, tools/re/rw2decomp.py):
// - GameClient::update, RW 0x64849E (BFME2 0x63BEE8, decomp GameClientDrawableTOC.cpp:1837): for every drawable with an object,
//   ss = object->getShroudStatusForPlayer(local); when ss >= OBJECTSHROUD_FOGGED and the drawable's shroud clear frame (+0x138) is not 0, ss counts as
//   CLEAR while the logic frame is below clearFrame + 2 * LOGICFRAMES_PER_SECOND (+ 3 * LOGICFRAMES_PER_SECOND more for an effectively dead object);
//   then Drawable::setFullyObscuredByShroud(ss >= OBJECTSHROUD_FOGGED) (RW 0x678F54, BFME2 0x678C29: byte +0x440, handed to every draw module).
// - RTS3DScene::renderOneObject (BFME2 0x6FB59, decomp RTS3DSceneRenderOneObject.cpp:476): a drawn object whose status is CLEAR stores the logic frame
//   as the shroud clear frame (+0x138); within the same grace a fogged / shrouded object is drawn as PARTIAL_CLEAR.
// - ParticleSystem::update, RW 0x5FA2CE: a system attached to a drawable does not emit while +0x440 is set (ParticleSys.cpp, isShrouded).
// INFERENCE (stop S-4970): renderOneObject runs only for render objects in the view; the clear frame is stored here for every presented CLEAR status,
// in or out of the view. The device layer stands the live drawable in for retail's ghost object while it is fogged (S-561), so `hidden` is the
// grace-adjusted SHROUDED status (VIS-1's rule) and `fullyObscured` the grace-adjusted >= FOGGED one (the particle gate).
#pragma once

#include "Common/GameCommon.h"
#include "GameLogic/System/ShroudManager.h"

#include <vector>

struct DrawableShroudLook
{
	// the viewer of a snapshot's shroud view: the local player (+ 2: -2 omniscient, -1 none) and the vision players as a bit mask
	static unsigned long long viewerKeyOf(int localPlayer, const std::vector<int> &visionPlayers)
	{
		unsigned long long key = (unsigned long long)(unsigned)(localPlayer + 2) << 32;
		for (int p : visionPlayers)
		{
			if (p >= 0 && p < 32)
			{
				key |= 1ull << p;
			}
		}
		return key;
	}

	UnsignedInt clearFrame = 0;  ///< Drawable +0x138
	bool fullyObscured = false;  ///< Drawable +0x440
	bool hidden = false;         ///< the device layer does not draw it (grace-adjusted OBJECTSHROUD_SHROUDED)
	unsigned long long viewer = 0; ///< whose shroud the statuses are (ShroudView: the local player and the players whose vision it shares)

	// `objectShroud`: the presented ObjectShroudStatus for the local player (0 OBJECTSHROUD_INVALID: no shroud displayed; no record and ALWAYS_VISIBLE present CLEAR, RW 0x68D8F7)
	// `viewerKey`: whose shroud it is (viewerKeyOf); another viewer (a caster's / observer's POV switch) starts without a clear frame (INFERENCE, S-4970)
	void update(int objectShroud, bool effectivelyDead, UnsignedInt logicFrame, unsigned long long viewerKey = 0)
	{
		if (viewerKey != viewer)
		{
			viewer = viewerKey;
			clearFrame = 0;
		}
		int ss = objectShroud;
		if (ss == (int)OBJECTSHROUD_CLEAR)
		{
			clearFrame = logicFrame;
		}
		else if (ss >= (int)OBJECTSHROUD_FOGGED && clearFrame != 0)
		{
			UnsignedInt limit = 2 * LOGICFRAMES_PER_SECOND;
			if (effectivelyDead)
			{
				limit += 3 * LOGICFRAMES_PER_SECOND;
			}
			if (logicFrame < limit + clearFrame)
			{
				ss = (int)OBJECTSHROUD_CLEAR;
			}
		}
		fullyObscured = ss >= (int)OBJECTSHROUD_FOGGED;
		hidden = ss == (int)OBJECTSHROUD_SHROUDED;
	}
};
