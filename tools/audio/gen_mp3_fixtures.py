#!/usr/bin/env python3
"""Generates the MP3 test fixtures (into an ignored build directory) and checks or rewrites their golden numbers.

  python3 tools/audio/gen_mp3_fixtures.py --out DIR [--check-only | --write-golden] [--cli path/to/audio_decode_cli]

Dev/test-time only (needs `lame` and `mpg123`; `ffmpeg` is used for a cross-check when present). Synthetic signals
(sweeps, clicks, silence; nothing from the game) are encoded with lame into DIR/*.mp3.bin. The streams are NEVER committed
(retail-format bytes, synthetic ones included, stay out of git): the CMake target `audio_mp3_fixtures` runs this into
<build>/audio_fixtures, and the unit tests read that directory (OPENBFME_AUDIO_FIXTURES overrides it) and fail loudly,
naming this command, when it is missing.
The committed golden file engine/tests/data/audio/mp3_golden.tsv (numbers only) records, from the mpg123 float decode rounded
to 16 bit (its default output): sample rate, channels, frame count, peak, RMS, and every 251st interleaved sample. The unit
test compares the OpenBFME decoder against those numbers (peak and points within 2 LSB, RMS within 0.5), so it needs no
external tool. Without --write-golden this script compares the numbers it just derived with the committed golden and the
streams with mp3_fixtures.tsv (sizes and SHA-256). Valid existing streams are reused without invoking any tools.
--check-only validates existing streams without attempting generation on failure. Generation requires LAME 3.100.
--write-golden deliberately updates both contracts; it is not a workaround for a non-reference encoder/decoder.
When generating, --cli also prints the maximum and RMS difference of the OpenBFME decoder against mpg123.
"""
import argparse
import hashlib
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GOLDEN = os.path.join(ROOT, "engine", "tests", "data", "audio", "mp3_golden.tsv")
MANIFEST = os.path.join(ROOT, "engine", "tests", "data", "audio", "mp3_fixtures.tsv")
STRIDE = 251


class Lcg:
    def __init__(self, seed):
        self.s = seed

    def next(self):  # uniform in [-1, 1)
        self.s = (self.s * 1103515245 + 12345) & 0x7FFFFFFF
        return (self.s / 0x40000000) - 1.0


def sweep(rate, secs, f0, f1, amp, phase=0.0):
    n = int(rate * secs)
    return [amp * math.sin(2 * math.pi * (f0 * (i / rate) + (f1 - f0) * (i / rate) ** 2 / (2 * secs)) + phase) for i in range(n)]


def clicks(rate, secs, period, amp, seed):
    rng = Lcg(seed)
    n = int(rate * secs)
    out = [0.0] * n
    burst = int(rate * 0.012)
    for start in range(int(rate * 0.05), n, int(rate * period)):
        for k in range(min(burst, n - start)):
            out[start + k] += amp * rng.next() * math.exp(-5.0 * k / burst)
    return out


def mix(*parts):
    n = max(len(p) for p in parts)
    return [sum(p[i] if i < len(p) else 0.0 for p in parts) for i in range(n)]


def music(rate, secs, seed, amp=0.35):
    return mix(sweep(rate, secs, 220, 1800, amp), sweep(rate, secs, 3100, 2400, amp * 0.3), clicks(rate, secs, 0.21, 0.5, seed))


def write_wav(path, rate, channels):
    def go(chans):
        n = len(chans[0])
        with wave.open(path, "wb") as w:
            w.setnchannels(len(chans))
            w.setsampwidth(2)
            w.setframerate(rate)
            frames = bytearray()
            for i in range(n):
                for c in chans:
                    v = int(round(max(-1.0, min(1.0, c[i])) * 32767))
                    frames += struct.pack("<h", v)
            w.writeframes(bytes(frames))
    return go


def id3v2(text):
    body = b"TIT2" + struct.pack(">I", len(text) + 1) + b"\x00\x00" + b"\x00" + text.encode("latin1")
    pad = b"\x00" * 64
    size = len(body) + len(pad)
    sync = bytes([(size >> 21) & 0x7F, (size >> 14) & 0x7F, (size >> 7) & 0x7F, size & 0x7F])
    return b"ID3\x03\x00\x00" + sync + body + pad


def id3v1():
    return b"TAG" + b"synthetic test tag".ljust(30, b"\x00") + b"\x00" * 95


# name, rate, channels, lame arguments, signal builder
FIXTURES = [
    ("mono_44k", 44100, 1, ["-b", "32"], lambda r: [music(r, 0.9, 1)]),
    ("joint_44k", 44100, 2, ["-m", "j", "-b", "64"], lambda r: [music(r, 0.9, 2), mix(music(r, 0.9, 2), sweep(r, 0.9, 500, 900, 0.1))]),
    ("stereo_44k", 44100, 2, ["-m", "s", "-b", "64"], lambda r: [music(r, 0.6, 3), sweep(r, 0.6, 700, 300, 0.4)]),
    ("joint_48k", 48000, 2, ["-m", "j", "-b", "64"], lambda r: [music(r, 0.7, 4), mix(sweep(r, 0.7, 300, 2600, 0.3), clicks(r, 0.7, 0.3, 0.4, 9))]),
    ("mono_48k", 48000, 1, ["-b", "32"], lambda r: [music(r, 0.5, 5)]),
    ("mono_32k", 32000, 1, ["-b", "32"], lambda r: [music(r, 0.5, 6)]),
    ("mpeg2_22k_mono", 22050, 1, ["-b", "24"], lambda r: [music(r, 1.0, 7)]),
    ("mpeg2_22k_joint", 22050, 2, ["-m", "j", "-b", "40"], lambda r: [music(r, 0.6, 8), mix(music(r, 0.6, 8), sweep(r, 0.6, 400, 800, 0.1))]),
    ("mpeg2_24k_mono", 24000, 1, ["-b", "24"], lambda r: [music(r, 0.4, 10)]),
    ("mpeg2_16k_mono", 16000, 1, ["-b", "16"], lambda r: [music(r, 0.4, 11)]),
    ("mpeg25_12k_mono", 12000, 1, ["-b", "16"], lambda r: [music(r, 0.4, 12)]),
    ("mpeg25_11k_mono", 11025, 1, ["-b", "16"], lambda r: [music(r, 0.4, 13)]),
    ("mpeg25_8k_mono", 8000, 1, ["-b", "16"], lambda r: [music(r, 0.4, 14)]),
    ("silence_44k", 44100, 1, ["-b", "32"], lambda r: [[0.0] * int(r * 0.3)]),
    ("notag_44k", 44100, 2, ["-t", "-m", "j", "-b", "64"], lambda r: [music(r, 0.5, 15), sweep(r, 0.5, 150, 1000, 0.3)]),
    ("vbr_44k", 44100, 2, ["-V", "9"], lambda r: [music(r, 0.5, 16), sweep(r, 0.5, 900, 200, 0.3)]),
    ("id3_44k", 44100, 1, ["-t", "-b", "32"], lambda r: [music(r, 0.4, 17)]),
]


RESAMPLE = {44100: "44.1", 48000: "48", 32000: "32", 22050: "22.05", 24000: "24", 16000: "16", 12000: "12", 11025: "11.025", 8000: "8"}


def check_rate(data, rate, name):
    """lame may choose another output rate than asked for; fail if the first frame header is not `rate`."""
    pos = 0
    while data[pos:pos + 3] == b"ID3":
        pos += 10 + ((data[pos + 6] << 21) | (data[pos + 7] << 14) | (data[pos + 8] << 7) | data[pos + 9])
    h = data[pos:pos + 4]
    version = (h[1] >> 3) & 3
    table = {3: (44100, 48000, 32000), 2: (22050, 24000, 16000), 0: (11025, 12000, 8000)}[version]
    got = table[(h[2] >> 2) & 3]
    if got != rate:
        sys.exit("%s: lame produced %d Hz, wanted %d Hz" % (name, got, rate))


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def data_lines(path):
    with open(path, encoding="utf-8") as f:
        return [line for line in f.read().splitlines() if line and not line.startswith("#")]


def fixture_hashes(out):
    rows = []
    for name, *_ in FIXTURES:
        path = os.path.join(out, name + ".mp3.bin")
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError as e:
            sys.exit("MP3 fixture missing or unreadable: %s (%s)" % (path, e))
        rows.append("%s\t%d\t%s" % (name, len(data), hashlib.sha256(data).hexdigest()))
    return rows


def check_fixtures(out):
    want = data_lines(MANIFEST)
    names = [name for name, *_ in FIXTURES]
    if [line.split("\t")[0] for line in want] != names or [line.split("\t")[0] for line in data_lines(GOLDEN)] != names:
        sys.exit("MP3 fixture names differ between the generator, mp3_fixtures.tsv and mp3_golden.tsv")
    for expected, got in zip(want, fixture_hashes(out)):
        if expected != got:
            sys.exit("MP3 fixture size/SHA-256 mismatch for %s; use the reference fixtures matching mp3_fixtures.tsv" % got.split("\t")[0])
    print("Verified %d MP3 fixtures (sizes and SHA-256)" % len(names))


def lame_version(tool):
    result = run([tool, "--version"], capture_output=True, text=True)
    banner = result.stdout + result.stderr
    match = re.search(r"\bversion\s+(\d+(?:\.\d+)+)\b", banner, re.IGNORECASE)
    if not match:
        sys.exit("Cannot determine lame version; fixtures need 3.100")
    if match[1] != "3.100":
        sys.exit("lame %s found, fixtures need 3.100" % match[1])
    return banner.splitlines()[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", help="audio_decode_cli to compare against mpg123")
    ap.add_argument("--out", required=True, help="directory that receives the *.mp3.bin streams (an ignored build directory)")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--write-golden", action="store_true", help="rewrite mp3_golden.tsv and mp3_fixtures.tsv instead of checking them")
    mode.add_argument("--check-only", action="store_true", help="verify existing fixture sizes/hashes without lame or mpg123")
    ap.add_argument("--lame", default="lame", help="LAME executable (CMake passes its resolved host tool)")
    ap.add_argument("--mpg123", default="mpg123", help="mpg123 executable (CMake passes its resolved host tool)")
    args = ap.parse_args()
    OUT = os.path.abspath(args.out)
    if args.check_only:
        check_fixtures(OUT)
        return
    if not args.write_golden:
        try:
            check_fixtures(OUT)
        except SystemExit:
            pass
        else:
            return
    try:
        inside_source = os.path.commonpath([OUT, ROOT]) == ROOT
    except ValueError:  # different Windows drives
        inside_source = False
    if inside_source:
        probe = os.path.join(OUT, "x.mp3.bin")
        ignored = subprocess.run(["git", "-C", ROOT, "check-ignore", "-q", probe]).returncode == 0
        if not ignored:
            sys.exit("--out %s is inside the source tree and not git-ignored: fixture streams must go to an ignored build directory" % OUT)
    for tool in (args.lame, args.mpg123):
        if not shutil.which(tool):
            sys.exit("MP3 fixture tool not found: %s; run this generator outside the CMake sandbox with lame and mpg123 on PATH" % tool)
        if tool == args.lame:
            versions = lame_version(tool)
    with tempfile.TemporaryDirectory(prefix="obfme_mp3fx_") as tmp:
        generate(args, tmp, versions)
        # Publish only after both reference contracts have passed.
        os.makedirs(OUT, exist_ok=True)
        for name, *_ in FIXTURES:
            shutil.copyfile(os.path.join(tmp, name + ".mp3.bin"), os.path.join(OUT, name + ".mp3.bin"))


def generate(args, tmp, versions):
    OUT = tmp
    lines = []
    total = 0
    mpg_version = run([args.mpg123, "--version"], capture_output=True, text=True).stdout.strip()
    lines.append("# generated by tools/audio/gen_mp3_fixtures.py; %s; %s" % (versions, mpg_version))
    lines.append("# name\trate\tchannels\tframes\tpeak\trms\tevery 251st interleaved sample of the mpg123 s16 decode")
    for name, rate, channels, lame_args, builder in FIXTURES:
        wav = os.path.join(tmp, name + ".wav")
        mp3 = os.path.join(tmp, name + ".mp3")
        chans = builder(rate)
        write_wav(wav, rate, channels)(chans)
        run([args.lame, "--quiet", "--resample", RESAMPLE[rate]] + lame_args + [wav, mp3])
        data = open(mp3, "rb").read()
        check_rate(data, rate, name)
        if name == "id3_44k":
            data = id3v2("OpenBFME fixture") + data + id3v1()
            open(mp3, "wb").write(data)
        if len(data) >= 12 * 1024:
            sys.exit("%s is %d bytes (limit 12288)" % (name, len(data)))
        total += len(data)
        with open(os.path.join(OUT, name + ".mp3.bin"), "wb") as f:
            f.write(data)
        raw = run([args.mpg123, "-q", "-s", mp3], capture_output=True).stdout
        samples = struct.unpack("<%dh" % (len(raw) // 2), raw)
        frames = len(samples) // channels
        peak = max(abs(s) for s in samples)
        rms = math.sqrt(sum(s * s for s in samples) / len(samples))
        pts = samples[::STRIDE]
        lines.append("%s\t%d\t%d\t%d\t%d\t%.3f\t%s" % (name, rate, channels, frames, peak, rms, ",".join(map(str, pts))))
        note = ""
        if shutil.which("ffmpeg"):
            ff = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", mp3, "-f", "s16le", "-acodec", "pcm_s16le", "-"],
                                capture_output=True, check=True).stdout
            fs = struct.unpack("<%dh" % (len(ff) // 2), ff)
            n = min(len(fs), len(samples))
            d = max(abs(fs[i] - samples[i]) for i in range(n))
            note += " ffmpeg-vs-mpg123 max diff %d (len %d vs %d)" % (d, len(fs), len(samples))
        if args.cli:
            out = subprocess.run([args.cli, mp3], capture_output=True, check=True).stdout
            cs = struct.unpack("<%dh" % (len(out) // 2), out)
            n = min(len(cs), len(samples))
            d = max(abs(cs[i] - samples[i]) for i in range(n))
            e = math.sqrt(sum((cs[i] - samples[i]) ** 2 for i in range(n)) / n)
            note += " openbfme-vs-mpg123 max %d rms %.4f (len %d vs %d)" % (d, e, len(cs), len(samples))
        print("%-18s %6d bytes %5d frames%s" % (name, len(data), frames, note))
    if total >= 60 * 1024:
        sys.exit("fixtures total %d bytes (limit 61440)" % total)
    if args.write_golden:
        with open(GOLDEN, "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        with open(MANIFEST, "w", newline="\n") as f:
            f.write("# name\tsize\tsha256; reference streams matching mp3_golden.tsv\n")
            f.write("\n".join(fixture_hashes(OUT)) + "\n")
    else:
        want = data_lines(GOLDEN)
        got = [line for line in lines if not line.startswith("#")]
        if want != got:
            for w, g in zip(want, got):
                if w != g:
                    print("golden mismatch:\n  committed %s\n  derived   %s" % (w[:160], g[:160]), file=sys.stderr)
                    break
            sys.exit("the fixtures derived here differ from mp3_golden.tsv (lame / mpg123 versions?); use the reference fixtures, not --write-golden")
        check_fixtures(OUT)
    print("total %d bytes in %d fixtures" % (total, len(FIXTURES)))


if __name__ == "__main__":
    main()
