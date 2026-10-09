// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Version constants of the map chunks (ZH GameEngine/Include/Common/MapReaderWriterInfo.h:34-45),
// extended with the versions the retail BFME2/RotWK corpus carries (spec maps-and-terrain.md
// 1.6; the extra constants are named after their BFME meaning where the corpus shows one).

#pragma once

#define K_HEIGHT_MAP_VERSION_1 1 // Height map cell = 5.0
#define K_HEIGHT_MAP_VERSION_2 2 // Height map cell = 10.0
#define K_HEIGHT_MAP_VERSION_3 3 // Added m_borderSize
#define K_HEIGHT_MAP_VERSION_4 4 // Multiple boundaries. BFME: 8-bit heights promoted to 16 bit (b*16+0.5)
#define K_HEIGHT_MAP_VERSION_5 5 // BFME: 16-bit heights stored directly (corpus: every map is v5)
#define K_BLEND_TILE_VERSION_1 1
#define K_BLEND_TILE_VERSION_2 2
#define K_BLEND_TILE_VERSION_3 3 // Added long diagonal blends.
#define K_BLEND_TILE_VERSION_4 4 // Added custom edge blends.
#define K_BLEND_TILE_VERSION_5 5 // Added custom cliff u/v coordinates.
#define K_BLEND_TILE_VERSION_6 6 // Added extra blend layer for 3 textures in cell.
#define K_BLEND_TILE_VERSION_7 7 // Added cliff-state (impassable) plane.
#define K_BLEND_TILE_VERSION_8 8 // Plane stride fixed to (width+7)/8.
#define K_OBJECTS_VERSION_1 1    // no dict
#define K_OBJECTS_VERSION_2 2    // includes dict
#define K_OBJECTS_VERSION_3 3    // includes dict, z is stored
#define K_WAYPOINTS_VERSION_1 1
#define K_TRIGGERS_VERSION_1 1
#define K_TRIGGERS_VERSION_2 2 // Added m_isWaterArea
#define K_TRIGGERS_VERSION_3 3 // Added m_isRiver & m_riverStart
#define K_TRIGGERS_VERSION_4 4 // Added layer name.
#define K_TRIGGERS_VERSION_5 5 // Added river/water presentation fields (OpenSAGE layout, corpus-verified)

// ZH WorldHeightMap.h: FLAG_VAL, the sentinel after every blended-tile record.
#define BLEND_TILE_FLAG_VAL 0x7ADA0000u
