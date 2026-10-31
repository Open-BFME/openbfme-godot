#!/usr/bin/env python3
"""Create small, source-only parser seeds in an ignored build directory.

No retail files are read or written. Usage: python3 engine/fuzz/make_seeds.py
engine/build-fuzz/corpus [engine/build-baseline/audio_fixtures]
"""

import pathlib
import shutil
import struct
import subprocess
import sys
import wave
import io


def le(*values):
    return struct.pack("<" + "I" * len(values), *values)


def seed(name, filename, data):
    global written
    directory = root / name
    directory.mkdir(parents=True, exist_ok=True)
    (directory / filename).write_bytes(data)
    written += 1


written = 0
root = pathlib.Path(sys.argv[1]).resolve()
repo = pathlib.Path(__file__).resolve().parents[2]
if root.is_relative_to(repo):
    ignored = subprocess.run(["git", "check-ignore", "-q", str(root)], cwd=repo).returncode == 0
    if not ignored:
        sys.exit("fuzz corpora must be written under a git-ignored directory")

# Minimal files use the same layouts as the synthetic engine test utilities.
big_name, big_data = b"data/fuzz.ini\0", b"AudioSettings\n  DefaultSoundVolume = 50\nEnd\n"
big_directory = struct.pack(">II", 16 + 8 + len(big_name), len(big_data)) + big_name
big = b"BIGF" + le(16 + len(big_directory) + len(big_data)) + struct.pack(">II", 1, 16 + len(big_directory)) + big_directory + big_data
seed("big", "one-entry", big)
seed("csf", "one-string", b" FSC" + le(3, 1, 1, 0, 0) + b" LBL" + le(1, 4) + b"TEST" + b" RTS" + le(2) + struct.pack("<HH", ~ord("o") & 0xFFFF, ~ord("k") & 0xFFFF))
seed("str", "one-string", b"TEST:TEXT\n\"fuzz string\"\nEND\n")
seed("ini", "audio-settings", big_data)
seed("ini", "unknown-field", b"AudioSettings\n Unknown = 42\nEnd\n")
seed("ini", "object", b"Object Plain\n  KindOf = INFANTRY\n  Body = ActiveBody ModuleTag_01\n    MaxHealth = 100\n  End\nEnd\n")
seed("ini", "macros", b"#define HP 100\nObject Unit\n  Body = ActiveBody ModuleTag_01\n    MaxHealth = HP\n  End\nEnd\nObjectReskin Unit2 Unit\nEnd\n")
seed("ini", "upgrade-command", b"Upgrade Upgrade_Test\n  Type = PLAYER\n  BuildCost = 10\nEnd\n"
     b"CommandButton Command_Test\n  Command = PLAYER_UPGRADE\n  Upgrade = Upgrade_Test\nEnd\n"
     b"CommandSet TestSet\n  1 = Command_Test\nEnd\n")
seed("ini", "experience", b"ExperienceLevel Level1\n  TargetNames = Plain\n  RequiredExperience = 0\n  Rank = 1\nEnd\n")
# Block names for the INI fuzzer's -dict (engine/build-fuzz/campaign usage: -dict=corpus/ini.dict).
ini_words = ["AIBase", "ArmyDefinition", "AudioEvent", "AudioSettings", "ChildObject", "CommandButton", "CommandMap",
             "CommandSet", "CreateAHeroSystem", "EmotionNugget", "ExperienceLevel", "FXList", "GameData", "LinearCampaign",
             "Locomotor", "LocomotorSet", "MappedImage", "ModifierList", "Multisound", "MusicTrack", "Object",
             "ObjectCreationList", "ObjectReskin", "PlayerTemplate", "Rank", "Science", "SpecialPower", "StanceTemplate",
             "Upgrade", "Weapon", "Armor", "End", "#define", "#include", "Behavior", "Body", "Draw", "ModuleTag_01", "="]
(root / "ini.dict").write_text("".join('"%s"\n' % word for word in ini_words))
# A CkMp table of contents, followed by a zero-length height-map chunk.
seed("map", "map-chunks", b"CkMp" + le(0))
seed("w3d", "empty-chunk", le(0, 0))
consts = bytearray(32)
consts[:17] = b"Apt constant file"
consts[17] = 0x1A
consts[20:32] = le(0x100, 0, 32)
apt = bytearray(0x100 + 60)
apt[:11] = b"Apt Data:7\x1a"
apt[0x100:0x108] = le(9, 0x09876543)
apt[0x100+8+20:0x100+8+32] = le(1, 1, 33)
seed("apt_const", "empty-constants", consts)
seed("apt", "empty-movie", le(len(consts)) + consts + apt)
seed("apt_dat", "two-mappings", b"; generated map\n46->1\n3=0 0 112 5\n")
seed("apt_geometry", "one-triangle", b"c\ns s:255:0:0:255\nt 0:0:10:0:0:10\n")
seed("tga", "one-pixel", bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 24, 0, 0, 127, 255]))
wav = io.BytesIO()
with wave.open(wav, "wb") as out:
    out.setnchannels(1)
    out.setsampwidth(2)
    out.setframerate(22050)
    out.writeframes(struct.pack("<hhhh", 0, 200, -200, 0))
seed("audio", "short-wav", wav.getvalue())
seed("replay", "replay-prefix", b"OBFMEREC" + le(5))
seed("save", "save-prefix", b"OBFMESAV" + le(1))
# An ICONDIR with one 1x1 32-bit DIB. The entry's pixel data is synthetic.
dib = le(40, 1, 2) + struct.pack("<HH", 1, 32) + le(0, 4, 0, 0, 0, 0) + b"\x00\x7f\xff\xff\x00\x00\x00\x00"
seed("cursor", "tiny-cur", struct.pack("<HHH", 0, 2, 1) + struct.pack("<BBBBHHII", 1, 1, 0, 0, 0, 0, len(dib), 22) + dib)
vp6head = b"VP60" + struct.pack("<HHIIII", 16, 16, 1, 4, 15, 1)
seed("vp6", "movie-header", b"MVhd" + le(32) + vp6head + b"MV0K" + le(12) + b"\x00\x00\x00\x00")
seed("json", "nested", b'{"fuzz": [1, true, null, {"key": "value"}]}')
seed("refpack", "envelope", b"\x10\xfb\x00\x00\x00\xfc")
seed("font", "mapping", b"Arial=Arial\n")
seed("map_cache", "entry", b"Map = Fuzz\n")
seed("text_file", "lines", b"// generated\nKey Value\n")

if len(sys.argv) > 2:
    fixtures = pathlib.Path(sys.argv[2])
    for mp3 in fixtures.glob("*.mp3.bin"):
        shutil.copyfile(mp3, root / "audio" / mp3.name)
        written += 1
print("generated", written, "seed files")
