import struct, math
def f32(x): return struct.unpack('<f',struct.pack('<f',x))[0]
def bits(x): return struct.unpack('<I',struct.pack('<f',x))[0]
def fl(b): return struct.unpack('<f',struct.pack('<I',b&0xFFFFFFFF))[0]
def mul(a,b): return f32(a*b)   # PC24 mul of floats == float32 product in normal range
def sub(a,b): return f32(a-b)
def invsqrt(x):
    b=bits(x); y0=fl((0xBE6EB508-b)>>1); xh=fl(b-0x800000)
    a=mul(mul(y0,y0),xh); bb=sub(1.5,a); c=mul(mul(a,bb),bb); y0b=mul(y0,bb); d=sub(1.5,c); cd=mul(c,d); y0bd=mul(y0b,d)
    e=sub(1.5,mul(cd,d)); return mul(y0bd,e)
def unitpos(base,off):
    x,y,z=[f32(v) for v in off]
    len2=f32(f32(f32(z*z)+f32(y*y))+f32(x*x))
    nx,ny=x,y
    if len2!=0.0:
        r=invsqrt(len2); nx=mul(x,r); ny=mul(y,r)
    c=invsqrt(2.0); negc=mul(-1.0,c)
    A=f32(math.acos(ny))
    t1=f32(ny*0.0); t2=f32(nx-t1)
    if 0.0>t2: A=f32(A*-1.0)
    s=f32(math.sin(A)); co=f32(math.cos(A))
    X=f32(f32(co*c)-f32(negc*s)); Y=f32(f32(negc*co)+f32(c*s))
    ln=f32(math.sqrt(len2))
    return (f32(f32(ln*X)+base[0]), f32(f32(Y*ln)+base[1]), f32(0.0+base[2]))
if __name__=='__main__':
    for off in [(1,130,0),(30,200,0),(1,160,0),(-60,185,0),(1,230,0),(30,250,0),(100,-50,0)]:
        p=unitpos((1000.0,2000.0,35.5),off)
        print(off,p,[hex(bits(v)) for v in p])
