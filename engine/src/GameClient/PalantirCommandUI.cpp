// OpenBFME. GPL-3.0.
// See PalantirCommandUI.h.

#include "GameClient/PalantirCommandUI.h"

#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Object/Object.h"

#include <variant>

namespace PalantirCommandUI
{
std::string objectPortrait(const Object &obj)
{
	// RW 0x694F06 -> 0x73D0A3 -> 0x73CF31: the template's SelectPortrait (template +0x74)
	const ThingTemplate *t = obj.getTemplate();
	if (!t)
	{
		return std::string();
	}
	if (const FieldValue *v = t->findField("SelectPortrait"))
	{
		if (const std::string *name = std::get_if<std::string>(v))
		{
			return *name;
		}
	}
	return std::string();
}

std::string portraitFor(const HudContext &ctx, ObjectID contextObject, const std::vector<ObjectID> &selected)
{
	if (contextObject != (ObjectID)INVALID_ID)
	{
		// RW 0x92FE44: a context drawable decides alone
		const Object *obj = ctx.logic.findObjectByID(contextObject);
		return obj ? objectPortrait(*obj) : std::string();
	}
	// RW 0x92FE6D .. 0x92FECF: the first selected drawable's portrait when every selected drawable shares it
	const Object *first = nullptr;
	std::string shared;
	bool same = true;
	for (ObjectID id : selected)
	{
		const Object *obj = ctx.logic.findObjectByID(id);
		if (!obj)
		{
			continue;
		}
		const std::string p = objectPortrait(*obj);
		if (!first)
		{
			first = obj;
			shared = p;
			if (shared.empty())
			{
				same = false; // 0x92FEA3: the first one has no image
				break;
			}
			continue;
		}
		if (p != shared)
		{
			same = false;
			break;
		}
	}
	if (first && same)
	{
		return shared;
	}
	if (!first)
	{
		return std::string(); // 0x92FF0D: nothing selected
	}
	// RW 0x92FF0F .. 0x92FF47: the first selected object's team's controlling player's MultiSelectionPortrait, else the image "MultiPortrait"
	if (const Team *team = first->getTeam())
	{
		if (const Player *owner = team->getControllingPlayer())
		{
			if (const PlayerTemplate *pt = owner->getPlayerTemplate())
			{
				if (!pt->m_multiSelectionPortrait.empty())
				{
					return pt->m_multiSelectionPortrait;
				}
			}
		}
	}
	return "MultiPortrait";
}

std::vector<std::string> acceptanceStops()
{
	return {
		"[S-760] Palantir portrait: a drawable's portrait is its template's SelectPortrait; the draw module's template override (RW 0x694BF8) and the local-player branch of the "
		"kind-of bit +0x11F & 0x40 (RW 0x68FBD3) are not ported, nor the rank and cost modifier interfaces of CommandUI (ShowRankInterface, SetRankProgressBar, "
		"ShowCostModifierUpgradeInterface)",
	};
}
} // namespace PalantirCommandUI
