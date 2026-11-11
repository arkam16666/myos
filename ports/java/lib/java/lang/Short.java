package java.lang;

public final class Short extends Number implements Comparable<Short> {
    public static final short MIN_VALUE = -32768;
    public static final short MAX_VALUE = 32767;
    public static final int SIZE = 16;
    public static final int BYTES = 2;
    @SuppressWarnings("unchecked")
    public static final Class<Short> TYPE = (Class<Short>) Class.getPrimitiveClass("short");
    private final short value;

    public Short(short value) { this.value = value; }

    public Short(String s) { this.value = parseShort(s); }

    public static short parseShort(String s) { return parseShort(s, 10); }

    public static short parseShort(String s, int radix) {
        int i = Integer.parseInt(s, radix);
        if (i < MIN_VALUE || i > MAX_VALUE) throw new NumberFormatException("Value out of range. Value:\"" + s + "\" Radix:" + radix);
        return (short) i;
    }

    public static Short valueOf(short s) { return new Short(s); }

    public static Short valueOf(String s) { return new Short(parseShort(s)); }

    public static Short valueOf(String s, int radix) { return new Short(parseShort(s, radix)); }

    public static String toString(short s) { return Integer.toString(s); }

    public String toString() { return Integer.toString(value); }

    public int hashCode() { return value; }

    public static int hashCode(short value) { return value; }

    public boolean equals(Object obj) { return obj instanceof Short && ((Short) obj).value == value; }

    public int compareTo(Short other) { return value - other.value; }

    public static int compare(short x, short y) { return x - y; }

    public static int toUnsignedInt(short x) { return x & 0xffff; }

    public static short reverseBytes(short i) { return (short) (((i & 0xFF00) >> 8) | (i << 8)); }

    public byte byteValue() { return (byte) value; }

    public short shortValue() { return value; }

    public int intValue() { return value; }

    public long longValue() { return value; }

    public float floatValue() { return value; }

    public double doubleValue() { return value; }
}
