import json,copy
from model import *
POS={1:(570.28,4462.36),2:(2672.81,4581.57),3:(4457.70,4440.85),4:(4602.08,2313.59),5:(4445.58,524.21),6:(2329.84,417.20),7:(539.34,572.59),8:(423.05,2676.61)}
POS={k:(f32(x),f32(y)) for k,(x,y) in POS.items()}
store={'all':list(range(12)),'starts':[3,5,6,7,8,9,10]}
def slot(occ=False,obs=False,tmpl=-1,color=-1,start=-1,team=-1): return dict(occ=occ,obs=obs,tmpl=tmpl,color=color,start=start,team=team)
def mk(spec):
    s=[slot() for _ in range(8)]
    for i,d in spec.items(): s[i]=slot(occ=True,**d)
    return s
scen={
 'two':{0:dict(),1:dict()},
 'four_teams':{0:dict(team=0),1:dict(team=1),2:dict(team=0),3:dict(team=1)},
 'eight':{i:dict(team=i%2) for i in range(8)},
 'mixed':{0:dict(tmpl=5,color=2,start=3),1:dict(),2:dict(tmpl=3,team=1),3:dict(color=2)},
 'observer':{0:dict(),1:dict(),2:dict(obs=True,tmpl=-2)},
}
out=[]
for name,spec in scen.items():
    for seed in (1,2,3,7,1234,4242):
        slots=mk(spec)
        for s in slots:
            if s['obs']: s['tmpl']=-2
        rng=RNG(seed)
        res=resolve(slots,store,10,POS,8,rng)
        out.append(dict(name=name,seed=seed,spec={str(k):v for k,v in spec.items()},slots=[(s['start'],s['tmpl'],s['color']) for s in res if s['occ']],draws=[(a,b,c) for a,b,c in rng.log]))
json.dump(out,open('golden.json','w'))
print(len(out))
for o in out[:3]: print(o['name'],o['seed'],o['slots'],o['draws'][:6])
