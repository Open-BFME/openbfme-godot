// OpenBFME. GPL-3.0.
// See PalantirCommandUI.h.

#include "GameClient/PalantirCommandUI.h"

#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
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

namespace
{
// RW 0x9D2437
bool rankAndProgress(const Object &obj, int &rank, float &progress)
{
	const ExperienceTracker *xp = obj.getExperienceTracker();
	if (!xp || !TheExperienceLevelSystem)
	{
		return false;
	}
	const ExperienceLevelTemplate *cur = TheExperienceLevelSystem->findLevel(xp->getLevelName());
	if (!cur)
	{
		return false;
	}
	rank = cur->m_rank;
	progress = -1.0f;
	const ExperienceLevelTemplate *next = xp->upcomingLevel();
	if (next)
	{
		// RW 0x6893E2: the experience is below the required experience of the last level of the chain
		const ExperienceLevelTemplate *last = next;
		for (int guard = 0; guard < 256; ++guard)
		{
			const ExperienceLevelTemplate *n = TheExperienceLevelSystem->nextLevel(xp->levelTargetName(), last->m_name, xp->levelsForMultiplayer());
			if (!n)
			{
				break;
			}
			last = n;
		}
		if ((float)last->m_requiredExperience > xp->getExperience())
		{
			const float curReq = (float)cur->m_requiredExperience, nextReq = (float)next->m_requiredExperience;
			if (nextReq > curReq)
			{
				const float p = (xp->getExperience() - curReq) / (nextReq - curReq);
				progress = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
			}
		}
	}
	return !(rank <= 1 && progress < 0.0f);
}
} // namespace

bool objectRankAndProgress(const Object &obj, int &rank, float &progress)
{
	return rankAndProgress(obj, rank, progress);
}

RankInfo rankInfo(const HudContext &ctx, const Object &obj)
{
	RankInfo r;
	r.type = 0;
	Object &o = const_cast<Object &>(obj);
	const LifetimeUpdate *life = dynamic_cast<const LifetimeUpdate *>(o.findModule("LifetimeUpdate"));
	// RW 0x92F7D9: module + 0x28 is set from WaitForWakeUp (ctor RW 0x7A7F2D) and cleared by the wake-up (RW 0x7A7E79): a module still waiting gives no time bar.
	// INFERENCE (S-1955): the port keeps no such byte; a module waiting for its wake-up has never computed its frames (both 0)
	const bool waiting = life && life->waitsForWakeUp() && life->dieFrame() == 0 && life->startFrame() == 0;
	if (life && !waiting)
	{
		r.type = 1;
		const unsigned death = life->dieFrame(), start = life->startFrame(), now = ctx.logic.getFrame();
		if (death <= start)
		{
			r.type = 2;
			return r;
		}
		// RW 0x92F8D0: unsigned differences as floats (fild with the 2^32 correction)
		const float p = (float)(unsigned)(death - now) / (float)(unsigned)(death - start);
		r.progress = (now > death || p < 0.0f) ? 0.0f : (p > 1.0f ? 1.0f : p);
		return r;
	}
	const ExperienceTracker *xp = obj.getExperienceTracker();
	if (!xp || !xp->isTrainable() || !rankAndProgress(obj, r.rank, r.progress))
	{
		r.type = 2;
	}
	return r;
}

RankInfo rankInfoFor(const HudContext &ctx, ObjectID contextObject, const std::vector<ObjectID> &selected)
{
	if (contextObject != INVALID_ID)
	{
		const Object *obj = ctx.logic.findObjectByID(contextObject);
		return obj ? rankInfo(ctx, *obj) : RankInfo{};
	}
	RankInfo info;
	bool first = true;
	for (ObjectID id : selected)
	{
		const Object *obj = ctx.logic.findObjectByID(id);
		if (!obj)
		{
			continue;
		}
		const RankInfo r = rankInfo(ctx, *obj);
		if (first)
		{
			info = r;
			first = false;
			if (info.type == 2)
			{
				break;
			}
			continue;
		}
		if (!(r == info))
		{
			info.type = 2;
			break;
		}
	}
	return info;
}

std::vector<std::string> acceptanceStops()
{
	return {
		"[S-760] Palantir portrait: a drawable's portrait is its template's SelectPortrait; the draw module's template override (RW 0x694BF8) and the local-player branch of the "
		"kind-of bit +0x11F & 0x40 (RW 0x68FBD3) are not ported, nor the cost modifier interface of CommandUI (ShowCostModifierUpgradeInterface)",
		"[S-1955] Palantir rank interface (HUD-5): the rank / experience bar (RW 0x9305CE, 0x92F778, 0x9D2437) and the LifetimeUpdate time bar are ported; not ported: the "
		"time bars of TemporarilyDefectUpdate and ToggleHiddenSpecialAbilityUpdate, a disguised enemy's shown template and rank 1 (RW 0x69194E); LifetimeUpdate's + 0x28 "
		"(still waiting for its wake-up) is taken as 'WaitForWakeUp and no frames computed yet'",
	};
}
} // namespace PalantirCommandUI
