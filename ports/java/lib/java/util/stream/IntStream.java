package java.util.stream;

import java.util.*;
import java.util.function.*;

public interface IntStream extends BaseStream<Integer, IntStream> {
    PrimitiveIterator.OfInt iterator();

    IntStream filter(IntPredicate predicate);

    IntStream map(IntUnaryOperator mapper);

    <U> Stream<U> mapToObj(IntFunction<? extends U> mapper);

    LongStream mapToLong(IntToLongFunction mapper);

    DoubleStream mapToDouble(IntToDoubleFunction mapper);

    IntStream flatMap(IntFunction<? extends IntStream> mapper);

    IntStream distinct();

    IntStream sorted();

    IntStream peek(IntConsumer action);

    IntStream limit(long maxSize);

    IntStream skip(long n);

    IntStream takeWhile(IntPredicate predicate);

    IntStream dropWhile(IntPredicate predicate);

    void forEach(IntConsumer action);

    void forEachOrdered(IntConsumer action);

    int[] toArray();

    int reduce(int identity, IntBinaryOperator op);

    OptionalInt reduce(IntBinaryOperator op);

    <R> R collect(Supplier<R> supplier, ObjIntConsumer<R> accumulator, BiConsumer<R, R> combiner);

    int sum();

    OptionalInt min();

    OptionalInt max();

    long count();

    OptionalDouble average();

    IntSummaryStatistics summaryStatistics();

    boolean anyMatch(IntPredicate predicate);

    boolean allMatch(IntPredicate predicate);

    boolean noneMatch(IntPredicate predicate);

    OptionalInt findFirst();

    OptionalInt findAny();

    LongStream asLongStream();

    DoubleStream asDoubleStream();

    Stream<Integer> boxed();

    public static IntStream empty() { return of(new int[0]); }

    public static IntStream of(int t) { return of(new int[] { t }); }

    public static IntStream of(final int... values) {
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            int i;

            public boolean hasNext() { return i < values.length; }

            public int nextInt() {
                if (i >= values.length) throw new NoSuchElementException();
                return values[i++];
            }
        });
    }

    public static IntStream range(final int startInclusive, final int endExclusive) {
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            int i = startInclusive;

            public boolean hasNext() { return i < endExclusive; }

            public int nextInt() {
                if (i >= endExclusive) throw new NoSuchElementException();
                return i++;
            }
        });
    }

    public static IntStream rangeClosed(final int startInclusive, final int endInclusive) {
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            long i = startInclusive;

            public boolean hasNext() { return i <= endInclusive; }

            public int nextInt() {
                if (i > endInclusive) throw new NoSuchElementException();
                return (int) i++;
            }
        });
    }

    public static IntStream iterate(final int seed, final IntUnaryOperator f) {
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            int t;
            boolean started;

            public boolean hasNext() { return true; }

            public int nextInt() {
                t = started ? f.applyAsInt(t) : seed;
                started = true;
                return t;
            }
        });
    }

    public static IntStream iterate(final int seed, final IntPredicate hasNext, final IntUnaryOperator next) {
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            int t = seed;
            boolean done = !hasNext.test(seed);

            public boolean hasNext() { return !done; }

            public int nextInt() {
                if (done) throw new NoSuchElementException();
                int r = t;
                t = next.applyAsInt(t);
                done = !hasNext.test(t);
                return r;
            }
        });
    }

    public static IntStream generate(final IntSupplier s) {
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            public boolean hasNext() { return true; }

            public int nextInt() { return s.getAsInt(); }
        });
    }

    public static IntStream concat(IntStream a, IntStream b) {
        final PrimitiveIterator.OfInt ia = a.iterator(), ib = b.iterator();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            public boolean hasNext() { return ia.hasNext() || ib.hasNext(); }

            public int nextInt() { return ia.hasNext() ? ia.nextInt() : ib.nextInt(); }
        });
    }
}
