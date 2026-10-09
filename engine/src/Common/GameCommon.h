// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Counterpart of ZH GeneralsMD/Code/GameEngine/Include/Common/GameCommon.h. Only the
// constants the rebuild uses so far live here; add more by porting them from that file.

#pragma once

// ----------------------------------------------------------------------------------------------
// Logic frame rate.
//
// Zero Hour steps game logic at 30 frames per second:
//   ZH GameEngine/Include/Common/GameCommon.h:72      LOGICFRAMES_PER_SECOND = 30
// BFME retail steps it at 5 frames per second. The BFME1 decompile recovers the constant
// from retail code that scales frame counts by it:
//   Open-BFME-1 game/GameEngine/Source/Common/System/BuildAssistant_sellObject.cpp:41-42
//     "Retail's LOGICFRAMES_PER_SECOND is 5"   enum { LOGICFRAMES_PER_SECOND = 5 };
// Every INI duration that is converted to frames depends on this value, so it must never be
// changed to match the render rate. Rendering interpolates between logic frames instead
// (see LogicFrameClock).
// ----------------------------------------------------------------------------------------------
enum
{
	LOGICFRAMES_PER_SECOND = 5,
	MSEC_PER_SECOND = 1000
};

const float LOGICFRAMES_PER_MSEC_REAL = ((float)LOGICFRAMES_PER_SECOND) / ((float)MSEC_PER_SECOND);
const float MSEC_PER_LOGICFRAME_REAL = ((float)MSEC_PER_SECOND) / ((float)LOGICFRAMES_PER_SECOND);
const float LOGICFRAMES_PER_SECONDS_REAL = (float)LOGICFRAMES_PER_SECOND;
const float SECONDS_PER_LOGICFRAME_REAL = 1.0f / LOGICFRAMES_PER_SECONDS_REAL;
