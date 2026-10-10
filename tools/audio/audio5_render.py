"""Lane AUDIO-5: render a short stereo battle through the port's stereo model before and after AUDIO-5 (tools/audio/miles_model.py) and
plot the channel balance (L / R RMS over time) as an SVG.

The battle is synthetic: retail impact, arrow and death sounds (read from the retail Audio.big at runtime, decoded with ffmpeg) fire at
random positions over RotWK's default tactical view (camera looking north at (1000, 1000)); for the first half the fight is spread over the
screen, then it moves to the upper left. Each event's volume is the game's linear distance falloff (MinRange / MaxRange below) from the
listener of that model, then the model's channel gains. Nothing is committed: the output goes to the git-ignored workspace.

    python3 tools/audio/audio5_render.py --game "<BFME2 install>" --out workspace/rebuild/videos/audio5
"""
import argparse
import array
import math
import os
import random
import struct
import subprocess
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import miles_model as mm  # noqa: E402

RATE = 22050
SECONDS = 12.0
MIN_RANGE, MAX_RANGE = 150.0, 600.0  # a typical SoundEffects.ini world sound


def big_entries(path):
    with open(path, 'rb') as f:
        head = f.read(16)
        count = struct.unpack('>I', head[8:12])[0]
        table = f.read(struct.unpack('>I', head[12:16])[0])
    out, o = {}, 0
    for _ in range(count):
        off, size = struct.unpack('>II', table[o:o + 8])
        o += 8
        e = table.index(b'\0', o)
        out[table[o:e].decode('latin1').lower()] = (off, size)
        o = e + 1
    return out


def decode(big, entry):
    with open(big, 'rb') as f:
        f.seek(entry[0])
        data = f.read(entry[1])
    pcm = subprocess.run(['ffmpeg', '-v', 'error', '-i', '-', '-f', 's16le', '-ac', '1', '-ar', str(RATE), '-'], input=data, capture_output=True, check=True).stdout
    a = array.array('h')
    a.frombytes(pcm)
    return [x / 32768.0 for x in a]


def falloff(listener, pos):
    d = math.dist(listener, pos)
    if d >= MAX_RANGE:
        return 0.0
    if d > MIN_RANGE:
        return (MAX_RANGE - d) / (MAX_RANGE - MIN_RANGE)
    return 1.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True, help='the BFME2 install (its audio.big holds the sounds)')
    ap.add_argument('--out', required=True)
    ap.add_argument('--seed', type=int, default=5)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    big = os.path.join(args.game, 'audio.big')
    entries = big_entries(big)
    pick = lambda prefix: sorted(n for n in entries if n.startswith('data\\audio\\sounds\\' + prefix))
    pools = {'impact': pick('wiimpa')[:40], 'arrow': pick('wiarro')[:20], 'death': pick('cumale_die')}
    cache = {}
    rng = random.Random(args.seed)
    cam, look = mm.default_camera()
    mic, face = mm.microphone(cam, look)
    zoom = mm.zoom_volume(cam, mic)  # RW 0x451946: positional slider volume x 0.8326 at the default camera
    n = int(SECONDS * RATE)
    mixes = {'before': ([0.0] * n, [0.0] * n), 'after': ([0.0] * n, [0.0] * n)}
    t = 0.0
    events = 0
    while t < SECONDS - 1.0:
        kind = rng.choices(['impact', 'arrow', 'death'], [6, 2, 1])[0]
        if t < SECONDS / 2:
            pos = (rng.uniform(700, 1300), rng.uniform(850, 1250), 0.0)
        else:
            pos = (rng.uniform(700, 850), rng.uniform(1150, 1300), 0.0)
        name = rng.choice(pools[kind])
        if name not in cache:
            cache[name] = decode(big, entries[name])
        pcm = cache[name]
        start = int(t * RATE)
        gains = {
            'before': mm.old_gains(falloff((look[0], look[1], 0.0), pos), look, 0.0, pos),
            'after': mm.fast2d_gains(falloff(mic, pos) * zoom, mic, face, pos, 2 * MAX_RANGE),
        }
        for model, (gl, gr) in gains.items():
            L, R = mixes[model]
            for i, s in enumerate(pcm[: n - start]):
                L[start + i] += s * gl
                R[start + i] += s * gr
        events += 1
        t += rng.uniform(0.04, 0.16)
    rms = {}
    loud = {}
    for model, (L, R) in mixes.items():
        peak = max(max(abs(x) for x in L), max(abs(x) for x in R)) or 1.0
        scale = 0.9 / peak  # each render peaks at -0.9 dBFS (the loudness difference is printed, not heard)
        loud[model] = peak
        path = os.path.join(args.out, f'battle_{model}.wav')
        with wave.open(path, 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(RATE)
            frames = array.array('h')
            for a, b in zip(L, R):
                frames.append(max(-32767, min(32767, int(a * scale * 32767))))
                frames.append(max(-32767, min(32767, int(b * scale * 32767))))
            w.writeframes(frames.tobytes())
        win = RATE // 20
        rms[model] = [(math.sqrt(sum(x * x for x in L[i:i + win]) / win) * scale, math.sqrt(sum(x * x for x in R[i:i + win]) / win) * scale) for i in range(0, n - win, win)]
        print(path)
    svg = os.path.join(args.out, 'channel_balance.svg')
    write_svg(svg, rms, events)
    print(svg)
    print(f'peak level after / before: {20 * math.log10(loud["after"] / loud["before"]):+.1f} dB (each WAV is normalised on its own)')
    for model, rows in rms.items():
        rows = [(a / (0.9 / loud[model]), b / (0.9 / loud[model])) for a, b in rows]
        lt = math.sqrt(sum(a * a for a, _ in rows) / len(rows))
        rt = math.sqrt(sum(b * b for _, b in rows) / len(rows))
        half = len(rows) // 2
        l2 = math.sqrt(sum(a * a for a, _ in rows[half:]) / (len(rows) - half))
        r2 = math.sqrt(sum(b * b for _, b in rows[half:]) / (len(rows) - half))
        l1 = math.sqrt(sum(a * a for a, _ in rows[:half]) / half)
        r1 = math.sqrt(sum(b * b for _, b in rows[:half]) / half)
        print(f'{model}: L/R RMS {lt:.4f} / {rt:.4f}; first half (spread) L-R {20 * math.log10(l1 / r1):+.1f} dB, second half (upper left) L-R {20 * math.log10(l2 / r2):+.1f} dB')


def write_svg(path, rms, events):
    W, H, pad = 900, 700, 50
    ph = (H - 4 * pad) / 3
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" font-family="sans-serif" font-size="12">',
           f'<rect width="{W}" height="{H}" fill="white"/>',
           f'<text x="{pad}" y="20" font-size="14">AUDIO-5: L / R RMS (50 ms windows) of a synthetic battle, {events} retail sounds, default tactical camera</text>']
    x = lambda i, n: pad + i * (W - 2 * pad) / max(1, n - 1)
    mid = pad + (W - 2 * pad) / 2
    for k, model in enumerate(('before', 'after')):
        rows = rms[model]
        top = max(max(a, b) for a, b in rows) or 1.0
        y0 = pad + k * (ph + pad)
        out.append(f'<text x="{pad}" y="{y0 - 6}">{"before AUDIO-5 (listener on the ground, forward = camera right, linear Godot pan)" if model == "before" else "after AUDIO-5 (retail microphone and zoom volume, Miles Fast 2D: acos pan, volume^(5/3), x0.75 behind)"}; own scale</text>')
        out.append(f'<rect x="{pad}" y="{y0}" width="{W - 2 * pad}" height="{ph}" fill="none" stroke="#999"/>')
        out.append(f'<line x1="{mid}" y1="{y0}" x2="{mid}" y2="{y0 + ph}" stroke="#ccc" stroke-dasharray="4"/>')
        for ch, colour in ((0, '#1f6fd1'), (1, '#d1491f')):
            pts = ' '.join(f'{x(i, len(rows)):.1f},{y0 + ph - rows[i][ch] / top * ph:.1f}' for i in range(len(rows)))
            out.append(f'<polyline points="{pts}" fill="none" stroke="{colour}" stroke-width="1.2"/>')
    # the balance L - R in dB, clamped to +-24
    y0 = pad + 2 * (ph + pad)
    out.append(f'<text x="{pad}" y="{y0 - 6}">balance 20 log10(L / R), dB (+ = left), clamped to +-24: before grey, after black</text>')
    out.append(f'<rect x="{pad}" y="{y0}" width="{W - 2 * pad}" height="{ph}" fill="none" stroke="#999"/>')
    out.append(f'<line x1="{pad}" y1="{y0 + ph / 2}" x2="{W - pad}" y2="{y0 + ph / 2}" stroke="#ccc"/>')
    out.append(f'<line x1="{mid}" y1="{y0}" x2="{mid}" y2="{y0 + ph}" stroke="#ccc" stroke-dasharray="4"/>')
    for model, colour in (('before', '#999'), ('after', '#000')):
        rows = rms[model]
        db = [max(-24.0, min(24.0, 20 * math.log10(max(a, 1e-9) / max(b, 1e-9)))) for a, b in rows]
        pts = ' '.join(f'{x(i, len(db)):.1f},{y0 + ph / 2 - d / 24.0 * ph / 2:.1f}' for i, d in enumerate(db))
        out.append(f'<polyline points="{pts}" fill="none" stroke="{colour}" stroke-width="1.2"/>')
    out.append(f'<text x="{W - pad + 4}" y="{y0 + 10}">+24</text><text x="{W - pad + 4}" y="{y0 + ph}">-24</text>')
    out.append(f'<text x="{pad}" y="{H - 12}" fill="#1f6fd1">left</text><text x="{pad + 40}" y="{H - 12}" fill="#d1491f">right</text>'
               f'<text x="{pad + 100}" y="{H - 12}">0 .. {SECONDS:.0f} s; dashed: the fight moves from across the screen to its upper left</text>')
    out.append('</svg>')
    with open(path, 'w') as f:
        f.write('\n'.join(out))


if __name__ == '__main__':
    main()
