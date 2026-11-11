#!/usr/bin/env python3
"""ed25519 - Ed25519 signatures (RFC 8032) in plain Python, for signing the
MyOS package list. Slow but small; signing one index takes a fraction of a
second. MyOS checks the signatures with Monocypher's crypto_ed25519_check.

    import ed25519
    pub = ed25519.public_key(seed32)
    sig = ed25519.sign(seed32, message)
    ed25519.verify(pub, message, sig) -> True / False
"""
import hashlib

P = 2 ** 255 - 19
L = 2 ** 252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def _inv(x):
    return pow(x, P - 2, P)


def _recover_x(y, sign):
    if y >= P:
        return None
    x2 = (y * y - 1) * _inv(D * y * y + 1) % P
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P:
        return None
    if (x & 1) != sign:
        x = P - x
    return x


_BY = 4 * _inv(5) % P
_BX = _recover_x(_BY, 0)
_B = (_BX, _BY, 1, _BX * _BY % P)          # base point, extended coordinates


def _add(p, q):
    a = (p[1] - p[0]) * (q[1] - q[0]) % P
    b = (p[1] + p[0]) * (q[1] + q[0]) % P
    c = 2 * p[3] * q[3] * D % P
    d = 2 * p[2] * q[2] % P
    e, f, g, h = b - a, d - c, d + c, b + a
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _mul(s, p):
    q = (0, 1, 1, 0)
    while s:
        if s & 1:
            q = _add(q, p)
        p = _add(p, p)
        s >>= 1
    return q


def _equal(p, q):
    return (p[0] * q[2] - q[0] * p[2]) % P == 0 and (p[1] * q[2] - q[1] * p[2]) % P == 0


def _compress(p):
    zi = _inv(p[2])
    x, y = p[0] * zi % P, p[1] * zi % P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _decompress(s):
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % P)


def _expand(seed):
    if len(seed) != 32:
        raise ValueError("the secret key (seed) must be 32 bytes")
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(seed):
    a, _ = _expand(seed)
    return _compress(_mul(a, _B))


def sign(seed, msg):
    a, prefix = _expand(seed)
    pub = _compress(_mul(a, _B))
    r = int.from_bytes(hashlib.sha512(prefix + msg).digest(), "little") % L
    big_r = _compress(_mul(r, _B))
    h = int.from_bytes(hashlib.sha512(big_r + pub + msg).digest(), "little") % L
    s = (r + h * a) % L
    return big_r + int.to_bytes(s, 32, "little")


def verify(pub, msg, sig):
    if len(pub) != 32 or len(sig) != 64:
        return False
    a = _decompress(pub)
    r = _decompress(sig[:32])
    s = int.from_bytes(sig[32:], "little")
    if a is None or r is None or s >= L:
        return False
    h = int.from_bytes(hashlib.sha512(sig[:32] + pub + msg).digest(), "little") % L
    return _equal(_mul(s, _B), _add(r, _mul(h, a)))


def _self_test():
    """RFC 8032 section 7.1, test 1 (empty message) and test 2."""
    seed = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
    pub = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
    sig = bytes.fromhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b")
    assert public_key(seed) == pub and sign(seed, b"") == sig and verify(pub, b"", sig)
    seed = bytes.fromhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb")
    pub = bytes.fromhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c")
    sig = bytes.fromhex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                        "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00")
    assert public_key(seed) == pub and sign(seed, b"\x72") == sig and verify(pub, b"\x72", sig)
    assert not verify(pub, b"\x73", sig)


if __name__ == "__main__":
    _self_test()
    print("ed25519: RFC 8032 test vectors OK")
