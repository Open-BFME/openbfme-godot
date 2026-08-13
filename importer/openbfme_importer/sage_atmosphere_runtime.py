"""Compile the runtime atmosphere contract from water.ini and weather.ini.

Godot's RetailSageAtmosphere snapshot must stay aligned with these
assignments. This module is the importer-side proof, not a second set of
numbers.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Mapping

from .sage_environment import _assignment_report, _parse_ini_blocks

WATER_INI = "data/ini/water.ini"
WEATHER_INI = "data/ini/weather.ini"
WEATHER_DATA_NAMES = ("RAINY", "CLOUDYRAINY", "SUNNY", "CLOUDY", "NONE")
WATER_SET_NAMES = ("MORNING", "AFTERNOON", "EVENING", "NIGHT")


def _assignment_map(block: Any) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for item in block.assignments:
        row = _assignment_report(item)
        result[str(row["key"])] = row["value"]
    return result


def compile_atmosphere(effective_root: Path | str) -> dict[str, Any]:
    root = Path(effective_root)
    water_blocks = _parse_ini_blocks(
        (root / WATER_INI).read_bytes(),
        label=WATER_INI,
        block_shapes={"WaterSet": True, "WaterTransparency": False},
    )
    weather_blocks = _parse_ini_blocks(
        (root / WEATHER_INI).read_bytes(),
        label=WEATHER_INI,
        block_shapes={"Weather": False, "WeatherData": True},
    )
    water_sets: dict[str, dict[str, Any]] = {}
    transparency: dict[str, Any] | None = None
    for block in water_blocks:
        if block.kind == "WaterSet" and block.name:
            water_sets[block.name] = _assignment_map(block)
        elif block.kind == "WaterTransparency" and block.name is None:
            transparency = _assignment_map(block)
    if transparency is None:
        raise ValueError("water.ini is missing WaterTransparency")
    weather: dict[str, Any] | None = None
    weather_data: dict[str, dict[str, Any]] = {}
    for block in weather_blocks:
        if block.kind == "Weather" and block.name is None:
            weather = _assignment_map(block)
        elif block.kind == "WeatherData" and block.name:
            weather_data[block.name] = _assignment_map(block)
    if weather is None:
        raise ValueError("weather.ini is missing Weather")
    missing_sets = [name for name in WATER_SET_NAMES if name not in water_sets]
    if missing_sets:
        raise ValueError(f"water.ini missing WaterSet(s): {missing_sets}")
    missing_weather = [name for name in WEATHER_DATA_NAMES if name not in weather_data]
    if missing_weather:
        raise ValueError(f"weather.ini missing WeatherData: {missing_weather}")
    return {
        "schema": "openbfme.sage-atmosphere",
        "schemaVersion": 0,
        "waterSets": water_sets,
        "waterTransparency": transparency,
        "weather": weather,
        "weatherData": weather_data,
        "sources": [WATER_INI, WEATHER_INI],
    }


def assert_matches_godot_snapshot(document: Mapping[str, Any]) -> None:
    afternoon = document["waterSets"]["AFTERNOON"]
    if afternoon.get("WaterTexture") != "TSWater.tga":
        raise ValueError("AFTERNOON WaterTexture drifted")
    if float(afternoon.get("UScrollPerMS", -1)) != 0.002:
        raise ValueError("AFTERNOON UScrollPerMS drifted")
    if int(afternoon.get("WaterRepeatCount", -1)) != 32:
        raise ValueError("AFTERNOON WaterRepeatCount drifted")
    diffuse = afternoon.get("DiffuseColor")
    if not isinstance(diffuse, dict) or float(diffuse.get("R", -1)) != 185.0 or float(diffuse.get("G", -1)) != 185.0 or float(diffuse.get("B", -1)) != 185.0:
        raise ValueError(f"AFTERNOON DiffuseColor drifted: {diffuse!r}")
    night = document["waterSets"]["NIGHT"]
    if float(night.get("UScrollPerMS", -1)) != 0.0 or float(night.get("VScrollPerMS", -1)) != 0.0:
        raise ValueError("NIGHT scroll drifted from zero")
    transparency = document["waterTransparency"]
    if float(transparency.get("TransparentWaterDepth", -1)) != 3.0:
        raise ValueError("TransparentWaterDepth drifted")
    if transparency.get("StandingWaterTexture") != "TWWater01.tga":
        raise ValueError("StandingWaterTexture drifted")
    weather = document["weather"]
    if weather.get("SnowTexture") != "EXRainDrop.tga":
        raise ValueError("SnowTexture drifted")
    if float(weather.get("SnowSpeed", -1)) != 50.0:
        raise ValueError("SnowSpeed drifted")
    if weather.get("IsSnowing") is not False:
        raise ValueError("IsSnowing drifted")
    if document["weatherData"]["RAINY"].get("HasLightning") is not True:
        raise ValueError("RAINY HasLightning drifted")
    if document["weatherData"]["SUNNY"].get("HasLightning") is not False:
        raise ValueError("SUNNY HasLightning drifted")
