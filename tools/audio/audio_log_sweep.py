#!/usr/bin/env python3
"""Lane AUDIO-3: reads a sound request log (LiveGameAudio::eventLogHeader columns: OPENBFME_AUDIO_LOG from the game, OPENBFME_AUDIO3_DIAG from the tests)
and lists what repeats implausibly (one object asking for one event more often than --max-per-sec times per game second, or re-asking within --min-gap
frames), the refusals by reason and the events per origin.

usage: audio_log_sweep.py <log.tsv> [--max-per-sec 1.0] [--min-gap 5] [--top 25]
"""
import argparse
import collections
import csv
import sys

LOGIC_FPS = 5.0


def read(path):
    with open(path, newline='') as f:
        r = csv.DictReader(f, delimiter='\t')
        for row in r:
            row['frame'] = int(row['frame'])
            yield row


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--max-per-sec', type=float, default=1.0)
    ap.add_argument('--min-gap', type=int, default=5, help='re-requests closer than this many logic frames count as rapid')
    ap.add_argument('--top', type=int, default=25)
    a = ap.parse_args(argv)
    rows = [r for r in read(a.log)]
    requests = [r for r in rows if r['line'] == 'request']
    plays = [r for r in rows if r['line'] == 'play']
    if not rows:
        print('empty log')
        return 1
    frames = max(r['frame'] for r in rows) - min(r['frame'] for r in rows) + 1
    print('%d request lines, %d play lines, frames %d..%d' % (len(requests), len(plays), min(r['frame'] for r in rows), max(r['frame'] for r in rows)))

    print('\n== requests by origin kind')
    c = collections.Counter(r['origin'].split(' obj ')[0] if r['origin'].startswith('fx ') else r['origin'] for r in requests)
    for k, n in c.most_common(a.top):
        print('%7d  %s' % (n, k))

    print('\n== outcomes')
    c = collections.Counter(r['outcome'].split(' Data')[0] if r['outcome'].startswith(('started', 'failed')) else r['outcome'] for r in rows)
    for k, n in c.most_common(a.top):
        print('%7d  %s' % (n, k))

    # per (object, event): count, the rate over the object's active span, and the rapid re-requests
    per = collections.defaultdict(list)
    for r in requests:
        if r['object'] != '0':
            per[(r['object'], r['event'])].append(r)
    print('\n== repeated requests of one event by one object (rate above %.2f/s or re-asked within %d frames)' % (a.max_per_sec, a.min_gap))
    worst = []
    for (obj, ev), rs in per.items():
        fr = [x['frame'] for x in rs]
        span = (max(fr) - min(fr) + 1) / LOGIC_FPS
        rate = len(rs) / max(span, 1.0)
        rapid = sum(1 for p, q in zip(fr, fr[1:]) if q - p < a.min_gap)
        if len(rs) >= 4 and (rate > a.max_per_sec or rapid >= 3):
            worst.append((len(rs), rate, rapid, obj, ev, rs[0]['template'], collections.Counter(x['origin'] for x in rs).most_common(1)[0][0]))
    worst.sort(reverse=True)
    # summarised by (event, template, origin)
    agg = collections.defaultdict(lambda: [0, 0, 0, 0.0])
    for n, rate, rapid, obj, ev, tt, origin in worst:
        k = (ev, tt, origin)
        agg[k][0] += 1
        agg[k][1] += n
        agg[k][2] += rapid
        agg[k][3] = max(agg[k][3], rate)
    for k, v in sorted(agg.items(), key=lambda kv: -kv[1][1])[:a.top]:
        print('%4d objects %6d requests %6d rapid  max %.2f/s  %s  [%s]  %s' % (v[0], v[1], v[2], v[3], k[0], k[1], k[2]))

    print('\n== events per game second (all objects), top')
    c = collections.Counter(r['event'] for r in requests)
    secs = frames / LOGIC_FPS
    for k, n in c.most_common(a.top):
        print('%7d  %6.2f/s  %s' % (n, n / secs, k))

    print('\n== refusals by event (unknown events and the like)')
    c = collections.Counter((r['event'], r['outcome']) for r in requests if r['outcome'].startswith('refused: unknown') or r['outcome'].startswith('refused: multisound'))
    for k, n in c.most_common(a.top):
        print('%7d  %s  %s' % (n, k[0], k[1]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
