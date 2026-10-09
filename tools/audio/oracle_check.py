#!/usr/bin/env python3
"""Independent oracle check of the AudioDecode decoders against ffmpeg and mpg123 (dev tool, nothing is committed).

  ROTWK_INSTALL=... BFME2_INSTALL=... python3 tools/audio/oracle_check.py --cli build/audio_decode_cli [--samples 20] [--seed 1]

For N randomly sampled retail files per category (the files that win the mount order, extracted with
tools/audio/bigread.py into a temp directory that is deleted afterwards) it decodes with audio_decode_cli and
with ffmpeg (and mpg123 for MP3) and reports the largest absolute sample difference and the RMS difference over
the common length:

  PCM16 mono / stereo     bit-exact against ffmpeg's pcm_s16le is required
  IMA ADPCM mono / stereo bit-exact against ffmpeg's adpcm_ima_wav is required for `--ima-multiply` (ffmpeg's
                          ((2m+1)*step)>>3 arithmetic); the default decode (reference series, what the retail
                          files were encoded for) is reported separately
  MP3 mono / stereo       within 2 LSB peak and below 1 LSB RMS of mpg123 and ffmpeg (float decoders rounded to s16)

ADPCM files are compared over the length the CLI reports (the 'fact' chunk trims the padded last block; ffmpeg
decodes the whole block). Exit status 1 when a requirement is violated.
"""
import argparse
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bigread  # noqa: E402


def wav_kind(data):
    """('ima'|'pcm16'|'other', channels) from the fmt chunk."""
    pos = 12
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        if cid == b"fmt ":
            tag, ch = struct.unpack("<HH", data[pos + 8:pos + 12])
            bits = struct.unpack("<H", data[pos + 22:pos + 24])[0]
            if tag == 0x11:
                return "ima", ch
            if tag == 1 and bits == 16:
                return "pcm16", ch
            return "other", ch
        pos += 8 + size + (size & 1)
    return "other", 0


def mp3_channels(data):
    pos = 0
    while data[pos:pos + 3] == b"ID3":
        pos += 10 + ((data[pos + 6] << 21) | (data[pos + 7] << 14) | (data[pos + 8] << 7) | data[pos + 9])
    while pos + 4 <= len(data) and not (data[pos] == 0xFF and (data[pos + 1] & 0xE0) == 0xE0):
        pos += 1
    return 1 if (data[pos + 3] >> 6) == 3 else 2


def run_cli(cli, path, out, extra=()):
    with open(out, "wb") as f:
        r = subprocess.run([cli, *extra, path], stdout=f, stderr=subprocess.PIPE)
    err = r.stderr.decode("latin1")
    m = re.search(r"frames=(\d+)", err)
    ch = re.search(r"channels=(\d+)", err)
    return r.returncode, (int(m.group(1)) if m else None), (int(ch.group(1)) if ch else None), err


def mp3_samples_per_frame(data):
    pos = 0
    while data[pos:pos + 3] == b"ID3":
        pos += 10 + ((data[pos + 6] << 21) | (data[pos + 7] << 14) | (data[pos + 8] << 7) | data[pos + 9])
    return 1152 if ((data[pos + 1] >> 3) & 3) == 3 else 576


def vbri_first_frame(data):
    pos = 0
    while data[pos:pos + 3] == b"ID3":
        pos += 10 + ((data[pos + 6] << 21) | (data[pos + 7] << 14) | (data[pos + 8] << 7) | data[pos + 9])
    return data[pos + 36:pos + 40] == b"VBRI"


def compare(cli, a, b, limit_samples=None, skip_a=0):
    if skip_a:
        with open(a, "rb") as f:
            f.seek(skip_a * 2)
            data = f.read()
        with open(a + ".skip", "wb") as f:
            f.write(data)
        a = a + ".skip"
    # trim b to limit_samples when given (ADPCM fact trimming), then ask the CLI's raw comparer
    if limit_samples is not None:
        with open(b, "rb") as f:
            data = f.read(limit_samples * 2)
        with open(b + ".trim", "wb") as f:
            f.write(data)
        b = b + ".trim"
    out = subprocess.run([cli, "--compare", a, b], capture_output=True, text=True).stdout
    return dict(kv.split("=") for kv in out.split())


def ffmpeg_decode(path, out):
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", path, "-f", "s16le", "-acodec", "pcm_s16le", out], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True)
    ap.add_argument("--samples", type=int, default=20)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    for tool in ("ffmpeg", "mpg123"):
        if not shutil.which(tool):
            print("SKIP: %s not found (the oracle check needs ffmpeg and mpg123)" % tool)
            return 77
    mount = bigread.Mount(*bigread.installs_from_env())
    rng = random.Random(args.seed)
    cats = {"ima-mono": [], "ima-stereo": [], "pcm16-mono": [], "pcm16-stereo": [], "mp3-mono": [], "mp3-stereo": []}
    for name in mount.names(".wav"):
        disk, off, size = mount.index[name]
        with open(disk, "rb") as f:
            f.seek(off)
            head = f.read(min(size, 256))
        kind, ch = wav_kind(head)
        if kind in ("ima", "pcm16") and ch in (1, 2):
            cats["%s-%s" % (kind, "mono" if ch == 1 else "stereo")].append(name)
    for name in mount.names(".mp3"):
        data = mount.get(name)
        cats["mp3-%s" % ("mono" if mp3_channels(data) == 1 else "stereo")].append(name)
    tmp = tempfile.mkdtemp(prefix="obfme_oracle_")
    failed = False
    try:
        for cat, names in cats.items():
            picked = rng.sample(names, min(args.samples, len(names)))
            worst = {"mult": (0, 0.0), "ref": (0, 0.0), "ffmpeg": (0, 0.0), "mpg123": (0, 0.0)}
            lenmis = 0
            vbri = 0
            for name in picked:
                src = os.path.join(tmp, "in" + os.path.splitext(name)[1])
                with open(src, "wb") as f:
                    f.write(mount.get(name))
                ff = os.path.join(tmp, "ff.raw")
                cli = os.path.join(tmp, "cli.raw")
                ffmpeg_decode(src, ff)
                if cat.startswith("mp3"):
                    rc, frames, ch, err = run_cli(args.cli, src, cli)
                    if rc:
                        print("FAIL %s: %s" % (name, err.strip())); failed = True; continue
                    # ffmpeg drops a Fraunhofer VBRI first frame (silence); mpg123 and OpenBFME decode it
                    skip = 0
                    if vbri_first_frame(mount.get(name)):
                        skip = mp3_samples_per_frame(mount.get(name)) * ch
                        vbri += 1
                    c = compare(args.cli, cli, ff, skip_a=skip)
                    worst["ffmpeg"] = (max(worst["ffmpeg"][0], int(c["max"])), max(worst["ffmpeg"][1], float(c["rms"])))
                    mp = os.path.join(tmp, "mpg.raw")
                    with open(mp, "wb") as f:
                        subprocess.run(["mpg123", "-q", "-s", src], stdout=f, check=True)
                    c2 = compare(args.cli, cli, mp)
                    worst["mpg123"] = (max(worst["mpg123"][0], int(c2["max"])), max(worst["mpg123"][1], float(c2["rms"])))
                    if int(c2["lenA"]) != int(c2["lenB"]):
                        lenmis += 1
                else:
                    for mode, extra in (("ref", ()), ("mult", ("--ima-multiply",))) if cat.startswith("ima") else (("ref", ()),):
                        rc, frames, ch, err = run_cli(args.cli, src, cli, extra)
                        if rc:
                            print("FAIL %s: %s" % (name, err.strip())); failed = True; break
                        c = compare(args.cli, cli, ff, frames * ch)
                        worst[mode] = (max(worst[mode][0], int(c["max"])), max(worst[mode][1], float(c["rms"])))
                        if os.path.getsize(ff) // 2 < frames * ch:
                            lenmis += 1  # ffmpeg decoded fewer samples than the CLI delivered (a fact chunk larger than the data)
            if cat.startswith("pcm16"):
                line = "bit-exact vs ffmpeg pcm_s16le: max %d rms %.4f" % worst["ref"]
                ok = worst["ref"][0] == 0
            elif cat.startswith("ima"):
                line = ("--ima-multiply vs ffmpeg adpcm_ima_wav: max %d rms %.4f; default (reference series) vs ffmpeg: max %d rms %.4f"
                        % (worst["mult"] + worst["ref"]))
                ok = worst["mult"][0] == 0
            else:
                line = "vs mpg123: max %d rms %.4f; vs ffmpeg: max %d rms %.4f; length mismatches vs mpg123: %d (%d with a VBRI first frame)" % (worst["mpg123"] + worst["ffmpeg"] + (lenmis, vbri))
                ok = worst["mpg123"][0] <= 2 and worst["mpg123"][1] < 1.0 and worst["ffmpeg"][0] <= 2 and worst["ffmpeg"][1] < 1.0 and lenmis == 0
            print("%-13s %3d files  %s  %s" % (cat, len(picked), line, "OK" if ok else "VIOLATION"))
            failed = failed or not ok
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
