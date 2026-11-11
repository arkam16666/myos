package java.util;

public interface PrimitiveIterator<T, C> extends Iterator<T> {
    public static interface OfInt extends PrimitiveIterator<Integer, java.util.function.IntConsumer> {
        int nextInt();

        default Integer next() { return nextInt(); }
    }

    public static interface OfLong extends PrimitiveIterator<Long, java.util.function.LongConsumer> {
        long nextLong();

        default Long next() { return nextLong(); }
    }

    public static interface OfDouble extends PrimitiveIterator<Double, java.util.function.DoubleConsumer> {
        double nextDouble();

        default Double next() { return nextDouble(); }
    }
}
