#!/usr/bin/env python3
"""Extracts the audio block field tables and name arrays from the RotWK game.dat into engine/tests/data/audio/rw_audio_tables.json (AUDIO-1).

    RW_GAME_DAT=<path to RotWK game.dat> python3 tools/audio/extract_tables.py [--write]

Binary facts (RW addresses, caveat S-001): the block parse functions are listed in docs / spec audio.md; each table is read with the shared
tools/rw_object_model reader (rows {token, parse function, userData, offset}). The golden is what tests/test_audio_ini.cpp compares the engine's
registered field names (and name arrays) against, so a field the binary parses can never be missing from the port.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "rw_object_model"))
from rwimage import Image  # noqa: E402
from rwtables import Tables  # noqa: E402

# (block keyword, parse function VA)
BLOCKS = [
    ("AudioEvent", 0x5DA159), ("MusicTrack", 0x5DA10F), ("DialogEvent", 0x5DA1A3), ("AmbientStream", 0x5DA1ED), ("StreamedSound", 0x5DA237),
    ("Multisound", 0x5D9C6D), ("AudioSettings", 0x44107A), ("MiscAudio", 0x5ECE27), ("AudioLOD", 0x602905), ("NewEvaEvent", 0x5DEB41),
    ("PredefinedEvaEvent", 0x5DE1AA), ("MiscEvaData", 0x5DC743), ("ScoredKillEvaAnnouncer", 0x8258BF), ("LivingWorldSound", 0x7FC8E1),
    ("AnimationSoundClientBehaviorGlobalSetting", 0x83F3EA), ("LargeGroupAudioUnusedKnownKeys", 0x60DADD), ("CrowdResponse", 0x826674),
]
# the AudioEvent family parses through RW 0x5D9EFF with the table below (the thunks pass the sound type and the default's name)
EVENT_TABLE = 0xBEFDF0
NAME_ARRAYS = {"priority": 0xD9DB28, "soundType": 0xD9DB40, "control": 0xD9DB70, "submixSlider": 0xD9DB94, "predefinedEva": 0xBF2168}


def name_array(img, va, count=None):
    out = []
    while count is None or len(out) < count:
        p = img.u32(va)
        if p == 0:
            break
        out.append(img.cstr(p))
        va += 4
    return out


def main():
    img = Image()
    T = Tables(img)
    result = {"blocks": {}, "nameArrays": {}}
    for kw, fn in BLOCKS:
        info = T.fn_info(fn)
        tables = []
        for e in info.get("tables", []):
            if "va" in e:
                rows, _ = T.read_table(e["va"], T._patches_for(e["va"], e["origin"]) if e.get("origin") else None)
            else:
                rows = e["rows"]
            tables.append([{"name": r[0], "offset": r[3], "parser": "0x%x" % r[1]} for r in rows])
        result["blocks"][kw] = {"function": "0x%x" % fn, "tables": tables}
    rows, _ = T.read_table(EVENT_TABLE)
    result["eventTable"] = [{"name": r[0], "offset": r[3], "parser": "0x%x" % r[1]} for r in rows]
    for k, va in NAME_ARRAYS.items():
        result["nameArrays"][k] = name_array(img, va, 22 if k == "predefinedEva" else None)
    text = json.dumps(result, indent=1, sort_keys=True) + "\n"
    if "--write" in sys.argv:
        path = os.path.join(HERE, "..", "..", "engine", "tests", "data", "audio", "rw_audio_tables.json")
        with open(path, "w", newline="\n") as f:
            f.write(text)
        print("wrote", os.path.normpath(path))
    else:
        print(text[:2000])


if __name__ == "__main__":
    main()
