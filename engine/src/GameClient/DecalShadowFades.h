// OpenBFME. GPL-3.0.
#pragma once

// Lane DRAWDECAL-1: the drawables' alpha / additive decal shadows over the frames (BlobShadow::DecalFade): which drawable shows which decal since
// when, and the _DYNAMIC decals that went and fade out where they were last drawn (RW 0x508B40). One frame: begin(), then for every drawable hide()
// for what its look or a script hides and keep() / drawn() for the decal it has, then end().

#include "GameClient/BlobShadow.h"
#include "GameClient/SelectionDecals.h"

#include <cstdint>
#include <map>
#include <vector>

namespace BlobShadow
{
class DecalFades
{
public:
	void clear();
	// The frame `now` (client frames). `useShadowDecals`: GlobalData + 0x61; a decal the current ShadowLOD does not draw (drawnAtLOD, RW 0x50BF7A) goes
	// at once, a fading one too: turning the shadows off leaves no glow behind
	void begin(unsigned now, bool useShadowDecals);
	// The drawable's decal is not seen this frame (its look hides it), or the draw `draw` it comes from is hidden: if it goes now, nothing fades out
	void hide(std::uint32_t drawable);
	void hide(std::uint32_t drawable, const void *draw);
	// The drawable has `spec` from its draw `draw` this frame, seen or not: its fade starts with the first such frame (another object or another decal
	// starts its own; the one before fades out). False when the fade has nothing to draw; else `opacity` is the decal's
	bool keep(std::uint32_t drawable, ObjectID object, const void *draw, const DecalSpec &spec, unsigned &opacity);
	// keep()'s decal was drawn as `decal` (a fade-out stands there)
	void drawn(std::uint32_t drawable, const SelectionDecal &decal);
	// The decals keep() did not name this frame go; the fading ones are added to `out`
	void end(std::vector<SelectionDecal> &out);

private:
	struct View
	{
		ObjectID object = INVALID_ID; ///< a drawable id the next object reuses starts its own fade
		const void *draw = nullptr;
		DecalSpec spec;
		DecalFade fade;
		bool hasLast = false; ///< `last` was drawn when the drawable last had the decal
		SelectionDecal last;
		bool kept = false; ///< named this frame
	};
	struct Going
	{
		SelectionDecal decal;
		DecalFade fade;
		DecalSpec spec;
	};
	void release(const View &v);

	std::map<std::uint32_t, View> m_views;
	std::vector<Going> m_going;
	unsigned m_now = 0;
};
} // namespace BlobShadow
