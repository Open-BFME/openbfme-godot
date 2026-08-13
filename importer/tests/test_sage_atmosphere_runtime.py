from pathlib import Path

from openbfme_importer.sage_atmosphere_runtime import (
    assert_matches_godot_snapshot,
    compile_atmosphere,
)


def test_compile_atmosphere_fixture(tmp_path: Path) -> None:
    water = tmp_path / "data" / "ini"
    water.mkdir(parents=True)
    (water / "water.ini").write_text(
        """
WaterSet AFTERNOON
  WaterTexture = TSWater.tga
  DiffuseColor = R:185 G:185 B:185 A:255
  UScrollPerMS = 0.002
  VScrollPerMS = 0.002
  WaterRepeatCount = 32
End
WaterSet MORNING
  WaterTexture = TSWater.tga
  DiffuseColor = R:175 G:175 B:175 A:255
  UScrollPerMS = 0.002
  VScrollPerMS = 0.002
  WaterRepeatCount = 32
End
WaterSet EVENING
  WaterTexture = TSWater.tga
  DiffuseColor = R:225 G:225 B:225 A:255
  UScrollPerMS = 0.002
  VScrollPerMS = 0.002
  WaterRepeatCount = 32
End
WaterSet NIGHT
  WaterTexture = TSWater.tga
  DiffuseColor = R:100 G:100 B:100 A:255
  UScrollPerMS = 0.0
  VScrollPerMS = 0.0
  WaterRepeatCount = 32
End
WaterTransparency
  TransparentWaterDepth = 3.0
  StandingWaterTexture = TWWater01.tga
  StandingWaterColor = R:255 G:255 B:255
End
""",
        encoding="utf-8",
    )
    (water / "weather.ini").write_text(
        """
Weather
  SnowEnabled = yes
  IsSnowing = no
  SnowTexture = EXRainDrop.tga
  SnowSpeed = 50.0
End
WeatherData RAINY
  WeatherSound = RainStereoLoop
  HasLightning = Yes
End
WeatherData CLOUDYRAINY
  WeatherSound = RainStereoLoop
  HasLightning = Yes
End
WeatherData SUNNY
  HasLightning = No
End
WeatherData CLOUDY
  HasLightning = No
End
WeatherData NONE
End
""",
        encoding="utf-8",
    )
    document = compile_atmosphere(tmp_path)
    assert_matches_godot_snapshot(document)
    assert document["weatherData"]["RAINY"]["WeatherSound"] == "RainStereoLoop"
