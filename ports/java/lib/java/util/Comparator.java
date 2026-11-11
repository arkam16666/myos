package java.util;

import java.util.function.Function;
import java.util.function.ToDoubleFunction;
import java.util.function.ToIntFunction;
import java.util.function.ToLongFunction;

public interface Comparator<T> {
    int compare(T o1, T o2);

    boolean equals(Object obj);

    default Comparator<T> reversed() { return Collections.reverseOrder(this); }

    default Comparator<T> thenComparing(final Comparator<? super T> other) {
        final Comparator<T> self = this;
        return new Comparator<T>() {
            public int compare(T a, T b) {
                int r = self.compare(a, b);
                return r != 0 ? r : other.compare(a, b);
            }
        };
    }

    default <U> Comparator<T> thenComparing(Function<? super T, ? extends U> keyExtractor, Comparator<? super U> keyComparator) {
        return thenComparing(comparing(keyExtractor, keyComparator));
    }

    default <U extends Comparable<? super U>> Comparator<T> thenComparing(Function<? super T, ? extends U> keyExtractor) {
        return thenComparing(comparing(keyExtractor));
    }

    default Comparator<T> thenComparingInt(ToIntFunction<? super T> keyExtractor) { return thenComparing(comparingInt(keyExtractor)); }

    default Comparator<T> thenComparingLong(ToLongFunction<? super T> keyExtractor) { return thenComparing(comparingLong(keyExtractor)); }

    default Comparator<T> thenComparingDouble(ToDoubleFunction<? super T> keyExtractor) {
        return thenComparing(comparingDouble(keyExtractor));
    }

    @SuppressWarnings("unchecked")
    public static <T extends Comparable<? super T>> Comparator<T> reverseOrder() { return Collections.<T>reverseOrder(); }

    @SuppressWarnings("unchecked")
    public static <T extends Comparable<? super T>> Comparator<T> naturalOrder() {
        return (Comparator<T>) (Comparator<?>) Collections.NATURAL;
    }

    public static <T> Comparator<T> nullsFirst(final Comparator<? super T> comparator) {
        return new Comparator<T>() {
            public int compare(T a, T b) {
                if (a == null) return b == null ? 0 : -1;
                if (b == null) return 1;
                return comparator == null ? 0 : comparator.compare(a, b);
            }
        };
    }

    public static <T> Comparator<T> nullsLast(final Comparator<? super T> comparator) {
        return new Comparator<T>() {
            public int compare(T a, T b) {
                if (a == null) return b == null ? 0 : 1;
                if (b == null) return -1;
                return comparator == null ? 0 : comparator.compare(a, b);
            }
        };
    }

    public static <T, U> Comparator<T> comparing(final Function<? super T, ? extends U> keyExtractor,
                                                 final Comparator<? super U> keyComparator) {
        return new Comparator<T>() {
            public int compare(T a, T b) { return keyComparator.compare(keyExtractor.apply(a), keyExtractor.apply(b)); }
        };
    }

    public static <T, U extends Comparable<? super U>> Comparator<T> comparing(final Function<? super T, ? extends U> keyExtractor) {
        return new Comparator<T>() {
            public int compare(T a, T b) { return keyExtractor.apply(a).compareTo(keyExtractor.apply(b)); }
        };
    }

    public static <T> Comparator<T> comparingInt(final ToIntFunction<? super T> keyExtractor) {
        return new Comparator<T>() {
            public int compare(T a, T b) { return Integer.compare(keyExtractor.applyAsInt(a), keyExtractor.applyAsInt(b)); }
        };
    }

    public static <T> Comparator<T> comparingLong(final ToLongFunction<? super T> keyExtractor) {
        return new Comparator<T>() {
            public int compare(T a, T b) { return Long.compare(keyExtractor.applyAsLong(a), keyExtractor.applyAsLong(b)); }
        };
    }

    public static <T> Comparator<T> comparingDouble(final ToDoubleFunction<? super T> keyExtractor) {
        return new Comparator<T>() {
            public int compare(T a, T b) { return Double.compare(keyExtractor.applyAsDouble(a), keyExtractor.applyAsDouble(b)); }
        };
    }
}
