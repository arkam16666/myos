package java.lang;

public final class Integer extends Number implements Comparable<Integer> {
    public static final int MIN_VALUE = 0x80000000;
    public static final int MAX_VALUE = 0x7fffffff;
    public static final int SIZE = 32;
    public static final int BYTES = 4;
    @SuppressWarnings("unchecked")
    public static final Class<Integer> TYPE = (Class<Integer>) Class.getPrimitiveClass("int");
    private static final Integer[] cache = new Integer[256];

    private final int value;

    public Integer(int value) { this.value = value; }

    public Integer(String s) throws NumberFormatException { this.value = parseInt(s, 10); }

    public static Integer valueOf(int i) {
        if (i >= -128 && i <= 127) {
            Integer c = cache[i + 128];
            if (c == null) cache[i + 128] = c = new Integer(i);
            return c;
        }
        return new Integer(i);
    }

    public static Integer valueOf(String s) throws NumberFormatException { return valueOf(parseInt(s, 10)); }

    public static Integer valueOf(String s, int radix) throws NumberFormatException { return valueOf(parseInt(s, radix)); }

    public static int parseInt(String s) throws NumberFormatException { return parseInt(s, 10); }

    public static int parseInt(String s, int radix) throws NumberFormatException {
        long v = Long.parse(s, radix, true);
        return (int) v;
    }

    public static int parseInt(CharSequence s, int beginIndex, int endIndex, int radix) {
        return parseInt(s.subSequence(beginIndex, endIndex).toString(), radix);
    }

    public static int parseUnsignedInt(String s) { return parseUnsignedInt(s, 10); }

    public static int parseUnsignedInt(String s, int radix) {
        long v = Long.parseLong(s, radix);
        if (v < 0 || v > 0xffffffffL) throw NumberFormatException.forInputString(s, radix);
        return (int) v;
    }

    public static Integer decode(String nm) throws NumberFormatException { return valueOf((int) Long.decode(nm).longValue()); }

    public static String toString(int i) {
        if (i == MIN_VALUE) return "-2147483648";
        char[] buf = new char[11];
        int p = 11;
        boolean neg = i < 0;
        if (neg) i = -i;
        do {
            buf[--p] = (char) ('0' + i % 10);
            i /= 10;
        } while (i != 0);
        if (neg) buf[--p] = '-';
        return new String(buf, p, 11 - p);
    }

    public static String toString(int i, int radix) { return Long.toString(i, radix); }

    public static String toUnsignedString(int i) { return Long.toString(i & 0xffffffffL); }

    public static String toUnsignedString(int i, int radix) { return Long.toString(i & 0xffffffffL, radix); }

    public static String toHexString(int i) { return Long.toUnsigned(i & 0xffffffffL, 4); }

    public static String toOctalString(int i) { return Long.toUnsigned(i & 0xffffffffL, 3); }

    public static String toBinaryString(int i) { return Long.toUnsigned(i & 0xffffffffL, 1); }

    public static long toUnsignedLong(int x) { return x & 0xffffffffL; }

    public static int divideUnsigned(int a, int b) { return (int) ((a & 0xffffffffL) / (b & 0xffffffffL)); }

    public static int remainderUnsigned(int a, int b) { return (int) ((a & 0xffffffffL) % (b & 0xffffffffL)); }

    public static int compareUnsigned(int x, int y) { return compare(x + MIN_VALUE, y + MIN_VALUE); }

    public String toString() { return toString(value); }

    public int hashCode() { return value; }

    public static int hashCode(int value) { return value; }

    public boolean equals(Object obj) { return obj instanceof Integer && ((Integer) obj).value == value; }

    public int compareTo(Integer other) { return compare(value, other.value); }

    public static int compare(int x, int y) { return x < y ? -1 : (x == y ? 0 : 1); }

    public byte byteValue() { return (byte) value; }

    public short shortValue() { return (short) value; }

    public int intValue() { return value; }

    public long longValue() { return value; }

    public float floatValue() { return value; }

    public double doubleValue() { return value; }

    public static int signum(int i) { return (i >> 31) | (-i >>> 31); }

    public static int bitCount(int i) {
        i = i - ((i >>> 1) & 0x55555555);
        i = (i & 0x33333333) + ((i >>> 2) & 0x33333333);
        i = (i + (i >>> 4)) & 0x0f0f0f0f;
        i = i + (i >>> 8);
        i = i + (i >>> 16);
        return i & 0x3f;
    }

    public static int reverse(int i) {
        int r = 0;
        for (int k = 0; k < 32; k++) {
            r = (r << 1) | (i & 1);
            i >>>= 1;
        }
        return r;
    }

    public static int reverseBytes(int i) {
        return (i << 24) | ((i & 0xff00) << 8) | ((i >>> 8) & 0xff00) | (i >>> 24);
    }

    public static int highestOneBit(int i) { return i & (MIN_VALUE >>> numberOfLeadingZeros(i)); }

    public static int lowestOneBit(int i) { return i & -i; }

    public static int numberOfLeadingZeros(int i) {
        if (i == 0) return 32;
        int n = 0;
        while (i > 0) {
            n++;
            i <<= 1;
        }
        return n;
    }

    public static int numberOfTrailingZeros(int i) {
        if (i == 0) return 32;
        int n = 0;
        while ((i & 1) == 0) {
            n++;
            i >>>= 1;
        }
        return n;
    }

    public static int rotateLeft(int i, int distance) { return (i << distance) | (i >>> -distance); }

    public static int rotateRight(int i, int distance) { return (i >>> distance) | (i << -distance); }

    public static int max(int a, int b) { return Math.max(a, b); }

    public static int min(int a, int b) { return Math.min(a, b); }

    public static int sum(int a, int b) { return a + b; }

    public static Integer getInteger(String nm) { return null; }
}
