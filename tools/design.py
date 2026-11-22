"""
Superalloy — design.py
Derives the composite transformer's parasitics and magnetising inductance:
  * leakage L and winding C: Butterworth-damped into RL per transformer, scaled so the
    COPPER cascade first reaches -3 dB at 15 MHz (skin exaggeration off)
  * L_m: copper cascade -3 dB at 0.04 Hz
and prints each metal's analog response (dB re 1 kHz). Requires numpy, scipy.
"""
import numpy as np
from scipy.special import jv
from scipy.optimize import brentq
mu0=4e-7*np.pi
rho_cu=16.78e-9
M={"Superconductor":0.0,"Silver7N":15.87e-9,"Copper6N":16.78e-9,"Gold":22.14e-9,"Rhodium":43e-9,"Iridium":49e-9,
"PtIr10":0.25e-6,"Monel400":0.53e-6,"Haynes188":1.01e-6,"MP35N":1.03e-6,"Waspaloy":1.22e-6,"Inconel718":1.24e-6,
"HastC276":1.26e-6,"Inconel625":1.29e-6,"Rene41":1.29e-6}
Rp0,Rs0=9.2,9.5; RL=1e4
r_w=1.024e-3/2  # AWG18
def zn(f,fk):
    if fk<=0 or fk>250e3 or f==0: return 1.0+0j
    x=np.sqrt(f/fk)
    if x>30: return x/2*(1+1j)+0.25
    z=x*(1-1j)
    return (z/2)*jv(0,z)/jv(1,z)
SKIN=[True]
def H(f,rho,Lm,Ll,C):
    w=2*np.pi*f; s=1j*w
    k=rho/rho_cu
    fk = rho/(np.pi*mu0*r_w**2) if (rho>0 and SKIN[0]) else 0
    Rp=max(Rp0*k,1e-6)*zn(f,fk); Rs=max(Rs0*k,1e-6)*zn(f,fk)
    # backward ladder: compute input impedance and transfer
    # stage2
    Zb2=1/(s*C+1/RL)
    Za2_load=s*Ll+Rs+Zb2
    Za2=1/(1/(s*Lm)+1/Za2_load)
    Zb1_load=Rp+Za2
    Zb1=1/(s*C+1/Zb1_load)
    Za1_load=s*Ll+Rs+Zb1
    Za1=1/(1/(s*Lm)+1/Za1_load)
    vA1=Za1/(Rp+Za1)
    vB1=vA1*Zb1/(s*Ll+Rs+Zb1)
    vA2=vB1*Za2/(Rp+Za2)
    vB2=vA2*Zb2/(s*Ll+Rs+Zb2)
    return vB2
def db(x): return 20*np.log10(abs(x))
# HF: per-transformer Butterworth ratio sqrt(L/C)=sqrt2*RL, scale so cascade -3dB=15MHz (copper)
Z0=np.sqrt(2)*RL
def f3hf(scale):
    Ll=Z0*scale; C=scale/Z0
    g1=db(H(1e3,rho_cu,1e9,Ll,C))
    return brentq(lambda f: db(H(f,rho_cu,1e9,Ll,C))-g1+3,1e4,1e13)
SKIN[0]=False
sc=brentq(lambda s: f3hf(s)-15e6,1e-11,1e-7)
SKIN[0]=True
Ll=Z0*sc; C=sc/Z0
print("Ll=%.4g H C=%.4g F"%(Ll,C), "f3=",f3hf(sc))
def f3lf(Lm):
    g1=db(H(1e3,rho_cu,Lm,Ll,C))
    return brentq(lambda f: db(H(f,rho_cu,Lm,Ll,C))-g1+3,1e-5,10)
Lm=brentq(lambda L: f3lf(L)-0.04,1,1e5)
print("Lm=%.4g H"%Lm)
fs=[10,20,40,100,1000,10000,20000,40000,80000,176400]
print("metal       IL1k   "+" ".join("%8s"%f for f in fs))
for n,r in M.items():
    g1=H(1e3,r,Lm,Ll,C)
    row=[db(H(f,r,Lm,Ll,C)/g1) for f in fs]
    print("%-12s %6.3f "%(n,db(g1))+" ".join("%8.4f"%v for v in row))
print("phase copper 20Hz deg", np.angle(H(20,rho_cu,Lm,Ll,C)/H(1e3,rho_cu,Lm,Ll,C),deg=True))
print("fk copper", rho_cu/(np.pi*mu0*r_w**2))
