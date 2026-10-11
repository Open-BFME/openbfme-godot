// OpenBFME. GPL-3.0.
//
// LegendaryHeroesGame (lane RULES-1): OpenBFME's "Legendary Heroes" rule in the simulation. NOT a retail mechanic: an enhanced-profile rule the owner asked
// for (2026-10-10), chosen in the lobby's Rules tab (SkirmishGameInfo::legendaryHeroes, every LAN peer receives it with the game setup, the replay header
// carries it, GameLogic hashes it). It uses hooks the engine already has, no new mechanic:
//   * at the end of Object::friend_initObject (after the Create-a-Hero record, the first level and the weather modifier) an object of KindOf HERO whose
//     controlling player's template is a PlayableSide gets ActiveBody::setMaxHealth(max * N, 1) (RW 0x8C1CD5's ratio-keeping change: the health follows)
//     and its body damage scalar (RW body + 4, ActiveBody::attemptDamage step 7) divided by N: damage taken / N;
//   * its weapons' damage (not healing) is multiplied by (N + 5) / 6 after the attribute modifiers (NuggetDamageHost::sourceOpenBfmeDamageScalar).
// With the factor 1 (Off) nothing is called and nothing is hashed: a game without the rule is bit-identical to one from before it existed.
// DECISIONS (RULES-1): the factor applies to every hero made while the game runs (recruited, revived as a new object, Create-a-Hero, ring heroes, a map's
// own heroes of a player). ONE ownership policy (RULES-1 review): the creation decides, the factor is stored on the object (Object::legendaryFactor) and
// health, armour and damage dealt all follow it: a legendary hero captured by the neutral player stays legendary, a hero made for a non-playable owner and
// later given to a player does not become one; level-up HEALTH bonuses (flat, AttributeModifierPool) are not
// multiplied, HEALTH_MULT ones scale the already multiplied maximum.

#pragma once

class Object;

namespace LegendaryHeroesGame
{
// a new object is a hero the game's factor applies to (factor > 1, KindOf HERO, a controlling player whose template is a PlayableSide): asked at its creation
bool appliesTo(const Object &obj);
// the health and armour part, once per object (Object::friend_initObject)
void applyAtCreation(Object &obj);
// the damage dealt factor of `source` (from the factor it was made with; 1.0 for every other object)
float damageDealtScalar(const Object &source);
}
