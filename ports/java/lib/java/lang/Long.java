package java.lang;

public final class Long extends Number implements Comparable<Long> {
    public static final long MIN_VALUE = 0x8000000000000000L;
    public static final long MAX_VALUE = 0x7fffffffffffffffL;
    public static final int SIZE = 64;
    public static final int BYTES = 8;
    @SuppressWarnings("unchecked")
    public static final Class<Long> TYPE = (Class<Long>) Class.getPrimitiveClass("long");
    private static final Long[] cache = new Long[256];

    private final long value;

    public Long(long value) { this.value = value; }

    public Long(String s) throws NumberFormatException { this.value = parseLong(s, 10); }

    public static Long valueOf(long l) {
        if (l >= -128 && l <= 127) {
            Long c = cache[(int) l + 128];
            if (c == null) cache[(int) l + 128] = c = new Long(l);
            return c;
        }
        return new Long(l);
    }

    public static Long valueOf(String s) throws NumberFormatException { return valueOf(parseLong(s, 10)); }

    public static Long valueOf(String s, int radix) throws NumberFormatException { return valueOf(parseLong(s, radix)); }

    public static long parseLong(String s) throws NumberFormatException { return parseLong(s, 10); }

    public static long parseLong(String s, int radix) throws NumberFormatException { return parse(s, radix, false); }

    /* shared by Integer/Short/Byte: checks the int range when asInt */
    static long parse(String s, int radix, boolean asInt) {
        if (s == null) throw new NumberFormatException("Cannot parse null string: null");
        if (radix < 2 || radix > 36) throw new NumberFormatException("radix " + radix + " out of range");
        int n = s.length(), i = 0;
        if (n == 0) throw new NumberFormatException("For input string: \"\"" + (radix == 10 ? "" : " under radix " + radix));
        boolean neg = false;
        char first = s.charAt(0);
        if (first == '-' || first == '+') {
            neg = first == '-';
            i = 1;
            if (n == 1) throw NumberFormatException.forInputString(s, radix);
        }
        long limit = asInt ? (neg ? 2147483648L : 2147483647L) : 0;
        long result = 0;
        for (; i < n; i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0) throw NumberFormatException.forInputString(s, radix);
            if (asInt) {
                result = result * radix + d;
                if (result > limit) throw NumberFormatException.forInputString(s, radix);
            } else {
                /* accumulate negatively to reach MIN_VALUE */
                long r = result * radix - d;
                if (result < MIN_VALUE / radix || r > result * radix || r > 0)
                    throw NumberFormatException.forInputString(s, radix);
                result = r;
            }
        }
        if (asInt) return neg ? -result : result;
        if (!neg) {
            if (result == MIN_VALUE) throw NumberFormatException.forInputString(s, radix);
            return -result;
        }
        return result;
    }

    public static long parseUnsignedLong(String s) { return parseUnsignedLong(s, 10); }

    public static long parseUnsignedLong(String s, int radix) {
        long r = 0;
        for (int i = 0; i < s.length(); i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0) throw NumberFormatException.forInputString(s, radix);
            r = r * radix + d;
        }
        return r;
    }

    public static Long decode(String nm) throws NumberFormatException {
        int radix = 10, index = 0;
        boolean neg = false;
        if (nm.isEmpty()) throw new NumberFormatException("Zero length string");
        char c = nm.charAt(0);
        if (c == '-' || c == '+') {
            neg = c == '-';
            index++;
        }
        if (nm.startsWith("0x", index) || nm.startsWith("0X", index)) {
            index += 2;
            radix = 16;
        } else if (nm.startsWith("#", index)) {
            index++;
            radix = 16;
        } else if (nm.startsWith("0", index) && nm.length() > 1 + index) {
            index++;
            radix = 8;
        }
        long v = parseLong(nm.substring(index), radix);
        return valueOf(neg ? -v : v);
    }

    public static String toString(long i) {
        if (i == MIN_VALUE) return "-9223372036854775808";
        char[] buf = new char[20];
        int p = 20;
        boolean neg = i < 0;
        if (neg) i = -i;
        do {
            buf[--p] = (char) ('0' + (int) (i % 10));
            i /= 10;
        } while (i != 0);
        if (neg) buf[--p] = '-';
        return new String(buf, p, 20 - p);
    }

    public static String toString(long i, int radix) {
        if (radix < 2 || radix > 36) radix = 10;
        if (radix == 10) return toString(i);
        char[] buf = new char[65];
        int p = 65;
        boolean neg = i < 0;
        if (!neg) i = -i;
        while (i <= -radix) {
            buf[--p] = Character.forDigit((int) (-(i % radix)), radix);
            i = i / radix;
        }
        buf[--p] = Character.forDigit((int) (-i), radix);
        if (neg) buf[--p] = '-';
        return new String(buf, p, 65 - p);
    }

    static String toUnsigned(long v, int shift) {
        char[] buf = new char[64];
        int p = 64, mask = (1 << shift) - 1;
        do {
            buf[--p] = Character.forDigit((int) (v & mask), 1 << shift);
            v >>>= shift;
        } while (v != 0);
        return new String(buf, p, 64 - p);
    }

    public static String toHexString(long i) { return toUnsigned(i, 4); }

    public static String toOctalString(long i) { return toUnsigned(i, 3); }

    public static String toBinaryString(long i) { return toUnsigned(i, 1); }

    public static String toUnsignedString(long i) {
        if (i >= 0) return toString(i);
        long q = (i >>> 1) / 5;
        long r = i - q * 10;
        return toString(q) + r;
    }

    public String toString() { return toString(value); }

    public int hashCode() { return (int) (value ^ (value >>> 32)); }

    public static int hashCode(long value) { return (int) (value ^ (value >>> 32)); }

    public boolean equals(Object obj) { return obj instanceof Long && ((Long) obj).value == value; }

    public int compareTo(Long other) { return compare(value, other.value); }

    public static int compare(long x, long y) { return x < y ? -1 : (x == y ? 0 : 1); }

    public static int compareUnsigned(long x, long y) { return compare(x + MIN_VALUE, y + MIN_VALUE); }

    public byte byteValue() { return (byte) value; }

    public short shortValue() { return (short) value; }

    public int intValue() { return (int) value; }

    public long longValue() { return value; }

    public float floatValue() { return value; }

    public double doubleValue() { return value; }

    public static int signum(long i) { return (int) ((i >> 63) | (-i >>> 63)); }

    public static int bitCount(long i) { return Integer.bitCount((int) i) + Integer.bitCount((int) (i >>> 32)); }

    public static long reverse(long i) {
        long r = 0;
        for (int k = 0; k < 64; k++) {
            r = (r << 1) | (i & 1);
            i >>>= 1;
        }
        return r;
    }

    public static long highestOneBit(long i) { return i & (MIN_VALUE >>> numberOfLeadingZeros(i)); }

    public static long lowestOneBit(long i) { return i & -i; }

    public static int numberOfLeadingZeros(long i) {
        int hi = (int) (i >>> 32);
        return hi != 0 ? Integer.numberOfLeadingZeros(hi) : 32 + Integer.numberOfLeadingZeros((int) i);
    }

    public static int numberOfTrailingZeros(long i) {
        int lo = (int) i;
        return lo != 0 ? Integer.numberOfTrailingZeros(lo) : 32 + Integer.numberOfTrailingZeros((int) (i >>> 32));
    }

    public static long rotateLeft(long i, int distance) { return (i << distance) | (i >>> -distance); }

    public static long rotateRight(long i, int distance) { return (i >>> distance) | (i << -distance); }

    public static long divideUnsigned(long dividend, long divisor) {
        if (divisor < 0) return compareUnsigned(dividend, divisor) < 0 ? 0 : 1;
        if (dividend >= 0) return dividend / divisor;
        long q = ((dividend >>> 1) / divisor) << 1;
        long r = dividend - q * divisor;
        return q + (compareUnsigned(r, divisor) >= 0 ? 1 : 0);
    }

    public static long remainderUnsigned(long dividend, long divisor) {
        return dividend - divideUnsigned(dividend, divisor) * divisor;
    }

    public static long max(long a, long b) { return Math.max(a, b); }

    public static long min(long a, long b) { return Math.min(a, b); }

    public static long sum(long a, long b) { return a + b; }
}
