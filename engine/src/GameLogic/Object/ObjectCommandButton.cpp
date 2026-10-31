// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0. See GameLogic/Object/ObjectCommandButton.h.

#include "GameLogic/Object/ObjectCommandButton.h"

#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectDoCommandButton.h"

// lane MODCOMPAT-AI: one port of each retail function. Object::doCommandButton* (lane MODS-3, ObjectDoCommandButton.cpp) runs RW 0x696FD2 / 0x697890 /
// 0x6979D9 with every case it has (production, upgrades, castle pack / unpack, gates, the weapon lock, ...) and counts each case it has not
// (ObjectDoCommandButton::stats().unported, stop S-5305); the scripts' calls go through it and answer false when that count moved (the logic runs one
// frame at a time on one thread)

bool ObjectCommandButton::doCommandButton(Object &obj, const CommandButton *b, CommandSourceType source)
{
	const unsigned long long before = ObjectDoCommandButton::stats().unported;
	obj.doCommandButton(b, source, false);
	return ObjectDoCommandButton::stats().unported == before;
}

bool ObjectCommandButton::doCommandButtonAtObject(Object &obj, const CommandButton *b, Object *target, CommandSourceType source)
{
	const unsigned long long before = ObjectDoCommandButton::stats().unported;
	obj.doCommandButtonAtObject(b, target, source, false);
	return ObjectDoCommandButton::stats().unported == before;
}

bool ObjectCommandButton::doCommandButtonAtPosition(Object &obj, const CommandButton *b, const Coord3D &pos, CommandSourceType source)
{
	const unsigned long long before = ObjectDoCommandButton::stats().unported;
	obj.doCommandButtonAtPosition(b, pos, source, false);
	return ObjectDoCommandButton::stats().unported == before;
}
