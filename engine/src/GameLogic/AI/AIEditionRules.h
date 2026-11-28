// OpenBFME. GPL-3.0.
// AI-system dispatch: BFME1 1.03 uses its original mood and target search.
#pragma once
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Object/Object.h"
struct AIEditionRules {
 bool bfme1;
 // APPROACHGIVEUP-1 ports BFME2/RotWK's generic approach; retain BFME1's existing path until its own rules are verified (S-5017).
 bool usesSharedApproachGiveUp() const { return !bfme1; }
 static const AIEditionRules &forObject(const Object &o) {
  static const AIEditionRules original{true},later{false};
  return o.getTemplate()->bfme1Data()?original:later;
 }
};
