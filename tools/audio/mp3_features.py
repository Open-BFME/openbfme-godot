#!/usr/bin/env python3
"""Counts which Layer III features the retail MP3 files exercise (side-information scan, no audio decoding).

  ROTWK_INSTALL=... BFME2_INSTALL=... python3 tools/audio/mp3_features.py

Reports, over every MP3 of the mount: MPEG version / sample rate / channel mode counts, frames with long / start /
short / stop blocks, MIXED blocks, joint-stereo mode extensions (intensity / mid-side), scfsi use, pre-emphasis,
scalefac_scale, count1 table select and the Huffman table numbers used. Features that never occur in retail data
are the ones the decoder cannot be verified on with retail files.
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bigread  # noqa: E402

BR1 = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0]
BR2 = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0]
SR = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}


class Bits:
    def __init__(self, b):
        self.v = int.from_bytes(b, "big")
        self.n = len(b) * 8
        self.p = 0

    def get(self, k):
        self.p += k
        return (self.v >> (self.n - self.p)) & ((1 << k) - 1)


def main():
    mount = bigread.Mount(*bigread.installs_from_env())
    c = collections.Counter()
    tables = collections.Counter()
    for name in mount.names(".mp3"):
        d = mount.get(name)
        pos = 0
        while d[pos:pos + 3] == b"ID3":
            pos += 10 + ((d[pos + 6] << 21) | (d[pos + 7] << 14) | (d[pos + 8] << 7) | d[pos + 9])
        first_pos = pos
        while pos + 4 <= len(d):
            h = d[pos:pos + 4]
            if not (h[0] == 0xFF and (h[1] & 0xE0) == 0xE0):
                break
            ver = (h[1] >> 3) & 3
            if ver == 1 or ((h[1] >> 1) & 3) != 1:
                break
            br = (BR1 if ver == 3 else BR2)[h[2] >> 4]
            sr = SR[ver][(h[2] >> 2) & 3]
            fl = (144 if ver == 3 else 72) * br * 1000 // sr + ((h[2] >> 1) & 1)
            if pos + fl > len(d):
                break
            if pos == first_pos and d[pos + 36:pos + 40] == b"VBRI":
                c["first frame carries a VBRI header (files)"] += 1
            ch = 1 if (h[3] >> 6) == 3 else 2
            c["mpeg%s %d Hz %s" % ({3: "1", 2: "2", 0: "2.5"}[ver], sr, "mono" if ch == 1 else "stereo")] += 1
            if ch == 2 and (h[3] >> 6) == 1:
                me = (h[3] >> 4) & 3
                c["joint-stereo mode_ext %d" % me] += 1
            si = d[pos + 4 + (0 if (h[1] & 1) else 2):pos + fl]
            b = Bits(si[:32])
            ngr = 2 if ver == 3 else 1
            b.get(9 if ver == 3 else 8)
            b.get((5 if ch == 1 else 3) if ver == 3 else (1 if ch == 1 else 2))
            if ver == 3:
                for _ in range(ch):
                    if b.get(4):
                        c["scfsi used"] += 1
            for g in range(ngr):
                for _ in range(ch):
                    b.get(12); big = b.get(9); b.get(8); b.get(4 if ver == 3 else 9)
                    ws = b.get(1)
                    if ws:
                        bt = b.get(2); mixed = b.get(1)
                        ts = [b.get(5), b.get(5)]
                        b.get(9)
                        c["window switching blocktype %d%s" % (bt, " MIXED" if mixed else "")] += 1
                    else:
                        ts = [b.get(5), b.get(5), b.get(5)]
                        b.get(7)
                        c["long block (no window switching)"] += 1
                    if ver == 3 and b.get(1):
                        c["preflag set"] += 1
                    elif ver != 3:
                        pass
                    if b.get(1):
                        c["scalefac_scale 1"] += 1
                    c["count1table %d" % b.get(1)] += 1
                    for t in ts:
                        tables[t] += 1
            pos += fl
    for k, v in sorted(c.items()):
        print("%9d  %s" % (v, k))
    print("Huffman table_select values used:", " ".join("%d:%d" % kv for kv in sorted(tables.items())))


if __name__ == "__main__":
    main()
