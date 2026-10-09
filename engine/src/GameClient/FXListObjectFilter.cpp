// OpenBFME. GPL-3.0.
//
// The ObjectFilter fields of an FXList nugget (RW 0x76392f, iniParseObjectFilter). Its own translation unit because
// GameLogic/ObjectFilter.h (HORDE-1) and Common/ModelState.h (DRAW-1) each define a different ModelConditionFlags typedef and
// cannot be included together. `store` is the nugget's std::shared_ptr<ObjectFilter> member (null = RW's index -1, accept all):
// SourceObjectFilter filters the primary object, ObjectFilter the secondary (RW 0x5df671).

#include "Common/INI.h"
#include "GameLogic/ObjectFilter.h"

#include <memory>

void ParseFXNuggetObjectFilter(INI *ini, void *instance, void *store, const void *)
{
	std::shared_ptr<ObjectFilter> filter = std::make_shared<ObjectFilter>();
	ParseObjectFilter(ini, instance, filter.get(), nullptr);
	*(std::shared_ptr<ObjectFilter> *)store = std::move(filter);
}
