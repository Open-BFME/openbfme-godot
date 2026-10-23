// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// CollideModuleInterface (ZH Include/GameLogic/Module/CollideModule.h): what a module does when its object touches another one. Lane HORDE-2.
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the collide modules of the registry (mask 16: SquishCollide RW create 0x650D84, HordeMemberCollide 0x650E45, ...) are
// asked through the +0xC interface slot of a module; SquishCollide::onCollide is RW 0x8BFBAE (`ret 0xC`: other, location, normal).
// DONOR: ZH Object::onCollide (Object.cpp): every collide module in module order, stopping when the object was destroyed.
// INFERENCE (stop S-581): the contact pairs come from MOVE-1's overlap pass of the AI units (AIWorld::processCollisions, S-220), not from RW's partition / physics
// contact (RW 0x62E93B); the location and normal of a contact are not computed (null).

#pragma once

class Object;
struct Coord3D;

class CollideModuleInterface
{
public:
	virtual ~CollideModuleInterface() = default;
	// `other` is never null here (ZH passes null for the ground; the overlap pass has no ground contacts)
	virtual void onCollide(Object *other, const Coord3D *loc, const Coord3D *normal) = 0;
	// lane BACKLOG-1: the collide interface's slot 1 (ZH CollideModuleInterface::wouldLikeToCollideWith). ActionManager::canEnterObject lets an object whose
	// collide module would like to collide with the container in (RW 0x82CD2E). RotWK: SquishCollide (vtable RW 0xC70EA4), HordeMemberCollide (RW 0xC711B8),
	// FireWeaponCollide (RW 0xC70CDC), CivilianSpawnCollide (RW 0xC71670) and AODCrushCollide (collide vtable RW 0xC70EF4) answer RW 0x7FEAC1 (false); only the
	// crate collides (RW 0x8C0664: CrateCollide's isValidToExecute) can answer yes
	virtual bool wouldLikeToCollideWith(const Object &other) const = 0;
	// lane UBER-2B: true when ObjectCollide::pairIsInert below knows every way this module's onCollide can act (SquishCollide, HordeMemberCollide). An object
	// with a module that answers false is never left out of a contact pair
	virtual bool pairFactsKnown() const { return false; }
};

namespace ObjectCollide
{
// ZH Object::onCollide: every collide module of `self` in module order meets `other`; stops when `self` is destroyed
void onCollide(Object &self, Object &other);

// lane UBER-2B (the Uber profile's collision pass, AIWorld::processCollisions): what an object's collide modules test before they act, taken once per object
// and pass instead of in every contact pair, both ways. Most pairs of a big battle are soldiers of one horde, or of two hordes not in a melee, where
// SquishCollide and HordeMemberCollide return at their first tests.
struct PairFacts
{
	const Object *container = nullptr; ///< getContainedBy
	bool known = false;                ///< every collide module answers pairFactsKnown (an object without collide modules too)
	bool deadOrHorde = false;          ///< as `other` of SquishCollide: effectively dead or HORDE
	bool squishInert = false;          ///< as `self` of SquishCollide: effectively dead or its physics motion disabled
	int crusher = 0, crushable = 0;    ///< ObjectCrush::crusherLevel / crushableLevel
	bool meleeHorde = false;           ///< contained by a horde whose HordeAIUpdate has a melee target (HordeMemberCollide acts only then)
};
PairFacts pairFacts(Object &obj);
// true when the collide modules of neither object can act on the other, as the facts describe them (SquishCollide: no crush either way; HordeMemberCollide:
// no melee horde, or both in the same container, where its tests repeat one that already failed)
bool pairIsInert(const PairFacts &a, const PairFacts &b);
} // namespace ObjectCollide
