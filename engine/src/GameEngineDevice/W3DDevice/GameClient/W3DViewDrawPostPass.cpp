// OpenBFME. GPL-3.0. Which world post pass runs (lane GLOW-1): see W3DViewDrawPostPass.h.

#include "GameEngineDevice/W3DDevice/GameClient/W3DViewDrawPostPass.h"

namespace W3DViewDrawPostPass
{

Choice choose(const Inputs &in)
{
	if (in.passMode != 0) // RW 0x4878C4
	{
		return Choice::None;
	}
	if (specialFilter(in.viewFilter)) // RW 0x4878CF; the filter 8 needs GlowEnabled (RW 0x4878E9), 6 and 7 always run
	{
		return in.viewFilter == 8 && !in.glowEnabled ? Choice::None : Choice::SpecialFilter;
	}
	const bool low = lowEffectsLOD(in.effectsLOD);
	if (in.colorGradeActive && !low) // RW 0x4879FB .. 0x487A16
	{
		return Choice::ColorGrade;
	}
	if (in.glowEnabled && !low) // RW 0x487A1B .. 0x487A36
	{
		return Choice::Glow;
	}
	return Choice::None;
}

} // namespace W3DViewDrawPostPass
