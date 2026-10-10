// OpenBFME. GPL-3.0. See GameLogic/FXEvents.h.

#include "GameLogic/FXEvents.h"

#include "GameLogic/Object/Object.h"

#include <cctype>

bool FXEventLog::isFXName(const std::string &name)
{
	if (name.empty())
	{
		return false;
	}
	// RW 0x73A302 parseFXList: "None" (stricmp) stores NULL
	static const char kNone[] = "none";
	if (name.size() == 4)
	{
		bool none = true;
		for (size_t i = 0; i < 4; ++i)
		{
			none = none && (char)std::tolower((unsigned char)name[i]) == kNone[i];
		}
		if (none)
		{
			return false;
		}
	}
	return true;
}

FXEvent FXEventLog::objectEvent(FXEvent::Kind kind, const char *site, UnsignedInt frame, const std::string &fxList, const Object &obj)
{
	FXEvent e;
	e.kind = kind;
	e.site = site;
	e.frame = frame;
	e.fxList = &fxList;
	e.primary = obj.getID();
	e.position = *obj.getPosition();
	const float *b = obj.getBasis();
	for (int r = 0; r < 3; ++r)
	{
		e.transform[r * 4 + 0] = b[r * 3 + 0];
		e.transform[r * 4 + 1] = b[r * 3 + 1];
		e.transform[r * 4 + 2] = b[r * 3 + 2];
	}
	e.transform[3] = e.position.x;
	e.transform[7] = e.position.y;
	e.transform[11] = e.position.z;
	e.hasTransform = true;
	e.conditions = obj.getModelConditionBits();
	return e;
}

void FXEventLog::emit(const FXEvent &event)
{
	if (event.choices ? event.choices->empty() : (!event.fxList || !isFXName(*event.fxList)))
	{
		return;
	}
	m_frame.push_back(event);
	++m_total;
	++m_perSite[event.site];
	if (m_sink)
	{
		m_sink->onFXEvent(event);
	}
}

std::vector<std::string> FXEventLog::stops()
{
	return {
		"[S-680] fire FX: the aim query of the fire FX block (RW 0x6CB85A flag 1: the PreferredTargetBone answer, else the logic draw of Weapon.cpp:1749 as "
		"the seed of RW 0x690BD2) runs for every shot at a victim; the contact point is S-362's approximation (the geometry centre, the seed unused); FireFlankFX "
		"is never chosen (the flank test RW 0x68FB63 is not ported, S-322); a HitStoredTarget warhead runs RotWK's temporary weapon at its victim, fire FX block and aim draw included (lane DECOMP-1: RW 0x85F1F7 -> RW 0x6CF590); "
		"a detonation through fireProjectileDetonation (RW 0x6CB7BD) delivers its nuggets without a fire FX block, as RotWK's does",
		"[S-681] damage FX: ActiveBody::doDamageFX (RW 0x8C2F02) plays the armour set's DamageFX; the per damage type ObjectCreationLists of the loop after it "
		"(body + 0xE0, RW 0x5F0126) are not created (no OCL store); the throttle fields (+0x38 / +0x3C) are not in the state hash",
		"[S-683] effects not played: the FXEvent entries of MODEL CONDITION states and the FXEvents routed to the timed particle list (FXList byte +8, "
		"the second loop of RW 0x4BCE68; the animation state entries play since FX-2), the research sound of a finished upgrade (S-486), "
		"LevelUpFX (no experience logic merged), the OCLs of death / collapse phases, the FXList LOD / cull settings of the options",
		"[S-685] slow death phases: RW 0x8609A8 is ported for the draws (the FX and Sound picks are client draws made by the player, the OCL and Weapon picks "
		"logic draws, all behind the resolved-entry mask +0x18C); the mask's OCL and Weapon bits are taken from the names (inference), the picked OCL is created "
		"from the object since lane SPELL-2 (RW 0x860A46, OCL::create RW 0x5F0126 through the CreateObject port, S-530), the Weapon entry is not executed "
		"(S-324) and the sound plays at the object's position, not on the object (no audio object query installed)",
	};
}
