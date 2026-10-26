// OpenBFME. GPL-3.0.
// See PalantirCommandUI.h.

#include "GameClient/PalantirCommandUI.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Module/StealthAbilityModules.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"

#include <variant>

namespace PalantirCommandUI
{
std::string objectPortrait(const Object &obj)
{
	// lane MODS-3: an ObjectImageUpgrade's SelectPortrait first (edain-unchained.object-image-upgrade, its hook 0xEF3331 at RW 0x694F06)
	std::string upgraded = ObjectImageUpgrade::imageOf(obj, true);
	if (!upgraded.empty())
	{
		return upgraded;
	}
	// RW 0x694F06 -> 0x73D0A3 -> 0x73CF31: the template's SelectPortrait (template +0x74)
	const ThingTemplate *t = obj.getTemplate();
	if (!t)
	{
		return std::string();
	}
	// DONOR: CreateAHeroHero::Update (BFME2 RVA 0x4087E0..0x408802) caches
	// Drawable's first non-null draw-module portrait (RVA 0x272709, slot 0xCC).
	// Object::getObjectSelectedPortraitImage (RVA 0x2946AB) uses that before
	// SelectPortrait. The model state's PortraitImageName is already parsed;
	// resolve the object's conditions with the same matcher as the draw runtime.
	if (obj.isKindOfName("CREATE_A_HERO"))
	{
		ModelConditionFlags flags;
		for (int bit = 0; bit < MODELCONDITION_COUNT; ++bit)
		{
			flags.set(bit, obj.testModelCondition(bit));
		}
		for (const auto &module : t->drawModules().nuggets())
		{
			const auto *data = dynamic_cast<const W3DModelDrawModuleData *>(module.data.get());
			const ModelConditionInfo *state = data ? data->findBestInfo(flags) : nullptr;
			if (state && !state->portraitImageName.empty())
			{
				return state->portraitImageName;
			}
		}
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
	const ThingTemplate *lastTemplate = nullptr; // lane SELPERF-1: the portrait is the template's: ordinary objects of the template just compared share it
	for (ObjectID id : selected)
	{
		const Object *obj = ctx.logic.findObjectByID(id);
		if (!obj)
		{
			continue;
		}
		if (first && obj->getTemplate() && obj->getTemplate() == lastTemplate && !obj->isKindOfName("CREATE_A_HERO"))
		{
			continue;
		}
		lastTemplate = obj->getTemplate();
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
// RW 0x69194E: only an active enemy viewer sees an override. DISGUISED is a model condition, not KindOf.
const ThingTemplate *disguisedRankTemplate(const HudContext &ctx, const Object &obj)
{
	const Player *viewer = ctx.localPlayer();
	if (!viewer || viewer->isObserver() || viewer->isDefeated() || viewer->getRelationship(obj.getTeam()) != ENEMIES)
	{
		return nullptr;
	}
	if (obj.testModelCondition(300))
	{
		const SpecialDisguiseUpdate *sd = SpecialDisguiseUpdate::of(const_cast<Object &>(obj));
		return sd && !sd->enemyPerspectiveTemplate().empty() ? ctx.logic.things().findTemplate(sd->enemyPerspectiveTemplate()) : nullptr;
	}
	// RW 0x68FBD3: the first StealthUpdate whose required/forbidden upgrade masks pass (RW 0x7761ED).
	for (const auto &module : obj.modules())
	{
		const auto *st = dynamic_cast<const StealthUpdate *>(module.get());
		if (st && st->upgradesAllowStealth())
		{
			return st->disguiseTemplate();
		}
	}
	return nullptr;
}

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
	static const std::string kLifetime("LifetimeUpdate"); // lane SELPERF-1: not a temporary per selected object and frame
	const LifetimeUpdate *life = dynamic_cast<const LifetimeUpdate *>(o.findModule(kLifetime));
	unsigned start = 0, death = 0;
	if (life && !life->waitingForWakeUp())
	{
		r.type = 1;
		start = life->startFrame();
		death = life->dieFrame();
	}
	else if (obj.testStatus(0x3E)) // RW 0x92F7F8: TEMPORARILY_DEFECTED takes precedence over HIDDEN, even without a timer
	{
		const TemporarilyDefectUpdate *defect = TemporarilyDefectUpdate::of(o);
		if (defect && defect->endFrame() != 0)
		{
			r.type = 1;
			start = defect->startFrame();
			death = defect->endFrame();
		}
	}
	else if (obj.testStatus(0x10)) // RW 0x92F849: HIDDEN
	{
		const ToggleHiddenSpecialAbilityUpdate *hidden = ToggleHiddenSpecialAbilityUpdate::of(obj);
		if (hidden && hidden->showPalantirTimer())
		{
			r.type = 1;
			start = hidden->hideFrame();
			death = start + hidden->abilityData()->m_effectDuration;
		}
	}
	if (r.type == 1)
	{
		const unsigned now = ctx.logic.getFrame();
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
	const ThingTemplate *disguise = disguisedRankTemplate(ctx, obj);
	const ThingTemplate *shown = disguise ? disguise : obj.getTemplate();
	bool trainable = shown != nullptr;
	if (shown)
	{
		if (const FieldValue *v = shown->getFinalOverride()->findField("IsTrainable"))
		{
			if (const bool *b = std::get_if<bool>(v))
			{
				trainable = *b;
			}
		}
	}
	if (!trainable || !rankAndProgress(obj, r.rank, r.progress))
	{
		r.type = 2;
	}
	else if (disguise)
	{
		r.rank = 1; // RW 0x92F96D: progress stays the original object's, only the rank changes
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
	// lane SELPERF-1: rankInfo of an object without a LifetimeUpdate is its experience tracker's (the level, the experience, the level chain's template,
	// the game mode, trainable): an object whose tracker reads the same as the last one compared has its RankInfo (a horde army walks one chain each)
	static const std::string kLifetime("LifetimeUpdate");
	const Object *last = nullptr;
	RankInfo lastInfo;
	// whether an object has a LifetimeUpdate is its template's (an object's modules are made from its template's module list): asked once per template
	const ThingTemplate *lifeOf = nullptr;
	bool lifeHas = true;
	auto hasLife = [&](const Object &o) {
		if (!o.getTemplate() || o.getTemplate() != lifeOf)
		{
			lifeOf = o.getTemplate();
			lifeHas = o.findModule(kLifetime) != nullptr;
		}
		return lifeHas;
	};
	auto sameRank = [&](const Object &a, const Object &b) {
		const ExperienceTracker *x = a.getExperienceTracker(), *y = b.getExperienceTracker();
		if (!x || !y || a.getTemplate() != b.getTemplate() || hasLife(b)
			|| a.testStatus(0x3E) || b.testStatus(0x3E) || a.testStatus(0x10) || b.testStatus(0x10)
			|| a.testModelCondition(300) || b.testModelCondition(300)
			|| disguisedRankTemplate(ctx, a) || disguisedRankTemplate(ctx, b))
		{
			return false;
		}
		return x->getLevelName() == y->getLevelName() && x->getExperience() == y->getExperience() && x->levelTargetName() == y->levelTargetName()
			&& x->levelsForMultiplayer() == y->levelsForMultiplayer() && x->isTrainable() == y->isTrainable();
	};
	for (ObjectID id : selected)
	{
		const Object *obj = ctx.logic.findObjectByID(id);
		if (!obj)
		{
			continue;
		}
		const RankInfo r = (last && sameRank(*last, *obj)) ? lastInfo : rankInfo(ctx, *obj);
		last = obj;
		lastInfo = r;
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
		"[S-760] Palantir portrait: a drawable's portrait uses SelectPortrait or a Create-a-Hero model-state PortraitImageName; the draw module's template override (RW 0x694BF8) and the local-player branch of the "
		"kind-of bit +0x11F & 0x40 (RW 0x68FBD3) are not ported, nor the cost modifier interface of CommandUI (ShowCostModifierUpgradeInterface)",
	};
}
} // namespace PalantirCommandUI
