import numpy as np
from scipy.special import jv
from scipy.optimize import least_squares
def zn(y):
    x=np.sqrt(y); z=x*(1-1j); return (z/2)*jv(0,z)/jv(1,z)
ys=np.logspace(-2,np.log10(25),400)
Z=np.array([zn(y) for y in ys])
K=4
def model(p,y):
    s=1j*y; out=np.ones_like(s)
    for k in range(K):
        r=np.exp(p[2*k]); l=np.exp(p[2*k+1])
        out=out+r*s*l/(r+s*l)
    return out
def res(p):
    m=model(p,ys); e=(m-Z)/np.abs(Z)
    return np.concatenate([e.real,e.imag])
best=None
for trial in range(40):
    rng=np.random.default_rng(trial)
    p0=np.log(np.concatenate([[rng.uniform(0.05,3),rng.uniform(0.01,0.3)] for _ in range(K)]))
    r=least_squares(res,p0,max_nfev=20000)
    if best is None or r.cost<best.cost: best=r
p=best.x; m=model(p,ys); err=np.abs(m-Z)/np.abs(Z)
print("max rel err",err.max(), "max err Re",np.max(np.abs(m.real-Z.real)/Z.real))
for k in range(K): print("r%d=%.10g l%d=%.10g"%(k,np.exp(p[2*k]),k,np.exp(p[2*k+1])))
print("sum l",sum(np.exp(p[1::2])))
for y in [0.1,0.5,1,1.23,2,5,10.9,20]:
    print(y, zn(y), model(p,np.array([y]))[0])
