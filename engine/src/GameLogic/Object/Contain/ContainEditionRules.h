// OpenBFME. GPL-3.0.
// Container-system edition policy. BFME1 1.03 behavior is isolated here;
// the established BFME2/RotWK runtime keeps its current rules.
#pragma once

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

struct ContainEditionRules
{
	bool bfme1;
	static const ContainEditionRules &forObject(const Object &object)
	{
		static const ContainEditionRules original{true}, shared{false};
		return object.getTemplate()->bfme1Data() ? original : shared;
	}
	// BFME1 0x0024CD28 / 0x0024CD5E. RNG provenance is part of
	// deterministic recordings, although these use the same integer bounds.
	const char *fallingSource() const
	{
		return bfme1 ? "F:\\bfme\\Code\\gameengine\\Source\\GameLogic\\Object\\Contain\\HordeContain\\HordeTransportContain.cpp" : "HordeTransportContain.cpp";
	}
	int fallingSite(bool first) const { return bfme1 ? (first ? 0x350 : 0x35D) : (first ? 0x35A : 0x367); }
	float healingPerFrame(float maximum, float percent) const
	{
		if (bfme1)
		{
			// BFME1 0x0022D6E8..0x0022D70B: x87 PC24, max *
			// 0.2f * percent * 0.01f, one final dword store.
			return SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24MulW(SimMath::pc24MulW((double)maximum, (double)0.2f), (double)percent), (double)0.01f));
		}
		// Established shared runtime (RW 0x86B897..0x86B91F).
		return (float)SimMath::mulD(SimMath::mulD(SimMath::mulD((double)maximum, (double)percent), (double)0.2f), (double)0.01f);
	}
};
