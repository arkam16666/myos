package java.util;

import java.util.function.Supplier;

public final class Objects {
    private Objects() {}

    public static boolean equals(Object a, Object b) { return (a == b) || (a != null && a.equals(b)); }

    public static boolean deepEquals(Object a, Object b) {
        if (a == b) return true;
        if (a == null || b == null) return false;
        if (a instanceof Object[] && b instanceof Object[]) return Arrays.deepEquals((Object[]) a, (Object[]) b);
        if (a instanceof int[] && b instanceof int[]) return Arrays.equals((int[]) a, (int[]) b);
        if (a instanceof long[] && b instanceof long[]) return Arrays.equals((long[]) a, (long[]) b);
        if (a instanceof char[] && b instanceof char[]) return Arrays.equals((char[]) a, (char[]) b);
        if (a instanceof double[] && b instanceof double[]) return Arrays.equals((double[]) a, (double[]) b);
        if (a instanceof byte[] && b instanceof byte[]) return Arrays.equals((byte[]) a, (byte[]) b);
        if (a instanceof boolean[] && b instanceof boolean[]) return Arrays.equals((boolean[]) a, (boolean[]) b);
        return a.equals(b);
    }

    public static int hashCode(Object o) { return o != null ? o.hashCode() : 0; }

    public static int hash(Object... values) { return Arrays.hashCode(values); }

    public static String toString(Object o) { return String.valueOf(o); }

    public static String toString(Object o, String nullDefault) { return (o != null) ? o.toString() : nullDefault; }

    public static <T> int compare(T a, T b, Comparator<? super T> c) { return (a == b) ? 0 : c.compare(a, b); }

    public static <T> T requireNonNull(T obj) {
        if (obj == null) throw new NullPointerException();
        return obj;
    }

    public static <T> T requireNonNull(T obj, String message) {
        if (obj == null) throw new NullPointerException(message);
        return obj;
    }

    public static <T> T requireNonNull(T obj, Supplier<String> messageSupplier) {
        if (obj == null) throw new NullPointerException(messageSupplier == null ? null : messageSupplier.get());
        return obj;
    }

    public static <T> T requireNonNullElse(T obj, T defaultObj) {
        return (obj != null) ? obj : requireNonNull(defaultObj, "defaultObj");
    }

    public static <T> T requireNonNullElseGet(T obj, Supplier<? extends T> supplier) {
        return (obj != null) ? obj : requireNonNull(requireNonNull(supplier, "supplier").get(), "supplier.get()");
    }

    public static boolean isNull(Object obj) { return obj == null; }

    public static boolean nonNull(Object obj) { return obj != null; }

    public static int checkIndex(int index, int length) {
        if (index < 0 || index >= length) throw new IndexOutOfBoundsException("Index " + index + " out of bounds for length " + length);
        return index;
    }

    public static int checkFromToIndex(int fromIndex, int toIndex, int length) {
        if (fromIndex < 0 || fromIndex > toIndex || toIndex > length)
            throw new IndexOutOfBoundsException("Range [" + fromIndex + ", " + toIndex + ") out of bounds for length " + length);
        return fromIndex;
    }

    public static int checkFromIndexSize(int fromIndex, int size, int length) {
        if ((length | fromIndex | size) < 0 || size > length - fromIndex)
            throw new IndexOutOfBoundsException("Range [" + fromIndex + ", " + fromIndex + " + " + size + ") out of bounds for length " + length);
        return fromIndex;
    }
}
