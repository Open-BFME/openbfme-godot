"""tools/start/model.py: independent model of the retail skirmish random resolution (RotWK 0x62D7EA / 0x62D44C) with the ZH carry-chain generator.
Written from the disassembly notes, not from the C++; used to produce the golden vectors of test_start_random.cpp."""
import struct, math, json, sys
M=0xFFFFFFFF
INIT=[0xf22d0e56,0x883126e9,0xc624dd2f,0x0702c49c,0x9e353f7d,0x6fdf3b64]
def f32(x): return struct.unpack('<f',struct.pack('<f',x))[0]
class RNG:
    def __init__(s,seed):
        s.base=seed&M
        s.a=[(seed+i)&M for i in INIT]
        s.log=[]
    def raw(s):
        a=s.a; c=0
        def adc(x,y):
            nonlocal c
            t=x+y+c
            c=1 if t>M else 0
            return t&M
        ax=adc(a[5],a[4]); a[4]=ax
        ax=adc(ax,a[3]); a[3]=ax
        ax=adc(ax,a[2]); a[2]=ax
        ax=adc(ax,a[1]); a[1]=ax
        ax=adc(ax,a[0]); a[0]=ax
        # increment bubbling carries
        i=5
        while i>=0:
            a[i]=(a[i]+1)&M
            if a[i]!=0: break
            if i==0: ax=(ax+1)&M
            i-=1
        return ax
    def get(s,lo,hi):
        d=(hi-lo+1)&M
        if d==0: r=hi
        else: r=((s.raw()%d)+lo)&M
        if r>=1<<31: r-=1<<32
        s.log.append((lo,hi,r))
        return r
def dist(p1,p2):
    dy=f32(p1[1]-p2[1]); dx=f32(p1[0]-p2[0])
    sdy=f32(dy*dy); sdx=f32(dx*dx)
    return f32(math.sqrt(f32(sdy+sdx)))
def resolve(slots, playable, ncolors, pos, nplayers, rng):
    """slots: list of 8 dicts: occ, obs, tmpl, color, start, team. pos: dict n(1-based)->(x,y)"""
    n=nplayers
    D=[[0.0]*8 for _ in range(8)]
    for i in range(8):
        for j in range(8):
            if i!=j and i<n and j<n:
                a=pos.get(i+1); b=pos.get(j+1)
                D[i][j]=1000000.0 if (a is None or b is None) else dist(a,b)
    taken=[i>=n for i in range(8)]; owner=[-1]*8; picked=False
    for i,s in enumerate(slots):
        if s['occ'] and not s['obs'] and 0<=s['start']<n:
            picked=True; taken[s['start']]=True; owner[s['start']]=i
    def startTaken(p): return any(t['start']==p for t in slots)
    for i,s in enumerate(slots):
        if not s['occ'] or s['obs']: continue
        if 0<=s['start']<n: continue
        hit=False
        if picked:
            best=-1; bv=0.0
            for c in range(n):
                if taken[c]: continue
                if best<0:
                    best=c
                    for k in range(n):
                        if taken[k] and k!=c: bv=f32(D[c][k]+bv)
                else:
                    sm=0.0
                    for k in range(n):
                        if not taken[k] or k==c: continue
                        if s['team']>-1 and slots[owner[k]]['team']==s['team']:
                            hit=True
                            if bv>D[c][k]: bv=D[c][k]; best=c
                        elif not hit:
                            sm=f32(D[c][k]+sm)
                            if sm>bv: bv=sm; best=c
            s['start']=best; taken[best]=True; owner[best]=i
        else:
            p=-1
            while p==-1:
                p=rng.get(0,n-1)
                if startTaken(p): p=-1
            s['start']=p; taken[p]=True; owner[p]=i; picked=True
    ing=sum(1 for s in slots if s['occ'] and not s['obs'])
    for i,s in enumerate(slots):
        if not (s['occ'] and s['obs']): continue
        p=0
        if ing:
            while True:
                p=rng.get(0,n-1)
                if startTaken(p): break
        s['start']=p
    # side and colour
    for i,s in enumerate(slots):
        if not s['occ']: continue
        while s['tmpl']!=-2 and not (0<=s['tmpl']<len(playable['all'])):
            for _ in range(rng.base%7): rng.get(0,1)
            c=playable['starts']
            r=rng.get(0,1000)
            s['tmpl']=c[r%len(c)]
        col=s['color']
        if not (0<=col<ncolors):
            col=-1
            while col==-1:
                col=rng.get(0,ncolors-1)
                if any(t['color']==col for t in slots): col=-1
            s['color']=col
    return slots
if __name__=='__main__':
    pass
