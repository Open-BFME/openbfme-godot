# AUDIO-1 (retail audio): sources and tests for the openbfme_core library.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_AUDIO_SOURCES
    # audio file decoders (PCM / IMA ADPCM / MS ADPCM WAV, MPEG Layer III)
    src/Common/Audio/AudioDecode.cpp
    src/Common/Audio/Mp3Decoder.cpp
    src/Common/Audio/Mp3Tables.cpp
    # INI: AudioEvent family, AudioSettings, MiscAudio, AudioLOD, ...
    src/Common/Audio/AudioEventInfo.cpp
    src/Common/Audio/AudioSettings.cpp
    src/Common/Audio/MiscAudio.cpp
    src/Common/Audio/AudioIni.cpp
    src/Common/Audio/EvaEvents.cpp
    src/Common/Audio/LargeGroupAudio.cpp
    # the manager core: event instances, the asset cache, the simulated device
    src/Common/Audio/AudioEventRTS.cpp
    src/Common/Audio/AudioAssetCache.cpp
    src/Common/Audio/GameAudio.cpp
    src/Common/Audio/AudioEntryPoints.cpp
    src/Common/Audio/SimulatedAudioDevice.cpp
    # AUDIO-5: the channel gains of Miles 6.6g (its volume law, the Fast 2D 3D provider)
    src/Common/Audio/MilesMix.cpp
    # EVA (the announcer)
    src/GameClient/Eva.cpp
    # AUDIO-2: the unit voice picker (RW 0x8DEDBB), the in-game audio of a live game
    src/GameClient/UnitVoiceResponse.cpp
    src/GameClient/LiveGameAudio.cpp
    src/GameClient/MusicScripts.cpp)

set(OPENBFME_AUDIO_TESTS
    tests/test_audio_decode.cpp
    tests/test_audio_ini.cpp
    tests/test_audio_manager.cpp
    tests/test_audio_retail.cpp
    tests/test_audio_eva.cpp
    tests/test_audio5.cpp
    tests/test_world_context.cpp
    tests/test_unit_voice.cpp
    tests/test_music_scripts.cpp)

# Dev tool: decodes a WAV / MP3 file to raw s16le on stdout (tools/audio/oracle_check.py drives it against ffmpeg / mpg123).
add_executable(audio_decode_cli tests/audio_decode_cli.cpp)
target_link_libraries(audio_decode_cli PRIVATE openbfme_core)

# The Godot half of the audio device (compiled into the GDExtension only).
set(OPENBFME_AUDIO_GODOT_SOURCES
    src/GodotDevice/GodotGameAudio.cpp)
