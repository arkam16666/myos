package java.util;

import java.util.function.IntFunction;
import java.util.function.IntUnaryOperator;

public class Arrays {
    private Arrays() {}

    private static void rangeCheck(int length, int from, int to) {
        if (from > to) throw new IllegalArgumentException("fromIndex(" + from + ") > toIndex(" + to + ")");
        if (from < 0) throw new ArrayIndexOutOfBoundsException(from);
        if (to > length) throw new ArrayIndexOutOfBoundsException(to);
    }

    private static native void sort0(int[] a, int from, int to);
    private static native void sort0(long[] a, int from, int to);
    private static native void sort0(short[] a, int from, int to);
    private static native void sort0(char[] a, int from, int to);
    private static native void sort0(byte[] a, int from, int to);
    private static native void sort0(float[] a, int from, int to);
    private static native void sort0(double[] a, int from, int to);

    public static void sort(int[] a) { sort0(a, 0, a.length); }
    public static void sort(int[] a, int from, int to) { rangeCheck(a.length, from, to); sort0(a, from, to); }
    public static void sort(long[] a) { sort0(a, 0, a.length); }
    public static void sort(long[] a, int from, int to) { rangeCheck(a.length, from, to); sort0(a, from, to); }
    public static void sort(short[] a) { sort0(a, 0, a.length); }
    public static void sort(short[] a, int from, int to) { rangeCheck(a.length, from, to); sort0(a, from, to); }
    public static void sort(char[] a) { sort0(a, 0, a.length); }
    public static void sort(char[] a, int from, int to) { rangeCheck(a.length, from, to); sort0(a, from, to); }
    public static void sort(byte[] a) { sort0(a, 0, a.length); }
    public static void sort(byte[] a, int from, int to) { rangeCheck(a.length, from, to); sort0(a, from, to); }
    public static void sort(float[] a) { sort0(a, 0, a.length); }
    public static void sort(float[] a, int from, int to) { rangeCheck(a.length, from, to); sort0(a, from, to); }
    public static void sort(double[] a) { sort0(a, 0, a.length); }
    public static void sort(double[] a, int from, int to) { rangeCheck(a.length, from, to); sort0(a, from, to); }
    public static void parallelSort(int[] a) { sort(a); }
    public static void parallelSort(long[] a) { sort(a); }
    public static void parallelSort(double[] a) { sort(a); }

    public static void sort(Object[] a) { sort(a, 0, a.length, null); }

    public static void sort(Object[] a, int fromIndex, int toIndex) { sort(a, fromIndex, toIndex, null); }

    public static <T> void sort(T[] a, Comparator<? super T> c) { sort(a, 0, a.length, c); }

    public static <T extends Comparable<? super T>> void parallelSort(T[] a) { sort(a); }

    @SuppressWarnings("unchecked")
    public static <T> void sort(T[] a, int fromIndex, int toIndex, Comparator<? super T> c) {
        rangeCheck(a.length, fromIndex, toIndex);
        Comparator<Object> cmp = c == null ? Collections.NATURAL : (Comparator<Object>) c;
        Object[] aux = new Object[toIndex - fromIndex];
        mergeSort(a, aux, fromIndex, toIndex, cmp);
    }

    /* stable merge sort, like TimSort's results */
    private static void mergeSort(Object[] a, Object[] aux, int lo, int hi, Comparator<Object> c) {
        int n = hi - lo;
        if (n < 12) {
            for (int i = lo + 1; i < hi; i++) {
                Object x = a[i];
                int j = i - 1;
                while (j >= lo && c.compare(a[j], x) > 0) {
                    a[j + 1] = a[j];
                    j--;
                }
                a[j + 1] = x;
            }
            return;
        }
        int mid = (lo + hi) >>> 1;
        mergeSort(a, aux, lo, mid, c);
        mergeSort(a, aux, mid, hi, c);
        if (c.compare(a[mid - 1], a[mid]) <= 0) return;
        int base = 0;
        System.arraycopy(a, lo, aux, base, mid - lo);
        int i = 0, iend = mid - lo, j = mid, k = lo;
        while (i < iend && j < hi) a[k++] = c.compare(a[j], aux[i]) < 0 ? a[j++] : aux[i++];
        while (i < iend) a[k++] = aux[i++];
    }

    public static int binarySearch(int[] a, int key) { return binarySearch(a, 0, a.length, key); }

    public static int binarySearch(int[] a, int fromIndex, int toIndex, int key) {
        int low = fromIndex, high = toIndex - 1;
        while (low <= high) {
            int mid = (low + high) >>> 1;
            if (a[mid] < key) low = mid + 1;
            else if (a[mid] > key) high = mid - 1;
            else return mid;
        }
        return -(low + 1);
    }

    public static int binarySearch(long[] a, long key) {
        int low = 0, high = a.length - 1;
        while (low <= high) {
            int mid = (low + high) >>> 1;
            if (a[mid] < key) low = mid + 1;
            else if (a[mid] > key) high = mid - 1;
            else return mid;
        }
        return -(low + 1);
    }

    public static int binarySearch(double[] a, double key) {
        int low = 0, high = a.length - 1;
        while (low <= high) {
            int mid = (low + high) >>> 1;
            int c = Double.compare(a[mid], key);
            if (c < 0) low = mid + 1;
            else if (c > 0) high = mid - 1;
            else return mid;
        }
        return -(low + 1);
    }

    public static int binarySearch(char[] a, char key) {
        int low = 0, high = a.length - 1;
        while (low <= high) {
            int mid = (low + high) >>> 1;
            if (a[mid] < key) low = mid + 1;
            else if (a[mid] > key) high = mid - 1;
            else return mid;
        }
        return -(low + 1);
    }

    public static int binarySearch(Object[] a, Object key) { return binarySearch(a, key, null); }

    @SuppressWarnings("unchecked")
    public static <T> int binarySearch(T[] a, T key, Comparator<? super T> c) {
        Comparator<Object> cmp = c == null ? Collections.NATURAL : (Comparator<Object>) c;
        int low = 0, high = a.length - 1;
        while (low <= high) {
            int mid = (low + high) >>> 1;
            int r = cmp.compare(a[mid], key);
            if (r < 0) low = mid + 1;
            else if (r > 0) high = mid - 1;
            else return mid;
        }
        return -(low + 1);
    }

    public static boolean equals(int[] a, int[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }

    public static boolean equals(long[] a, long[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }

    public static boolean equals(short[] a, short[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }

    public static boolean equals(char[] a, char[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }

    public static boolean equals(byte[] a, byte[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }

    public static boolean equals(boolean[] a, boolean[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }

    public static boolean equals(double[] a, double[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (Double.doubleToLongBits(a[i]) != Double.doubleToLongBits(b[i])) return false;
        return true;
    }

    public static boolean equals(float[] a, float[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (Float.floatToIntBits(a[i]) != Float.floatToIntBits(b[i])) return false;
        return true;
    }

    public static boolean equals(Object[] a, Object[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (!Objects.equals(a[i], b[i])) return false;
        return true;
    }

    public static boolean deepEquals(Object[] a1, Object[] a2) {
        if (a1 == a2) return true;
        if (a1 == null || a2 == null || a1.length != a2.length) return false;
        for (int i = 0; i < a1.length; i++) if (!Objects.deepEquals(a1[i], a2[i])) return false;
        return true;
    }

    public static void fill(int[] a, int val) { for (int i = 0; i < a.length; i++) a[i] = val; }
    public static void fill(int[] a, int from, int to, int val) { rangeCheck(a.length, from, to); for (int i = from; i < to; i++) a[i] = val; }
    public static void fill(long[] a, long val) { for (int i = 0; i < a.length; i++) a[i] = val; }
    public static void fill(long[] a, int from, int to, long val) { rangeCheck(a.length, from, to); for (int i = from; i < to; i++) a[i] = val; }
    public static void fill(short[] a, short val) { for (int i = 0; i < a.length; i++) a[i] = val; }
    public static void fill(char[] a, char val) { for (int i = 0; i < a.length; i++) a[i] = val; }
    public static void fill(char[] a, int from, int to, char val) { rangeCheck(a.length, from, to); for (int i = from; i < to; i++) a[i] = val; }
    public static void fill(byte[] a, byte val) { for (int i = 0; i < a.length; i++) a[i] = val; }
    public static void fill(boolean[] a, boolean val) { for (int i = 0; i < a.length; i++) a[i] = val; }
    public static void fill(boolean[] a, int from, int to, boolean val) { rangeCheck(a.length, from, to); for (int i = from; i < to; i++) a[i] = val; }
    public static void fill(double[] a, double val) { for (int i = 0; i < a.length; i++) a[i] = val; }
    public static void fill(double[] a, int from, int to, double val) { rangeCheck(a.length, from, to); for (int i = from; i < to; i++) a[i] = val; }
    public static void fill(float[] a, float val) { for (int i = 0; i < a.length; i++) a[i] = val; }
    public static void fill(Object[] a, Object val) { for (int i = 0; i < a.length; i++) a[i] = val; }
    public static void fill(Object[] a, int from, int to, Object val) { rangeCheck(a.length, from, to); for (int i = from; i < to; i++) a[i] = val; }

    public static void setAll(int[] array, IntUnaryOperator generator) {
        for (int i = 0; i < array.length; i++) array[i] = generator.applyAsInt(i);
    }

    public static <T> void setAll(T[] array, IntFunction<? extends T> generator) {
        for (int i = 0; i < array.length; i++) array[i] = generator.apply(i);
    }

    public static void setAll(long[] array, java.util.function.IntToLongFunction generator) {
        for (int i = 0; i < array.length; i++) array[i] = generator.applyAsLong(i);
    }

    public static void setAll(double[] array, java.util.function.IntToDoubleFunction generator) {
        for (int i = 0; i < array.length; i++) array[i] = generator.applyAsDouble(i);
    }

    @SuppressWarnings("unchecked")
    public static <T> T[] copyOf(T[] original, int newLength) { return (T[]) copyOf(original, newLength, original.getClass()); }

    @SuppressWarnings("unchecked")
    public static <T, U> T[] copyOf(U[] original, int newLength, Class<? extends T[]> newType) {
        T[] copy = (T[]) java.lang.reflect.Array.newInstance(newType.getComponentType(), newLength);
        System.arraycopy(original, 0, copy, 0, Math.min(original.length, newLength));
        return copy;
    }

    public static int[] copyOf(int[] o, int n) { int[] c = new int[n]; System.arraycopy(o, 0, c, 0, Math.min(o.length, n)); return c; }
    public static long[] copyOf(long[] o, int n) { long[] c = new long[n]; System.arraycopy(o, 0, c, 0, Math.min(o.length, n)); return c; }
    public static short[] copyOf(short[] o, int n) { short[] c = new short[n]; System.arraycopy(o, 0, c, 0, Math.min(o.length, n)); return c; }
    public static char[] copyOf(char[] o, int n) { char[] c = new char[n]; System.arraycopy(o, 0, c, 0, Math.min(o.length, n)); return c; }
    public static byte[] copyOf(byte[] o, int n) { byte[] c = new byte[n]; System.arraycopy(o, 0, c, 0, Math.min(o.length, n)); return c; }
    public static boolean[] copyOf(boolean[] o, int n) { boolean[] c = new boolean[n]; System.arraycopy(o, 0, c, 0, Math.min(o.length, n)); return c; }
    public static double[] copyOf(double[] o, int n) { double[] c = new double[n]; System.arraycopy(o, 0, c, 0, Math.min(o.length, n)); return c; }
    public static float[] copyOf(float[] o, int n) { float[] c = new float[n]; System.arraycopy(o, 0, c, 0, Math.min(o.length, n)); return c; }

    private static int rangeLen(int length, int from, int to) {
        if (from > to) throw new IllegalArgumentException(from + " > " + to);
        if (from < 0 || from > length) throw new ArrayIndexOutOfBoundsException("Array index out of range: " + from);
        return to - from;
    }

    @SuppressWarnings("unchecked")
    public static <T> T[] copyOfRange(T[] o, int from, int to) {
        int n = rangeLen(o.length, from, to);
        T[] c = (T[]) java.lang.reflect.Array.newInstance(o.getClass().getComponentType(), n);
        System.arraycopy(o, from, c, 0, Math.min(o.length - from, n));
        return c;
    }

    public static int[] copyOfRange(int[] o, int from, int to) { int n = rangeLen(o.length, from, to); int[] c = new int[n]; System.arraycopy(o, from, c, 0, Math.min(o.length - from, n)); return c; }
    public static long[] copyOfRange(long[] o, int from, int to) { int n = rangeLen(o.length, from, to); long[] c = new long[n]; System.arraycopy(o, from, c, 0, Math.min(o.length - from, n)); return c; }
    public static char[] copyOfRange(char[] o, int from, int to) { int n = rangeLen(o.length, from, to); char[] c = new char[n]; System.arraycopy(o, from, c, 0, Math.min(o.length - from, n)); return c; }
    public static byte[] copyOfRange(byte[] o, int from, int to) { int n = rangeLen(o.length, from, to); byte[] c = new byte[n]; System.arraycopy(o, from, c, 0, Math.min(o.length - from, n)); return c; }
    public static boolean[] copyOfRange(boolean[] o, int from, int to) { int n = rangeLen(o.length, from, to); boolean[] c = new boolean[n]; System.arraycopy(o, from, c, 0, Math.min(o.length - from, n)); return c; }
    public static double[] copyOfRange(double[] o, int from, int to) { int n = rangeLen(o.length, from, to); double[] c = new double[n]; System.arraycopy(o, from, c, 0, Math.min(o.length - from, n)); return c; }
    public static float[] copyOfRange(float[] o, int from, int to) { int n = rangeLen(o.length, from, to); float[] c = new float[n]; System.arraycopy(o, from, c, 0, Math.min(o.length - from, n)); return c; }
    public static short[] copyOfRange(short[] o, int from, int to) { int n = rangeLen(o.length, from, to); short[] c = new short[n]; System.arraycopy(o, from, c, 0, Math.min(o.length - from, n)); return c; }

    @SafeVarargs
    public static <T> List<T> asList(T... a) { return new ArrayAsList<T>(a); }

    private static class ArrayAsList<E> extends AbstractList<E> implements RandomAccess, java.io.Serializable {
        private final E[] a;

        ArrayAsList(E[] array) {
            if (array == null) throw new NullPointerException();
            a = array;
        }

        public int size() { return a.length; }

        public Object[] toArray() { return Arrays.copyOf(a, a.length, Object[].class); }

        public E get(int index) { return a[index]; }

        public E set(int index, E element) {
            E old = a[index];
            a[index] = element;
            return old;
        }

        public int indexOf(Object o) {
            for (int i = 0; i < a.length; i++) if (Objects.equals(o, a[i])) return i;
            return -1;
        }

        public boolean contains(Object o) { return indexOf(o) >= 0; }

        public void sort(Comparator<? super E> c) { Arrays.sort(a, c); }
    }

    public static int hashCode(int[] a) {
        if (a == null) return 0;
        int r = 1;
        for (int e : a) r = 31 * r + e;
        return r;
    }

    public static int hashCode(long[] a) {
        if (a == null) return 0;
        int r = 1;
        for (long e : a) r = 31 * r + Long.hashCode(e);
        return r;
    }

    public static int hashCode(char[] a) {
        if (a == null) return 0;
        int r = 1;
        for (char e : a) r = 31 * r + e;
        return r;
    }

    public static int hashCode(byte[] a) {
        if (a == null) return 0;
        int r = 1;
        for (byte e : a) r = 31 * r + e;
        return r;
    }

    public static int hashCode(boolean[] a) {
        if (a == null) return 0;
        int r = 1;
        for (boolean e : a) r = 31 * r + (e ? 1231 : 1237);
        return r;
    }

    public static int hashCode(double[] a) {
        if (a == null) return 0;
        int r = 1;
        for (double e : a) r = 31 * r + Double.hashCode(e);
        return r;
    }

    public static int hashCode(Object[] a) {
        if (a == null) return 0;
        int r = 1;
        for (Object e : a) r = 31 * r + (e == null ? 0 : e.hashCode());
        return r;
    }

    public static int deepHashCode(Object[] a) {
        if (a == null) return 0;
        int r = 1;
        for (Object e : a) {
            int h;
            if (e instanceof Object[]) h = deepHashCode((Object[]) e);
            else if (e instanceof int[]) h = hashCode((int[]) e);
            else if (e instanceof long[]) h = hashCode((long[]) e);
            else if (e instanceof char[]) h = hashCode((char[]) e);
            else if (e instanceof double[]) h = hashCode((double[]) e);
            else h = e == null ? 0 : e.hashCode();
            r = 31 * r + h;
        }
        return r;
    }

    public static String toString(int[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) b.append(i == 0 ? "" : ", ").append(a[i]);
        return b.append(']').toString();
    }

    public static String toString(long[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) b.append(i == 0 ? "" : ", ").append(a[i]);
        return b.append(']').toString();
    }

    public static String toString(short[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) b.append(i == 0 ? "" : ", ").append(a[i]);
        return b.append(']').toString();
    }

    public static String toString(char[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) b.append(i == 0 ? "" : ", ").append(a[i]);
        return b.append(']').toString();
    }

    public static String toString(byte[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) b.append(i == 0 ? "" : ", ").append(a[i]);
        return b.append(']').toString();
    }

    public static String toString(boolean[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) b.append(i == 0 ? "" : ", ").append(a[i]);
        return b.append(']').toString();
    }

    public static String toString(float[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) b.append(i == 0 ? "" : ", ").append(a[i]);
        return b.append(']').toString();
    }

    public static String toString(double[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) b.append(i == 0 ? "" : ", ").append(a[i]);
        return b.append(']').toString();
    }

    public static String toString(Object[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) b.append(i == 0 ? "" : ", ").append(String.valueOf(a[i]));
        return b.append(']').toString();
    }

    public static String deepToString(Object[] a) {
        if (a == null) return "null";
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) {
            if (i > 0) b.append(", ");
            Object e = a[i];
            if (e instanceof Object[]) b.append(e == a ? "[...]" : deepToString((Object[]) e));
            else if (e instanceof int[]) b.append(toString((int[]) e));
            else if (e instanceof long[]) b.append(toString((long[]) e));
            else if (e instanceof char[]) b.append(toString((char[]) e));
            else if (e instanceof double[]) b.append(toString((double[]) e));
            else if (e instanceof boolean[]) b.append(toString((boolean[]) e));
            else if (e instanceof byte[]) b.append(toString((byte[]) e));
            else if (e instanceof float[]) b.append(toString((float[]) e));
            else if (e instanceof short[]) b.append(toString((short[]) e));
            else b.append(String.valueOf(e));
        }
        return b.append(']').toString();
    }

    public static java.util.stream.IntStream stream(int[] array) { return java.util.stream.IntStream.of(array); }

    public static java.util.stream.IntStream stream(int[] array, int from, int to) { return java.util.stream.IntStream.of(copyOfRange(array, from, to)); }

    public static java.util.stream.LongStream stream(long[] array) { return java.util.stream.LongStream.of(array); }

    public static java.util.stream.DoubleStream stream(double[] array) { return java.util.stream.DoubleStream.of(array); }

    public static <T> java.util.stream.Stream<T> stream(T[] array) { return java.util.stream.Stream.of(array); }

    public static <T> java.util.stream.Stream<T> stream(T[] array, int from, int to) { return java.util.stream.Stream.of(copyOfRange(array, from, to)); }
}
