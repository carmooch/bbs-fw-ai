# Accuracy map with every realistic error source on and the magnet pattern
# learned while cruising. Run from this directory after ./learn.sh.
import subprocess, statistics as st
def est(G,cad,T,cal,seed=42):
    out=subprocess.run(["./ripple_model",str(G),str(cad),str(T),"0.8","0.8","0.3","10","0.056",str(cal),str(seed),"24","96","100","0.1"],capture_output=True,text=True,check=True).stdout.split("\n")
    e=[float(l.split()[1]) for l in out if l.strip()]
    w=[st.mean(e[i:i+4]) for i in range(0,len(e)-3,4)]
    return st.mean(w), st.pstdev(w)
print("Realistic: 0.3 deg magnet errors, road noise 10 N, leg inertia, 10% effort wander, 96 edges, 100 us tick.")
print("Magnet pattern LEARNED while cruising, not known. Mean +/- spread of 4-revolution windows; true 10 / 25 / 40 Nm.\n")
print("gear  cad  km/h |  10 Nm          25 Nm          40 Nm")
for G in (1.0,1.5,2.0,2.5,3.0,3.5):
    for cad in (40,60,80,100):
        v=cad/60*6.2832*0.356*G*3.6
        if v>45 or v<4: continue
        cells=[]
        for T in (10,25,40):
            m,s=est(G,cad,T,2)
            cells.append(f"{m:5.1f} +/-{s:4.1f}")
        print(f"{G:4.1f} {cad:4d} {v:5.1f} | "+"  ".join(cells))
