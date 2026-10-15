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
};

namespace ObjectCollide
{
// ZH Object::onCollide: every collide module of `self` in module order meets `other`; stops when `self` is destroyed
void onCollide(Object &self, Object &other);
} // namespace ObjectCollide
