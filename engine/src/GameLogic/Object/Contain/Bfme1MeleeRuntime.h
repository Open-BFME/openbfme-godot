// OpenBFME. GPL-3.0.
// Original BFME1 1.03 melee system; callers select this variant, never RW's.
#pragma once
#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"
#include <vector>
class Object;
class AIUpdateInterface;
namespace Bfme1Melee
{
// Object 0x001CBDC0 and recipient 0x001CFF30.
void acquire(Object &object, bool includeCurrent = false);
bool receive(Object &object, Object &other);
bool targetMovingAway(Object &object, Object *other); // 0x00175820.
bool view(Object &object, const Coord3D &from, Object &other); // 0x003EA570.
bool movementClear(Object &object); // 0x003E5E40.
// Scalar footprint queries / acceptance, 0x003E4680 and 0x003DF250.
std::vector<ObjectID> footprintGoals(Object &object, const Coord3D &position);
bool goalFootprintFree(Object &object, const Coord3D &position);
// Native idle acquisition byte +0x335; later editions have no writer here.
void autoAcquired(AIUpdateInterface &ai);
}
