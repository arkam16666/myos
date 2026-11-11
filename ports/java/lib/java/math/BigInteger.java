package java.math;

import java.util.Random;

/**
 * Arbitrary-precision integers: sign + magnitude in 32-bit words (most
 * significant first, like OpenJDK, so hashCode matches).
 */
public class BigInteger extends Number implements Comparable<BigInteger> {
    final int signum;           /* -1, 0, 1 */
    final int[] mag;            /* no leading zero words; empty for zero */

    public static final BigInteger ZERO = new BigInteger(new int[0], 0);
    public static final BigInteger ONE = valueOf(1);
    public static final BigInteger TWO = valueOf(2);
    public static final BigInteger TEN = valueOf(10);
    public static final BigInteger NEGATIVE_ONE = valueOf(-1);
    private static final long MASK = 0xffffffffL;

    BigInteger(int[] magnitude, int signum) {
        int[] m = trim(magnitude);
        this.mag = m;
        this.signum = m.length == 0 ? 0 : signum;
    }

    public BigInteger(String val) { this(val, 10); }

    public BigInteger(String val, int radix) {
        int len = val.length();
        if (len == 0) throw new NumberFormatException("Zero length BigInteger");
        int sign = 1, i = 0;
        if (val.charAt(0) == '-' || val.charAt(0) == '+') {
            sign = val.charAt(0) == '-' ? -1 : 1;
            i = 1;
            if (len == 1) throw new NumberFormatException("Zero length BigInteger");
        }
        int[] m = new int[0];
        for (; i < len; i++) {
            int d = Character.digit(val.charAt(i), radix);
            if (d < 0) throw new NumberFormatException("For input string: \"" + val + "\"" + (radix == 10 ? "" : " under radix " + radix));
            m = mulAdd(m, radix, d);
        }
        m = trim(m);
        this.mag = m;
        this.signum = m.length == 0 ? 0 : sign;
    }

    public BigInteger(byte[] val) {
        if (val.length == 0) throw new NumberFormatException("Zero length BigInteger");
        boolean neg = val[0] < 0;
        byte[] b = val.clone();
        if (neg) {          /* two's complement -> magnitude */
            for (int i = 0; i < b.length; i++) b[i] = (byte) ~b[i];
            for (int i = b.length - 1; i >= 0; i--) if (++b[i] != 0) break;
        }
        int n = (b.length + 3) / 4;
        int[] m = new int[n];
        for (int i = 0; i < b.length; i++) {
            int bytePos = b.length - 1 - i;
            m[n - 1 - i / 4] |= (b[bytePos] & 0xff) << (8 * (i % 4));
        }
        m = trim(m);
        this.mag = m;
        this.signum = m.length == 0 ? 0 : (neg ? -1 : 1);
    }

    public BigInteger(int numBits, Random rnd) {
        int[] m = new int[(numBits + 31) / 32];
        for (int i = 0; i < m.length; i++) m[i] = rnd.nextInt();
        if (m.length > 0 && numBits % 32 != 0) m[0] &= (1 << (numBits % 32)) - 1;
        m = trim(m);
        this.mag = m;
        this.signum = m.length == 0 ? 0 : 1;
    }

    public static BigInteger valueOf(long val) {
        if (val == 0) return ZERO == null ? new BigInteger(new int[0], 0) : ZERO;
        int sign = val < 0 ? -1 : 1;
        long a = val < 0 ? -val : val;      /* MIN_VALUE stays 2^63 as unsigned */
        int hi = (int) (a >>> 32), lo = (int) a;
        return new BigInteger(hi == 0 ? new int[] { lo } : new int[] { hi, lo }, sign);
    }

    private static int[] trim(int[] m) {
        int k = 0;
        while (k < m.length && m[k] == 0) k++;
        if (k == 0) return m;
        int[] r = new int[m.length - k];
        System.arraycopy(m, k, r, 0, r.length);
        return r;
    }

    private static int[] mulAdd(int[] m, int mul, int add) {
        int[] r = new int[m.length + 1];
        long carry = add & MASK;
        for (int i = m.length - 1; i >= 0; i--) {
            long t = (m[i] & MASK) * (mul & MASK) + carry;
            r[i + 1] = (int) t;
            carry = t >>> 32;
        }
        r[0] = (int) carry;
        return trim(r);
    }

    /* ------------------------------------------------------------ magnitude arithmetic */
    private static int cmpMag(int[] a, int[] b) {
        if (a.length != b.length) return a.length < b.length ? -1 : 1;
        for (int i = 0; i < a.length; i++) {
            if (a[i] != b[i]) return (a[i] & MASK) < (b[i] & MASK) ? -1 : 1;
        }
        return 0;
    }

    private static int[] addMag(int[] a, int[] b) {
        if (a.length < b.length) {
            int[] t = a;
            a = b;
            b = t;
        }
        int[] r = new int[a.length + 1];
        long carry = 0;
        int i = a.length - 1, j = b.length - 1;
        for (; i >= 0; i--, j--) {
            long t = (a[i] & MASK) + (j >= 0 ? b[j] & MASK : 0) + carry;
            r[i + 1] = (int) t;
            carry = t >>> 32;
        }
        r[0] = (int) carry;
        return trim(r);
    }

    private static int[] subMag(int[] a, int[] b) {      /* a >= b */
        int[] r = new int[a.length];
        long borrow = 0;
        int i = a.length - 1, j = b.length - 1;
        for (; i >= 0; i--, j--) {
            long t = (a[i] & MASK) - (j >= 0 ? b[j] & MASK : 0) - borrow;
            r[i] = (int) t;
            borrow = t < 0 ? 1 : 0;
        }
        return trim(r);
    }

    private static int[] mulMag(int[] a, int[] b) {
        if (a.length == 0 || b.length == 0) return new int[0];
        int[] r = new int[a.length + b.length];
        for (int i = a.length - 1; i >= 0; i--) {
            long carry = 0, ai = a[i] & MASK;
            int k = i + b.length;
            for (int j = b.length - 1; j >= 0; j--, k--) {
                long t = ai * (b[j] & MASK) + (r[k] & MASK) + carry;
                r[k] = (int) t;
                carry = t >>> 32;
            }
            r[i] = (int) carry;
        }
        return trim(r);
    }

    /* divide by one word: {quotient, remainder (one word)} */
    private static int[][] divWord(int[] a, int d) {
        long dd = d & MASK, rem = 0;
        int[] q = new int[a.length];
        for (int i = 0; i < a.length; i++) {
            long cur = (rem << 32) | (a[i] & MASK);
            q[i] = (int) Long.divideUnsigned(cur, dd);
            rem = Long.remainderUnsigned(cur, dd);
        }
        return new int[][] { trim(q), new int[] { (int) rem } };
    }

    /* Knuth's algorithm D: {quotient, remainder} of magnitudes */
    private static int[][] divMag(int[] a, int[] b) {
        if (b.length == 0) throw new ArithmeticException("BigInteger divide by zero");
        if (cmpMag(a, b) < 0) return new int[][] { new int[0], a };
        if (b.length == 1) {
            int[][] r = divWord(a, b[0]);
            return new int[][] { r[0], trim(r[1]) };
        }
        int s = Integer.numberOfLeadingZeros(b[0]);
        int[] v = shiftLeftMag(b, s), u = shiftLeftMag(a, s);
        if (u.length == a.length) {         /* make room for one more word */
            int[] t = new int[u.length + 1];
            System.arraycopy(u, 0, t, 1, u.length);
            u = t;
        }
        int n = v.length, m = u.length - n;
        int[] q = new int[m];
        long v1 = v[0] & MASK, v2 = v[1] & MASK;
        for (int j = 0; j < m; j++) {
            long num = ((u[j] & MASK) << 32) | (u[j + 1] & MASK);
            long qhat = Long.divideUnsigned(num, v1), rhat = Long.remainderUnsigned(num, v1);
            while (qhat >= (1L << 32) || Long.compareUnsigned(qhat * v2, (rhat << 32) | (u[j + 2] & MASK)) > 0) {
                qhat--;
                rhat += v1;
                if (rhat >= (1L << 32)) break;
            }
            /* u[j..j+n] -= qhat * v */
            long borrow = 0, carry = 0;
            for (int i = n - 1; i >= 0; i--) {
                long p = qhat * (v[i] & MASK) + carry;
                carry = p >>> 32;
                long t = (u[j + i + 1] & MASK) - (p & MASK) - borrow;
                u[j + i + 1] = (int) t;
                borrow = t < 0 ? 1 : 0;
            }
            long t = (u[j] & MASK) - carry - borrow;
            u[j] = (int) t;
            if (t < 0) {                    /* added back: qhat was one too big */
                qhat--;
                long c = 0;
                for (int i = n - 1; i >= 0; i--) {
                    long sum = (u[j + i + 1] & MASK) + (v[i] & MASK) + c;
                    u[j + i + 1] = (int) sum;
                    c = sum >>> 32;
                }
                u[j] += (int) c;
            }
            q[j] = (int) qhat;
        }
        int[] rem = new int[n];
        System.arraycopy(u, m, rem, 0, n);
        return new int[][] { trim(q), shiftRightMag(trim(rem), s) };
    }

    private static int[] shiftLeftMag(int[] a, int n) {
        if (a.length == 0) return a;
        int words = n >>> 5, bits = n & 31;
        int[] r = new int[a.length + words + 1];
        for (int i = 0; i < a.length; i++) {
            long v = (a[i] & MASK) << bits;
            r[i] |= (int) (v >>> 32);
            r[i + 1] |= (int) v;
        }
        return trim(r);
    }

    private static int[] shiftRightMag(int[] a, int n) {
        int words = n >>> 5, bits = n & 31;
        if (words >= a.length) return new int[0];
        int len = a.length - words;
        int[] r = new int[len];
        for (int i = 0; i < len; i++) {
            long v = (a[i] & MASK) >>> bits;
            if (bits != 0 && i > 0) v |= (a[i - 1] & MASK) << (32 - bits) & MASK;
            r[i] = (int) v;
        }
        return trim(r);
    }

    /* ------------------------------------------------------------ public arithmetic */
    public BigInteger add(BigInteger val) {
        if (val.signum == 0) return this;
        if (signum == 0) return val;
        if (val.signum == signum) return new BigInteger(addMag(mag, val.mag), signum);
        int c = cmpMag(mag, val.mag);
        if (c == 0) return ZERO;
        return c > 0 ? new BigInteger(subMag(mag, val.mag), signum) : new BigInteger(subMag(val.mag, mag), val.signum);
    }

    public BigInteger subtract(BigInteger val) { return add(val.negate()); }

    public BigInteger multiply(BigInteger val) {
        if (signum == 0 || val.signum == 0) return ZERO;
        return new BigInteger(mulMag(mag, val.mag), signum * val.signum);
    }

    public BigInteger divide(BigInteger val) {
        int[][] qr = divMag(mag, val.mag);
        return new BigInteger(qr[0], signum * val.signum);
    }

    public BigInteger remainder(BigInteger val) {
        int[][] qr = divMag(mag, val.mag);
        return new BigInteger(qr[1], signum);
    }

    public BigInteger[] divideAndRemainder(BigInteger val) {
        int[][] qr = divMag(mag, val.mag);
        return new BigInteger[] { new BigInteger(qr[0], signum * val.signum), new BigInteger(qr[1], signum) };
    }

    public BigInteger mod(BigInteger m) {
        if (m.signum <= 0) throw new ArithmeticException("BigInteger: modulus not positive");
        BigInteger r = remainder(m);
        return r.signum < 0 ? r.add(m) : r;
    }

    public BigInteger pow(int exponent) {
        if (exponent < 0) throw new ArithmeticException("Negative exponent");
        BigInteger result = ONE, base = this;
        while (exponent > 0) {
            if ((exponent & 1) != 0) result = result.multiply(base);
            exponent >>= 1;
            if (exponent > 0) base = base.multiply(base);
        }
        return result;
    }

    public BigInteger modPow(BigInteger exponent, BigInteger m) {
        if (m.signum <= 0) throw new ArithmeticException("BigInteger: modulus not positive");
        if (exponent.signum < 0) return modInverse(m).modPow(exponent.negate(), m);
        BigInteger result = ONE.mod(m), base = this.mod(m);
        for (int i = exponent.bitLength() - 1; i >= 0; i--) {
            result = result.multiply(result).mod(m);
            if (exponent.testBit(i)) result = result.multiply(base).mod(m);
        }
        return result;
    }

    public BigInteger modInverse(BigInteger m) {
        if (m.signum <= 0) throw new ArithmeticException("BigInteger: modulus not positive");
        BigInteger a = this.mod(m), b = m, x0 = ONE, x1 = ZERO;
        while (b.signum != 0) {
            BigInteger[] qr = a.divideAndRemainder(b);
            BigInteger t = x0.subtract(qr[0].multiply(x1));
            x0 = x1;
            x1 = t;
            a = b;
            b = qr[1];
        }
        if (!a.equals(ONE)) throw new ArithmeticException("BigInteger not invertible.");
        return x0.mod(m);
    }

    public BigInteger gcd(BigInteger val) {
        BigInteger a = abs(), b = val.abs();
        while (b.signum != 0) {
            BigInteger t = a.remainder(b);
            a = b;
            b = t;
        }
        return a;
    }

    public BigInteger sqrt() {
        if (signum < 0) throw new ArithmeticException("Negative BigInteger");
        if (signum == 0) return ZERO;
        BigInteger x = ONE.shiftLeft((bitLength() + 1) / 2);
        for (;;) {
            BigInteger y = x.add(divide(x)).shiftRight(1);
            if (y.compareTo(x) >= 0) return x;
            x = y;
        }
    }

    public BigInteger abs() { return signum < 0 ? negate() : this; }

    public BigInteger negate() { return new BigInteger(mag, -signum); }

    public int signum() { return signum; }

    public BigInteger min(BigInteger val) { return compareTo(val) <= 0 ? this : val; }

    public BigInteger max(BigInteger val) { return compareTo(val) >= 0 ? this : val; }

    public boolean isProbablePrime(int certainty) {
        BigInteger n = abs();
        if (n.compareTo(TWO) < 0) return false;
        int[] small = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
        for (int p : small) {
            BigInteger bp = valueOf(p);
            if (n.equals(bp)) return true;
            if (n.mod(bp).signum == 0) return false;
        }
        BigInteger d = n.subtract(ONE);
        int r = d.getLowestSetBit();
        d = d.shiftRight(r);
        BigInteger nm1 = n.subtract(ONE);
        outer:
        for (int p : small) {
            BigInteger x = valueOf(p).modPow(d, n);
            if (x.equals(ONE) || x.equals(nm1)) continue;
            for (int i = 1; i < r; i++) {
                x = x.multiply(x).mod(n);
                if (x.equals(nm1)) continue outer;
            }
            return false;
        }
        return true;
    }

    public BigInteger nextProbablePrime() {
        BigInteger c = add(ONE);
        while (!c.isProbablePrime(50)) c = c.add(ONE);
        return c;
    }

    /* ------------------------------------------------------------ bits (two's complement) */
    private int[] twos(int len) {           /* len words, two's complement */
        int[] r = new int[len];
        for (int i = 0; i < mag.length && i < len; i++) r[len - 1 - i] = mag[mag.length - 1 - i];
        if (signum < 0) {
            for (int i = 0; i < len; i++) r[i] = ~r[i];
            for (int i = len - 1; i >= 0; i--) if (++r[i] != 0) break;
        }
        return r;
    }

    private static BigInteger fromTwos(int[] w) {
        if (w.length > 0 && w[0] < 0) {
            int[] m = w.clone();
            for (int i = 0; i < m.length; i++) m[i] = ~m[i];
            for (int i = m.length - 1; i >= 0; i--) if (++m[i] != 0) break;
            return new BigInteger(m, -1);
        }
        return new BigInteger(w, 1);
    }

    private BigInteger bitOp(BigInteger val, int op) {
        int len = Math.max(mag.length, val.mag.length) + 1;
        int[] a = twos(len), b = val.twos(len);
        for (int i = 0; i < len; i++) a[i] = op == 0 ? a[i] & b[i] : op == 1 ? a[i] | b[i] : op == 2 ? a[i] ^ b[i] : a[i] & ~b[i];
        return fromTwos(a);
    }

    public BigInteger and(BigInteger val) { return bitOp(val, 0); }

    public BigInteger or(BigInteger val) { return bitOp(val, 1); }

    public BigInteger xor(BigInteger val) { return bitOp(val, 2); }

    public BigInteger andNot(BigInteger val) { return bitOp(val, 3); }

    public BigInteger not() { return negate().subtract(ONE); }

    public BigInteger shiftLeft(int n) {
        if (n < 0) return shiftRight(-n);
        if (signum == 0) return this;
        return new BigInteger(shiftLeftMag(mag, n), signum);
    }

    public BigInteger shiftRight(int n) {
        if (n < 0) return shiftLeft(-n);
        if (signum >= 0) return new BigInteger(shiftRightMag(mag, n), signum);
        /* floor division for negatives */
        BigInteger q = new BigInteger(shiftRightMag(mag, n), -1);
        if (!q.shiftLeft(n).equals(this)) q = q.subtract(ONE);
        return q;
    }

    public boolean testBit(int n) {
        if (n < 0) throw new ArithmeticException("Negative bit address");
        int[] w = twos(Math.max(mag.length, (n >>> 5) + 1) + 1);
        return (w[w.length - 1 - (n >>> 5)] >>> (n & 31) & 1) != 0;
    }

    public BigInteger setBit(int n) { return or(ONE.shiftLeft(n)); }

    public BigInteger clearBit(int n) { return andNot(ONE.shiftLeft(n)); }

    public BigInteger flipBit(int n) { return xor(ONE.shiftLeft(n)); }

    public int getLowestSetBit() {
        if (signum == 0) return -1;
        int n = 0;
        for (int i = mag.length - 1; i >= 0; i--, n += 32) if (mag[i] != 0) return n + Integer.numberOfTrailingZeros(mag[i]);
        return -1;
    }

    public int bitLength() {
        if (mag.length == 0) return 0;
        int n = (mag.length - 1) * 32 + (32 - Integer.numberOfLeadingZeros(mag[0]));
        if (signum < 0) {           /* a power of two needs one bit less */
            boolean pow2 = Integer.bitCount(mag[0]) == 1;
            for (int i = 1; i < mag.length && pow2; i++) pow2 = mag[i] == 0;
            if (pow2) n--;
        }
        return n;
    }

    public int bitCount() {
        int n = 0;
        for (int w : mag) n += Integer.bitCount(w);
        return n;
    }

    public byte[] toByteArray() {
        int byteLen = bitLength() / 8 + 1;
        int[] w = twos((byteLen + 3) / 4 + 1);
        byte[] b = new byte[byteLen];
        for (int i = 0; i < byteLen; i++) b[byteLen - 1 - i] = (byte) (w[w.length - 1 - i / 4] >>> (8 * (i % 4)));
        return b;
    }

    /* ------------------------------------------------------------ comparison, conversion */
    public int compareTo(BigInteger val) {
        if (signum != val.signum) return signum > val.signum ? 1 : -1;
        int c = cmpMag(mag, val.mag);
        return signum >= 0 ? c : -c;
    }

    public boolean equals(Object x) {
        if (x == this) return true;
        if (!(x instanceof BigInteger)) return false;
        BigInteger o = (BigInteger) x;
        return o.signum == signum && cmpMag(mag, o.mag) == 0;
    }

    public int hashCode() {
        int h = 0;
        for (int w : mag) h = (int) (31 * h + (w & MASK));
        return h * signum;
    }

    public String toString() { return toString(10); }

    public String toString(int radix) {
        if (signum == 0) return "0";
        if (radix < 2 || radix > 36) radix = 10;
        StringBuilder sb = new StringBuilder();
        int[] m = mag;
        int chunk = radix == 10 ? 1000000000 : radix;
        int digits = radix == 10 ? 9 : 1;
        while (m.length > 0) {
            int[][] qr = divWord(m, chunk);
            m = qr[0];
            int rem = qr[1][0];
            String part = Integer.toString(rem, radix);
            sb.insert(0, part);
            if (m.length > 0) for (int i = part.length(); i < digits; i++) sb.insert(0, '0');
        }
        if (signum < 0) sb.insert(0, '-');
        return sb.toString();
    }

    public long longValue() {
        long v = 0;
        for (int i = Math.max(0, mag.length - 2); i < mag.length; i++) v = (v << 32) | (mag[i] & MASK);
        return signum * v;
    }

    public int intValue() { return (int) longValue(); }

    public float floatValue() { return (float) doubleValue(); }

    public double doubleValue() { return Double.parseDouble(toString()); }

    public long longValueExact() {
        if (bitLength() > 63) throw new ArithmeticException("BigInteger out of long range");
        return longValue();
    }

    public int intValueExact() {
        if (bitLength() > 31) throw new ArithmeticException("BigInteger out of int range");
        return intValue();
    }
}
