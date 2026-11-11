package java.lang;

public final class Byte extends Number implements Comparable<Byte> {
    public static final byte MIN_VALUE = -128;
    public static final byte MAX_VALUE = 127;
    public static final int SIZE = 8;
    public static final int BYTES = 1;
    @SuppressWarnings("unchecked")
    public static final Class<Byte> TYPE = (Class<Byte>) Class.getPrimitiveClass("byte");
    private static final Byte[] cache = new Byte[256];
    private final byte value;

    public Byte(byte value) { this.value = value; }

    public Byte(String s) { this.value = parseByte(s); }

    public static byte parseByte(String s) { return parseByte(s, 10); }

    public static byte parseByte(String s, int radix) {
        int i = Integer.parseInt(s, radix);
        if (i < MIN_VALUE || i > MAX_VALUE) throw new NumberFormatException("Value out of range. Value:\"" + s + "\" Radix:" + radix);
        return (byte) i;
    }

    public static Byte valueOf(byte b) {
        Byte c = cache[b + 128];
        if (c == null) cache[b + 128] = c = new Byte(b);
        return c;
    }

    public static Byte valueOf(String s) { return valueOf(parseByte(s)); }

    public static Byte valueOf(String s, int radix) { return valueOf(parseByte(s, radix)); }

    public static String toString(byte b) { return Integer.toString(b); }

    public String toString() { return Integer.toString(value); }

    public int hashCode() { return value; }

    public static int hashCode(byte value) { return value; }

    public boolean equals(Object obj) { return obj instanceof Byte && ((Byte) obj).value == value; }

    public int compareTo(Byte other) { return value - other.value; }

    public static int compare(byte x, byte y) { return x - y; }

    public static int toUnsignedInt(byte x) { return x & 0xff; }

    public static long toUnsignedLong(byte x) { return x & 0xffL; }

    public byte byteValue() { return value; }

    public short shortValue() { return value; }

    public int intValue() { return value; }

    public long longValue() { return value; }

    public float floatValue() { return value; }

    public double doubleValue() { return value; }
}
