#!/usr/bin/env python3
"""Independent census of the audio INI blocks of the pure RotWK 2.01 mount (AUDIO-1). Dev tool: nothing retail is committed.

    ROTWK_INSTALL=... BFME2_INSTALL=... python3 tools/audio/audio_ini_census.py [--write]   (prints the census, --write updates the golden)

The scan shares no code with the engine's INI reader: it strips ';' comments, takes the first token of a line (split on blanks and '=')
and counts a block per header keyword. The golden engine/tests/data/audio/ini_census.json is what tests/test_audio_ini.cpp compares the
engine's parse against (block counts, distinct event names per keyword, weighted sound tokens).
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bigread  # noqa: E402

FILES = ["audiosettings", "default\\music", "default\\speech", "default\\soundeffects", "default\\ambientstream", "music", "soundeffects", "speech", "voice", "ambientstream", "miscaudio"]
KEYWORDS = {"AudioEvent", "MusicTrack", "DialogEvent", "AmbientStream", "StreamedSound", "Multisound", "AudioSettings", "MiscAudio", "AnimationSoundClientBehaviorGlobalSetting"}
EVENT_KEYWORDS = {"AudioEvent", "MusicTrack", "DialogEvent", "AmbientStream", "StreamedSound", "Multisound"}
LIST_FIELDS = {"Sounds", "Attack", "Decay", "Subsounds"}


def census(mount):
    blocks = collections.Counter()
    names = collections.defaultdict(set)
    fields = collections.Counter()
    tokens = collections.Counter()
    for stem in FILES:
        text = mount.get("data\\ini\\" + stem + ".ini").decode("latin1")
        current = None
        for raw in text.replace("\r", "").split("\n"):
            line = raw.split(";")[0].split("//")[0].strip()
            if not line or line.startswith("#"):
                continue
            parts = line.replace("=", " ").split()
            if current is None:
                if parts[0] in KEYWORDS:
                    current = parts[0]
                    blocks[current] += 1
                    if current in EVENT_KEYWORDS:
                        names[current].add(parts[1])
                else:
                    raise SystemExit("unexpected top-level line in %s: %s" % (stem, line))
            elif parts[0].lower() == "end":
                current = None
            else:
                fields[current + "." + parts[0]] += 1
                if current in EVENT_KEYWORDS and parts[0] in LIST_FIELDS:
                    tokens[current + "." + parts[0]] += len(parts) - 1
    return {
        "blocks": dict(sorted(blocks.items())),
        "distinctNames": {k: len(v) for k, v in sorted(names.items())},
        "fieldCounts": dict(sorted(fields.items())),
        "listTokens": dict(sorted(tokens.items())),
    }


def census_eva_lga(mount):
    """Eva.ini (+ Default), LargeGroupAudio.ini and ScoredKillEvaAnnouncer.ini: block counts, nested block counts and list token counts."""
    out = {"blocks": collections.Counter(), "nested": collections.Counter(), "tokens": collections.Counter(), "names": collections.defaultdict(set)}
    for path, tops, nested in (("data\\ini\\default\\eva.ini", {"PredefinedEvaEvent"}, {"SideSound"}), ("data\\ini\\eva.ini", {"PredefinedEvaEvent", "NewEvaEvent", "EvaEventForwardReference", "MiscEvaData"}, {"SideSound"}),
                               ("data\\ini\\largegroupaudio.ini", {"LargeGroupAudioMap", "LargeGroupAudioUnusedKnownKeys"}, {"Sound"}), ("data\\ini\\scoredkillevaannouncer.ini", {"ScoredKillEvaAnnouncer"}, set())):
        depth = 0
        cur = None
        for raw in mount.get(path).decode("latin1").replace("\r", "").split("\n"):
            line = raw.split(";")[0].split("//")[0].strip()
            if not line or line.startswith("#"):
                continue
            parts = line.replace("=", " ").split()
            if depth == 0:
                if parts[0] in tops:
                    cur = parts[0]
                    out["blocks"][cur] += 1
                    if len(parts) > 1:
                        out["names"][cur].add(parts[1])
                    if cur not in ("EvaEventForwardReference",):
                        depth = 1
                else:
                    raise SystemExit("unexpected line in %s: %s" % (path, line))
            elif parts[0].lower() == "end":
                depth -= 1
            elif parts[0] in nested and "=" not in line.split(parts[0], 1)[1][:3] and not (len(parts) > 1 and parts[1] == "=") and (cur != "LargeGroupAudioMap" or True) and parts[0] == "SideSound" or (parts[0] == "Sound" and cur == "LargeGroupAudioMap" and "=" not in line):
                depth += 1
                out["nested"][cur + "." + parts[0]] += 1
            else:
                if parts[0] in ("Key", "OtherEvaEventsToBlock", "Duck"):
                    out["tokens"][cur + "." + parts[0]] += len(parts) - 1
                if parts[0] == "Duck":
                    out["nested"]["Duck"] += 1
    return {"blocks": dict(sorted(out["blocks"].items())), "distinctNames": {k: len(v) for k, v in sorted(out["names"].items())}, "nested": dict(sorted(out["nested"].items())),
            "tokens": dict(sorted(out["tokens"].items()))}


def main():
    rotwk, bfme2 = bigread.installs_from_env()
    mount = bigread.Mount(rotwk, bfme2)
    result = census(mount)
    result["evaAndLargeGroupAudio"] = census_eva_lga(mount)
    text = json.dumps(result, indent=1, sort_keys=True) + "\n"
    if "--write" in sys.argv:
        path = os.path.join(bigread.REPO, "engine", "tests", "data", "audio", "ini_census.json")
        with open(path, "w", newline="\n") as f:
            f.write(text)
        print("wrote", path)
    else:
        print(text)


if __name__ == "__main__":
    main()
