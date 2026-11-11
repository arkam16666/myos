# primes - prime numbers
#   primes 100          all primes up to 100
#   primes is 97        is 97 prime? (shows its factors if not)
# @version 1.0
# @description Prime numbers: list, test, factorize
import sys
import time


def sieve(n):
    flags = bytearray(b"\x01" * (n + 1))
    flags[0] = flags[1] = 0
    i = 2
    while i * i <= n:
        if flags[i]:
            for j in range(i * i, n + 1, i):
                flags[j] = 0
        i += 1
    return [i for i in range(n + 1) if flags[i]]


def factors(n):
    out, d = [], 2
    while d * d <= n:
        while n % d == 0:
            out.append(d)
            n //= d
        d += 1 if d == 2 else 2
    if n > 1:
        out.append(n)
    return out


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "is" and args[1].isdigit():
        n = int(args[1])
        f = factors(n) if n > 1 else []
        if len(f) == 1:
            print(n, "is prime.")
        else:
            print(n, "is not prime." + ("  %d = %s" % (n, " x ".join(map(str, f))) if f else ""))
        return
    n = int(args[0]) if args and args[0].isdigit() else 1000
    n = min(n, 200000)
    t = time.ticks_ms()
    ps = sieve(n)
    ms = time.ticks_diff(time.ticks_ms(), t)
    if len(ps) <= 400:
        for i in range(0, len(ps), 12):
            print(" ".join("%6d" % p for p in ps[i:i + 12]))
    else:
        print("first:", " ".join(map(str, ps[:10])), "...")
        print("last: ", " ".join(map(str, ps[-5:])))
    print("%d primes up to %d (found in %d ms)" % (len(ps), n, ms))


main()
