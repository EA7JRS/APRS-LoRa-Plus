#!/usr/bin/env python3
"""Codigos de desbloqueo del rango de emision de la T-TWR (include/tx_unlock.h): firma ECDSA P-256 / SHA-256 de
"APRS-LoRa-Plus TX unlock <MAC>". Python puro (sin librerias externas).

  tx_unlock_code.py genkey <archivo_privado>      crea la clave (guarda la privada en el archivo, NUNCA en el repositorio) y
                                                  escribe la publica para include/tx_unlock_key.h
  tx_unlock_code.py sign <archivo_privado> <MAC>  codigo para ese equipo (la MAC la muestran la WebUI y la app)
  tx_unlock_code.py verify <publica_hex> <MAC> <codigo>

Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later.
"""
import hashlib
import re
import secrets
import sys

P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
G = (0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
     0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5)


def add(p, q):
    if p is None:
        return q
    if q is None:
        return p
    if p[0] == q[0] and (p[1] + q[1]) % P == 0:
        return None
    if p == q:
        l = (3 * p[0] * p[0] + A) * pow(2 * p[1], -1, P) % P
    else:
        l = (q[1] - p[1]) * pow(q[0] - p[0], -1, P) % P
    x = (l * l - p[0] - q[0]) % P
    return (x, (l * (p[0] - x) - p[1]) % P)


def mul(k, p):
    r = None
    while k:
        if k & 1:
            r = add(r, p)
        p = add(p, p)
        k >>= 1
    return r


def message(mac):
    mac = re.sub(r"[^0-9A-Fa-f]", "", mac).upper()
    if len(mac) != 12:
        raise ValueError("the MAC must have 12 hexadecimal digits")
    return ("APRS-LoRa-Plus TX unlock " + mac).encode()


def sign(d, mac):
    z = int.from_bytes(hashlib.sha256(message(mac)).digest(), "big")
    while True:
        k = secrets.randbelow(N - 1) + 1
        r = mul(k, G)[0] % N
        if r == 0:
            continue
        s = pow(k, -1, N) * (z + r * d) % N
        if s:
            return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def verify(pub, mac, sig):
    r, s = int.from_bytes(sig[:32], "big"), int.from_bytes(sig[32:], "big")
    if not (0 < r < N and 0 < s < N):
        return False
    z = int.from_bytes(hashlib.sha256(message(mac)).digest(), "big")
    w = pow(s, -1, N)
    x = add(mul(z * w % N, G), mul(r * w % N, pub))
    return x is not None and x[0] % N == r


def pub_hex(q):
    return "04" + q[0].to_bytes(32, "big").hex() + q[1].to_bytes(32, "big").hex()


def parse_pub(h):
    b = bytes.fromhex(re.sub(r"[^0-9A-Fa-f]", "", h))
    if len(b) != 65 or b[0] != 4:
        raise ValueError("public key: 65 bytes starting with 04")
    return (int.from_bytes(b[1:33], "big"), int.from_bytes(b[33:], "big"))


def group(h):
    return " ".join(h[i:i + 8] for i in range(0, len(h), 8))


def main(a):
    if len(a) == 2 and a[0] == "genkey":
        d = secrets.randbelow(N - 1) + 1
        with open(a[1], "x") as f:
            f.write("%064x\n" % d)
        print('#define TX_UNLOCK_PUBKEY "%s"' % pub_hex(mul(d, G)))
        return 0
    if len(a) == 3 and a[0] == "sign":
        d = int(open(a[1]).read().strip(), 16)
        sig = sign(d, a[2])
        assert verify(mul(d, G), a[2], sig)
        print(group(sig.hex().upper()))
        return 0
    if len(a) == 4 and a[0] == "verify":
        sig = bytes.fromhex(re.sub(r"[^0-9A-Fa-f]", "", a[3]))
        ok = len(sig) == 64 and verify(parse_pub(a[1]), a[2], sig)
        print("OK" if ok else "BAD")
        return 0 if ok else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
