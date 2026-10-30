# Lane WEATHER-1 (the weather: snow and rain, the Freezing Rain ramp, the Darkness darkening, lightning, the weather sound, the map's Weather /
# CloudEffect data): sources and tests. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client sources of openbfme_core (tools/sim/sim_policy.json excludes them: rendering and audio, no simulation state)
set(OPENBFME_WEATHER1_CLIENT_SOURCES
    src/GameClient/WeatherSetting.cpp
    src/GameClient/SnowManager.cpp
    src/GameClient/CloudEffect.cpp
    src/GameClient/WeatherClient.cpp
)
# the Godot device part (the snow mesh, the lightning and darkening of the terrain and the models)
set(OPENBFME_WEATHER1_GODOT_SOURCES
    src/GodotDevice/GodotWeather.cpp
)
set(OPENBFME_WEATHER1_TESTS
    tests/test_weather1.cpp
)
