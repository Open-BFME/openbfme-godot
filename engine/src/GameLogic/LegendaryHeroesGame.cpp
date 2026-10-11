// OpenBFME. GPL-3.0.
// See GameLogic/LegendaryHeroesGame.h (an OpenBFME rule, not a port).

#include "GameLogic/LegendaryHeroesGame.h"

#include "Common/MpGameRules.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

bool LegendaryHeroesGame::appliesTo(const Object &obj)
{
	if (obj.logic().legendaryHeroes() <= LegendaryHeroes::kOff)
	{
		return false;
	}
	static const int kHero = ObjectTemplateInfoBuilder::kindOfIndex("HERO");
	if (kHero < 0 || !obj.isKindOf((unsigned)kHero))
	{
		return false;
	}
	const Player *p = obj.getControllingPlayer();
	const PlayerTemplate *pt = p ? p->getPlayerTemplate() : nullptr;
	return pt && pt->m_playableSide;
}

void LegendaryHeroesGame::applyAtCreation(Object &obj)
{
	if (!appliesTo(obj))
	{
		return;
	}
	ActiveBody *body = dynamic_cast<ActiveBody *>(obj.getBodyModule());
	if (!body)
	{
		return;
	}
	const int factor = obj.logic().legendaryHeroes();
	obj.setLegendaryFactor(factor);
	body->setMaxHealth(SimMath::mulf32(body->getMaxHealth(), LegendaryHeroes::healthFactor(factor)), 1);
	body->setBodyDamageScalar(SimMath::divf32(body->bodyDamageScalar(), LegendaryHeroes::armorFactor(factor)));
}

float LegendaryHeroesGame::damageDealtScalar(const Object &source)
{
	// the factor the object was made with, not its current owner's: the same policy as its health and armour
	return LegendaryHeroes::damageDealtFactor(source.legendaryFactor());
}
