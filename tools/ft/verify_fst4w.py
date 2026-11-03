# Verifica las palabras de codigo del codificador FST4W (tools/ft/test_fst4w.cpp) contra la matriz de paridad ldpc_240_74_parity.f90 de WSJT-X,
# el resto del CRC24 y el empaquetado del indicativo (independiente). Edita W (carpeta lib/fst4 de WSJT-X 3.3.0) y ejecuta en la carpeta de t_fst4w.
import re,subprocess,sys
W="/ruta/a/wsjtx-3.3.0/lib/fst4/"
txt=open(W+'ldpc_240_74_parity.f90').read()
mn=re.search(r'data Mn/(.*?)/',txt,re.S).group(1)
nums=[int(x) for x in re.findall(r'\d+',mn)]
assert len(nums)==720,len(nums)
checks=[[] for _ in range(166)]
for n in range(240):
    for k in range(3):
        checks[nums[3*n+k]-1].append(n)
def crc24(bits):
    p=[1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,1,1,0,1,1]
    r=bits[:25]
    for i in range(len(bits)-24):
        r[24]=bits[i+24]
        if r[0]: r=[(a+b)%2 for a,b in zip(r,p)]
        r=r[1:]+r[:1]
    return r[:24]
def n28(call):
    # standard callsign packing (FT8): 6 chars, 3rd char digit
    c=call.upper()
    if not c[2].isdigit(): c=' '+c
    c=c.ljust(6)
    A=' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'; B='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'; C='0123456789'; D=' ABCDEFGHIJKLMNOPQRSTUVWXYZ'
    n=A.index(c[0]);n=n*36+B.index(c[1]);n=n*10+C.index(c[2]);n=n*27+D.index(c[3]);n=n*27+D.index(c[4]);n=n*27+D.index(c[5])
    return 2063592+4194304+n
ok=True
for call,grid,dbm in [('K1ABC','FN42',37),('EA7JRS','IM77',10),('G4ABC','IO91',0),('W9XYZ','EN34',30),('EA7JRS','IM76',17),('PA3XYZ','JO22',3)]:
    out=subprocess.check_output(['./t_fst4w',call,grid,str(dbm)]).decode().split()
    cw=[int(x) for x in out[0]]; tones=out[1]
    syn=sum(sum(cw[i] for i in ch)%2 for ch in checks)
    ig=(ord(grid[0])-65)*1800+(ord(grid[1])-65)*100+int(grid[2])*10+int(grid[3])
    idbm=int(0.3*dbm+0.5)
    exp=[int(b) for b in format(n28(call),'028b')+format(ig,'015b')+format(idbm,'05b')+'00']
    exp+= [0]*24
    exp[50:74]=crc24(exp[:74])
    ok&= (syn==0) and cw[:74]==exp and crc24(cw[:74])==[0]*24
    print(call,grid,dbm,'parity-fail',syn,'payload',cw[:74]==exp,'crc-resid0',crc24(cw[:74])==[0]*24)
print('ALL OK' if ok else 'FAIL')
