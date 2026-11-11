package java.lang;

public final class Math {
    public static final double E = 2.718281828459045;
    public static final double PI = 3.141592653589793;
    public static final double TAU = 2.0 * PI;
    private static java.util.Random random;

    private Math() {}

    public static native double sin(double a);
    public static native double cos(double a);
    public static native double tan(double a);
    public static native double asin(double a);
    public static native double acos(double a);
    public static native double atan(double a);
    public static native double exp(double a);
    public static native double log(double a);
    public static native double log10(double a);
    public static native double sqrt(double a);
    public static native double cbrt(double a);
    public static native double sinh(double x);
    public static native double cosh(double x);
    public static native double tanh(double x);
    public static native double expm1(double x);
    public static native double log1p(double x);
    public static native double floor(double a);
    public static native double ceil(double a);
    public static native double rint(double a);
    public static native double atan2(double y, double x);
    public static native double pow(double a, double b);
    public static native double hypot(double x, double y);

    public static double toRadians(double angdeg) { return angdeg * (PI / 180.0); }

    public static double toDegrees(double angrad) { return angrad * (180.0 / PI); }

    public static double IEEEremainder(double f1, double f2) { return f1 - f2 * rint(f1 / f2); }

    public static long round(double a) {
        if (a != a) return 0;
        double f = floor(a);
        if (a - f >= 0.5) f += 1;
        return (long) f;
    }

    public static int round(float a) {
        if (a != a) return 0;
        double f = floor(a);
        if (a - f >= 0.5) f += 1;
        return (int) f;
    }

    public static double random() {
        if (random == null) random = new java.util.Random();
        return random.nextDouble();
    }

    public static int abs(int a) { return a < 0 ? -a : a; }

    public static long abs(long a) { return a < 0 ? -a : a; }

    public static float abs(float a) { return Float.intBitsToFloat(Float.floatToRawIntBits(a) & 0x7fffffff); }

    public static double abs(double a) { return Double.longBitsToDouble(Double.doubleToRawLongBits(a) & 0x7fffffffffffffffL); }

    public static int absExact(int a) {
        if (a == Integer.MIN_VALUE) throw new ArithmeticException("Overflow to represent absolute value of Integer.MIN_VALUE");
        return abs(a);
    }

    public static long absExact(long a) {
        if (a == Long.MIN_VALUE) throw new ArithmeticException("Overflow to represent absolute value of Long.MIN_VALUE");
        return abs(a);
    }

    public static int max(int a, int b) { return a >= b ? a : b; }

    public static long max(long a, long b) { return a >= b ? a : b; }

    public static float max(float a, float b) {
        if (a != a) return a;
        if (a == 0.0f && b == 0.0f && Float.floatToRawIntBits(a) != 0) return b;
        return a >= b ? a : b;
    }

    public static double max(double a, double b) {
        if (a != a) return a;
        if (a == 0.0d && b == 0.0d && Double.doubleToRawLongBits(a) != 0) return b;
        return a >= b ? a : b;
    }

    public static int min(int a, int b) { return a <= b ? a : b; }

    public static long min(long a, long b) { return a <= b ? a : b; }

    public static float min(float a, float b) {
        if (a != a) return a;
        if (a == 0.0f && b == 0.0f && Float.floatToRawIntBits(b) != 0) return b;
        return a <= b ? a : b;
    }

    public static double min(double a, double b) {
        if (a != a) return a;
        if (a == 0.0d && b == 0.0d && Double.doubleToRawLongBits(b) != 0) return b;
        return a <= b ? a : b;
    }

    public static int clamp(long value, int min, int max) { return (int) Math.min(max, Math.max(value, min)); }

    public static long clamp(long value, long min, long max) { return Math.min(max, Math.max(value, min)); }

    public static double clamp(double value, double min, double max) { return Math.min(max, Math.max(value, min)); }

    public static float clamp(float value, float min, float max) { return Math.min(max, Math.max(value, min)); }

    public static double signum(double d) { return (d == 0.0 || d != d) ? d : (d > 0 ? 1.0 : -1.0); }

    public static float signum(float f) { return (f == 0.0f || f != f) ? f : (f > 0 ? 1.0f : -1.0f); }

    public static double copySign(double magnitude, double sign) {
        long s = Double.doubleToRawLongBits(sign) & 0x8000000000000000L;
        return Double.longBitsToDouble((Double.doubleToRawLongBits(magnitude) & 0x7fffffffffffffffL) | s);
    }

    public static double ulp(double d) {
        if (d != d) return d;
        d = abs(d);
        if (d == Double.POSITIVE_INFINITY) return d;
        if (d == Double.MAX_VALUE) return Math.pow(2, 971);
        return Double.longBitsToDouble(Double.doubleToRawLongBits(d) + 1) - d;
    }

    public static double nextUp(double d) {
        if (d != d || d == Double.POSITIVE_INFINITY) return d;
        d += 0.0d;
        return Double.longBitsToDouble(Double.doubleToRawLongBits(d) + ((d >= 0.0d) ? +1L : -1L));
    }

    public static double nextDown(double d) {
        if (d != d || d == Double.NEGATIVE_INFINITY) return d;
        if (d == 0.0) return -Double.MIN_VALUE;
        return Double.longBitsToDouble(Double.doubleToRawLongBits(d) + ((d > 0.0d) ? -1L : +1L));
    }

    public static double log2(double a) { return log(a) / log(2); }

    public static double fma(double a, double b, double c) { return a * b + c; }

    public static int addExact(int x, int y) {
        int r = x + y;
        if (((x ^ r) & (y ^ r)) < 0) throw new ArithmeticException("integer overflow");
        return r;
    }

    public static long addExact(long x, long y) {
        long r = x + y;
        if (((x ^ r) & (y ^ r)) < 0) throw new ArithmeticException("long overflow");
        return r;
    }

    public static int subtractExact(int x, int y) {
        int r = x - y;
        if (((x ^ y) & (x ^ r)) < 0) throw new ArithmeticException("integer overflow");
        return r;
    }

    public static long subtractExact(long x, long y) {
        long r = x - y;
        if (((x ^ y) & (x ^ r)) < 0) throw new ArithmeticException("long overflow");
        return r;
    }

    public static int multiplyExact(int x, int y) {
        long r = (long) x * (long) y;
        if ((int) r != r) throw new ArithmeticException("integer overflow");
        return (int) r;
    }

    public static long multiplyExact(long x, int y) { return multiplyExact(x, (long) y); }

    public static long multiplyExact(long x, long y) {
        long r = x * y;
        long ax = abs(x), ay = abs(y);
        if (((ax | ay) >>> 31 != 0)) {
            if (((y != 0) && (r / y != x)) || (x == Long.MIN_VALUE && y == -1)) throw new ArithmeticException("long overflow");
        }
        return r;
    }

    public static int incrementExact(int a) {
        if (a == Integer.MAX_VALUE) throw new ArithmeticException("integer overflow");
        return a + 1;
    }

    public static long incrementExact(long a) {
        if (a == Long.MAX_VALUE) throw new ArithmeticException("long overflow");
        return a + 1L;
    }

    public static int decrementExact(int a) {
        if (a == Integer.MIN_VALUE) throw new ArithmeticException("integer overflow");
        return a - 1;
    }

    public static long decrementExact(long a) {
        if (a == Long.MIN_VALUE) throw new ArithmeticException("long overflow");
        return a - 1L;
    }

    public static int negateExact(int a) {
        if (a == Integer.MIN_VALUE) throw new ArithmeticException("integer overflow");
        return -a;
    }

    public static long negateExact(long a) {
        if (a == Long.MIN_VALUE) throw new ArithmeticException("long overflow");
        return -a;
    }

    public static int toIntExact(long value) {
        if ((int) value != value) throw new ArithmeticException("integer overflow");
        return (int) value;
    }

    public static int floorDiv(int x, int y) {
        int q = x / y;
        if ((x ^ y) < 0 && (q * y != x)) q--;
        return q;
    }

    public static long floorDiv(long x, int y) { return floorDiv(x, (long) y); }

    public static long floorDiv(long x, long y) {
        long q = x / y;
        if ((x ^ y) < 0 && (q * y != x)) q--;
        return q;
    }

    public static int floorMod(int x, int y) {
        int m = x % y;
        if ((m ^ y) < 0 && m != 0) m += y;
        return m;
    }

    public static int floorMod(long x, int y) { return (int) floorMod(x, (long) y); }

    public static long floorMod(long x, long y) {
        long m = x % y;
        if ((m ^ y) < 0 && m != 0) m += y;
        return m;
    }

    public static int ceilDiv(int x, int y) { return -floorDiv(-x, y); }
}
