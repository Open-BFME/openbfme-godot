// OpenBFME. GPL-3.0.
//
// CameraSettings (lane CAM-1): the GameData fields the tactical camera reads, and the map's own camera overrides.
//
// TARGET FACTS (RotWK game.dat GlobalData = RW 0xDE4364, caveat S-001; the field-table rows were read in the GameData table RW 0xBFF700 .. 0xC01000: the
// row is { name, parse proc, user data, offset }):
//   DefaultCameraMinHeight +0x9C, DefaultCameraMaxHeight +0xA0, DefaultCameraPitchAngle +0xA4, DefaultCameraYawAngle +0xA8, DefaultCameraScrollSpeedScalar +0xAC
//   (parseReal RW 0x42ED00); TerrainHeightAtEdgeOfMap +0xB0; CameraLockHeightDelta +0xDD4, CameraTerrainSampleRadiusForHeight +0xDD8, CameraEaseFactor +0xDE0;
//   HorizontalScrollSpeedFactor +0xA9C, VerticalScrollSpeedFactor +0xAA0, ScreenEdgeScrollSpeedFactor +0xAA4, ScreenEdgeScrollRampTime +0xAA8 (RW 0x42EE37:
//   scanReal, times 1000.0f, truncated to an int: milliseconds), ScrollAmountCutoff +0xAAC, CameraAdjustSpeed +0xAB0, EnforceMaxCameraHeight +0xAB4 (parseBool RW
//   0x42E558), KeyboardScrollSpeedFactor +0xAF8, KeyboardDefaultScrollSpeedFactor +0xAFC, KeyboardCameraRotateSpeed +0xC2C, UseCameraInReplay +0xB71,
//   PartitionCellSize +0xD4. TerrainHeightAtEdgeOfMap, CameraAudibleRadius and the shake intensities are not camera inputs of this lane.
// GlobalData's constructor (RW 0x642A.. .. 0x6437..) sets +0xDE4 (the map height smoothness, no INI field) to 1.0f: CameraSettings::mapHeightSmoothness.
// The map overrides (RW 0x501328 family at 0x50136C .. 0x501454 and the height field builder RW 0x710107) are Dict reals of the map's WorldInfo chunk: cameraMinHeight,
// cameraMaxHeight, cameraPitchAngle, cameraYawAngle, cameraScrollSpeedScalar, cameraMapHeightSmoothnessScalar, cameraGroundMinHeight, cameraGroundMaxHeight; a key
// that is absent (or not a real) leaves the GameData value.
//
// A key that is missing from the install's GameData is an error (PLAN rule 10), except CameraEaseFactor and KeyboardDefaultScrollSpeedFactor (absent in retail: the
// constructor defaults above): the constructor values of the other keys were not resolved (stop S-451).

#pragma once

#include "GameLogic/ObjectFilter.h"

#include <string>
#include <vector>

class ArchiveFileSystem;
class Dict;

struct CameraSettings
{
	bool loaded = false;
	float defaultMinHeight = 0.0f, defaultMaxHeight = 0.0f, defaultPitchAngle = 0.0f, defaultYawAngle = 0.0f, defaultScrollSpeedScalar = 0.0f;
	float lockHeightDelta = 0.0f, terrainSampleRadius = 0.0f, partitionCellSize = 0.0f;
	// no value in the retail GameData (CameraEaseFactor is commented out there): the constructor defaults RW 0x6436D2 (0.2f, RW 0xBDAD78) and RW 0x6430C2 (1.0f)
	float easeFactor = 0.2f;
	bool useCameraInReplay = false;
	float scrollAmountCutoff = 0.0f, cameraAdjustSpeed = 0.0f;
	bool enforceMaxCameraHeight = false;
	float horizontalScrollSpeedFactor = 0.0f, verticalScrollSpeedFactor = 0.0f, screenEdgeScrollSpeedFactor = 0.0f;
	int screenEdgeScrollRampTimeMs = 0;
	float keyboardScrollSpeedFactor = 0.0f, keyboardDefaultScrollSpeedFactor = 1.0f, keyboardCameraRotateSpeed = 0.0f;
	// GlobalData +0xDE4 (RW constructor, RW 0x6436DA: 1.0f; no INI field)
	float mapHeightSmoothness = 1.0f;
	// lane PLAY-1: the in-game UI's move hint model (MoveHintName, GlobalData + 0x10, row RW 0xBFF5C0, parseAsciiString RW 0x42EE5E; retail "SCMoveHint").
	// Optional: a GameData without it leaves it empty and the HUD reports that no hint is drawn (the constructor's value was not read)
	std::string moveHintName;
	// lane HUD-5: what the drawable decorations read (GameClient/DrawableIconUI.h): ShowObjectHealth (GlobalData + 0x9BD, parseBool, row RW 0xC00120) and
	// VeterancyPipDrawObjectFilter (GlobalData + 0xEB8, ParseObjectFilter RW 0x76392F, row RW 0xC00C40). Optional (not every GameData has them): absent, the
	// health bars and the veterancy marks are off and the HUD reports it
	bool showObjectHealth = false;
	// lane UI-4: the selection marker (GameClient/SelectionDecals.h): ShowSelectedUnitMarker, UseSimpleHordeDecals, UseSimpleMergeDecals (GlobalData + 0x9A5 ..
	// + 0x9A7, parseBool) and OpacityOfSimpleMergeDecals (+ 0x9A8, parsePercentToReal). Optional: absent, no marker is drawn (the ctor's values were not read)
	bool showSelectedUnitMarker = false, useSimpleHordeDecals = false, useSimpleMergeDecals = false;
	float opacityOfSimpleMergeDecals = 0.0f;
	bool haveVeterancyPipFilter = false;
	ObjectFilter veterancyPipFilter;

	// reads data\ini\gamedata.ini through the shared INI pipeline (macros, retail parsers, later blocks override); a missing file or key is an error
	static bool load(ArchiveFileSystem &fs, CameraSettings &out, std::string *error);
	static bool scan(const std::string &text, CameraSettings &out, std::string *error);
};

// The values the camera uses for one map: GameData's, then the map dict's reals where present (RW 0x50136C .. 0x501454, 0x710107 .. 0x7101B5).
struct MapCameraValues
{
	float minHeight = 0.0f, maxHeight = 0.0f, pitchAngle = 0.0f, yawAngle = 0.0f, scrollSpeedScalar = 0.0f;
	float heightSmoothness = 0.0f, groundMinHeight = 0.0f, groundMaxHeight = 0.0f;
	// which keys the map carried (for the report)
	std::vector<std::string> overridden;

	// `worldInfo` may be null (no WorldInfo chunk: every value is GameData's). RW's defaults of the ground limits: -9999999.0f / 9999999.0f (RW 0xC1FCEC / 0xC11F8C).
	static MapCameraValues resolve(const CameraSettings &gameData, const Dict *worldInfo);
};
