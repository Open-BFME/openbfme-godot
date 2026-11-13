// OpenBFME. GPL-3.0.
// AI-system dispatch: BFME1 1.03 uses its original mood and target search.
#pragma once
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Object/Object.h"
struct AIEditionRules {
 bool bfme1;
 static const AIEditionRules &forObject(const Object &o) {
  static const AIEditionRules original{true},later{false};
  return o.getTemplate()->bfme1Data()?original:later;
 }
};
