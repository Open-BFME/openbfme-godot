#!/usr/bin/env python3
"""Independent census of the audio FILES the retail INI events reference (AUDIO-1). Dev tool: nothing retail is committed, only the counts and the
names of the files that do not exist (the retail data defects).

    ROTWK_INSTALL=... BFME2_INSTALL=... python3 tools/audio/audio_files_census.py [--write]

Shares no code with the engine: it reads the nine audio INI files of the mount (tools/audio/bigread.py precedence model), keeps the LAST block of a
name (a redefinition resets the event), and expands the files the way AudioEventRTS names them (folder of the sound type + entry + '.wav' for
AudioEvents; folder + Filename for the other types; the Default* events are never played). The golden is engine/tests/data/audio/retail_files.json.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bigread  # noqa: E402
from audio_ini_census import FILES  # noqa: E402

FOLDERS = {"AudioEvent": "data\\audio\\sounds\\", "StreamedSound": "data\\audio\\sounds\\", "MusicTrack": "data\\audio\\tracks\\", "DialogEvent": "data\\audio\\speech\\",
           "AmbientStream": "data\\audio\\ambientstreams\\"}
LISTS = ("Sounds", "Attack", "Decay")


def entries(parts):
    out = []
    for tok in parts:
        # name[:weight]: the weight is the digits after the last ':' (a name may contain ':' when what follows is not all digits)
        i = tok.rfind(":")
        if i > 0 and tok[i + 1:].isdigit():
            tok = tok[:i]
        out.append(tok)
    return out


def census(mount):
    events = {}  # name -> (keyword, {field: [values]})
    for stem in FILES:
        text = mount.get("data\\ini\\" + stem + ".ini").decode("latin1")
        cur = None
        for raw in text.replace("\r", "").split("\n"):
            line = raw.split(";")[0].split("//")[0].strip()
            if not line or line.startswith("#"):
                continue
            parts = line.replace("=", " ").split()
            if cur is None:
                if parts[0] in FOLDERS:
                    cur = (parts[0], parts[1], {})
                elif parts[0] in ("Multisound", "AudioSettings", "MiscAudio", "AnimationSoundClientBehaviorGlobalSetting"):
                    cur = (parts[0], parts[1] if len(parts) > 1 else "", {})
            elif parts[0].lower() == "end":
                if cur[0] in FOLDERS:
                    events[cur[1]] = (cur[0], cur[2])  # the last block of a name wins
                cur = None
            else:
                cur[2].setdefault(parts[0], []).extend(parts[1:])
    files = {}  # path -> category
    for name, (kw, fields) in events.items():
        if name.startswith("Default"):
            continue
        if kw in ("AudioEvent",):
            for f in LISTS:
                for e in entries(fields.get(f, [])):
                    files["data\\audio\\sounds\\" + e.lower() + ".wav"] = "AudioEvent"
        else:
            for fn in fields.get("Filename", []):
                files[FOLDERS[kw] + fn.lower()] = kw
    present = set(mount.index)
    missing = sorted(p for p in files if p not in present)
    by_cat = {}
    for p, c in files.items():
        by_cat[c] = by_cat.get(c, 0) + 1
    return {
        "distinctFiles": len(files),
        "distinctFilesByKeyword": dict(sorted(by_cat.items())),
        "missing": missing,
        "missingByKeyword": dict(sorted({c: sum(1 for p in missing if files[p] == c) for c in set(files.values())}.items())),
    }


def main():
    rotwk, bfme2 = bigread.installs_from_env()
    result = census(bigread.Mount(rotwk, bfme2))
    text = json.dumps(result, indent=1, sort_keys=True) + "\n"
    if "--write" in sys.argv:
        path = os.path.join(bigread.REPO, "engine", "tests", "data", "audio", "retail_files.json")
        with open(path, "w", newline="\n") as f:
            f.write(text)
        print("wrote", path)
    else:
        print(text[:3000])


if __name__ == "__main__":
    main()
