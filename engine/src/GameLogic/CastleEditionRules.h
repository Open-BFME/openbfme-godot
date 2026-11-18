// OpenBFME. GPL-3.0.
// Castle-system variants. BFME1 1.03 unpack (RVA 0x00376590, 1104 bytes,
// Open-BFME-1 d_0036ed40.asm) retains the flag's script name. RotWK
// RW 0x79BE6A / RW 0x79C0FD and BFME2's CastleBehaviorRegisterOwnedObject.cpp
// move it to the keep. BFME1's AI_Initialize library addresses BASE_FLAG_n.
#pragma once

#include "Common/Thing/ThingTemplate.h"

namespace CastleEditionRules
{
struct Rules
{
	bool moveNameToKeep;
};
inline Rules forTemplate(const ThingTemplate &tt)
{
	return { !tt.bfme1Data() };
}
}
