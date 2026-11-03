# Demodulador no coherente por simbolo para los WAV de test_ft_sim (FST4W): python3 demod_fst4w.py f120.wav 8200  (21504 para -300)
import sys, numpy as np, wave
w=wave.open(sys.argv[1]); fs=w.getframerate(); x=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(float)/32768
tones=[int(c) for c in open(sys.argv[1]+'.tones').read().strip()]
nsps=int(sys.argv[2]); sp=fs/nsps; f0=1500-1.5*sp; t0=1.0
errs=0; snr=[]
for i,t in enumerate(tones):
    a=int(round((t0+i*nsps/fs)*fs)); seg=x[a:a+nsps]
    n=np.arange(len(seg))
    e=[abs(np.sum(seg*np.exp(-2j*np.pi*(f0+k*sp)*n/fs)))**2 for k in range(4)]
    if int(np.argmax(e))!=t: errs+=1
print(sys.argv[1],'simbolos con error:',errs,'de',len(tones))
