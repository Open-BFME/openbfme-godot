#!/usr/bin/env python3
"""Which IMA ADPCM arithmetic do the retail files follow? (dev tool; the result is quoted in AudioDecode.h)

Every ADPCM block starts with a header whose predictor is the ENCODER's own value for the first sample of the
block. The last sample a decoder reconstructs for the previous block should therefore be close to the next
header predictor, with an error that only the encoder's quantisation and the signal slope explain. A decoder
whose arithmetic differs from the encoder's adds a drift on top. For a sample of retail mono ADPCM files this
prints the mean squared error of  (next header predictor - last reconstructed sample)  for both arithmetics
(reference series vs ffmpeg's multiplication) over the blocks whose error is small enough to be dominated by
the codec rather than by the music, and a paired t statistic. A clearly smaller error for one arithmetic means
the files were encoded for it.

  ROTWK_INSTALL=... BFME2_INSTALL=... python3 tools/audio/ima_statistics.py [--files 400] [--seed 2]
"""
import argparse
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bigread  # noqa: E402

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157,
        173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
        2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
        16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def last_sample(block, multiply):
    p = struct.unpack("<h", block[0:2])[0]
    ix = block[2]
    for byte in block[4:]:
        for nib in (byte & 15, byte >> 4):
            step = STEP[ix]
            if multiply:
                diff = ((2 * (nib & 7) + 1) * step) >> 3
            else:
                diff = step >> 3
                if nib & 4: diff += step
                if nib & 2: diff += step >> 1
                if nib & 1: diff += step >> 2
            p += -diff if nib & 8 else diff
            p = max(-32768, min(32767, p))
            ix = max(0, min(88, ix + INDEX[nib & 7]))
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", type=int, default=400)
    ap.add_argument("--seed", type=int, default=2)
    args = ap.parse_args()
    mount = bigread.Mount(*bigread.installs_from_env())
    names = mount.names(".wav")
    random.Random(args.seed).shuffle(names)
    diffs, closer_ref, closer_mul, used = [], 0, 0, 0
    for name in names:
        if used >= args.files:
            break
        d = mount.get(name)
        pos, fmt, ds, dn = 12, None, 0, 0
        while pos + 8 <= len(d):
            cid, sz = d[pos:pos + 4], struct.unpack("<I", d[pos + 4:pos + 8])[0]
            if cid == b"fmt ":
                fmt = d[pos + 8:pos + 8 + sz]
            if cid == b"data":
                ds, dn = pos + 8, sz
                break
            pos += 8 + sz + (sz & 1)
        tag, ch, _, _, ba, _ = struct.unpack("<HHIIHH", fmt[:16])
        if tag != 0x11 or ch != 1:
            continue
        used += 1
        for b in range(ds, ds + dn - 2 * ba + 1, ba):
            blk = d[b:b + ba]
            nxt = struct.unpack("<h", d[b + ba:b + ba + 2])[0]
            ref, mul = last_sample(blk, False), last_sample(blk, True)
            e_ref, e_mul = nxt - ref, nxt - mul
            if abs(e_ref) < 60 or abs(e_mul) < 60:
                diffs.append(e_mul * e_mul - e_ref * e_ref)
                closer_ref += abs(e_ref) < abs(e_mul)
                closer_mul += abs(e_mul) < abs(e_ref)
    n = len(diffs)
    mu = sum(diffs) / n
    sd = math.sqrt(sum((x - mu) ** 2 for x in diffs) / n)
    print("files %d, blocks with a small boundary error %d" % (used, n))
    print("mean(e_mul^2 - e_ref^2) = %.1f, t = %.1f; reference arithmetic closer in %d blocks, multiplication in %d" % (mu, mu / (sd / math.sqrt(n)), closer_ref, closer_mul))
    print("positive mean / t >> 2: the retail files follow the reference series" if mu > 0 else "negative mean: the files follow ffmpeg's multiplication")


if __name__ == "__main__":
    main()
