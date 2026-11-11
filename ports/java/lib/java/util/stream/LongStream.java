package java.util.stream;

import java.util.*;
import java.util.function.*;

public interface LongStream extends BaseStream<Long, LongStream> {
    PrimitiveIterator.OfLong iterator();

    LongStream filter(LongPredicate predicate);

    LongStream map(LongUnaryOperator mapper);

    <U> Stream<U> mapToObj(LongFunction<? extends U> mapper);

    IntStream mapToInt(LongToIntFunction mapper);

    DoubleStream mapToDouble(LongToDoubleFunction mapper);

    LongStream sorted();

    LongStream distinct();

    LongStream limit(long maxSize);

    LongStream skip(long n);

    LongStream peek(LongConsumer action);

    void forEach(LongConsumer action);

    long[] toArray();

    long reduce(long identity, LongBinaryOperator op);

    OptionalLong reduce(LongBinaryOperator op);

    long sum();

    OptionalLong min();

    OptionalLong max();

    long count();

    OptionalDouble average();

    LongSummaryStatistics summaryStatistics();

    boolean anyMatch(LongPredicate predicate);

    boolean allMatch(LongPredicate predicate);

    boolean noneMatch(LongPredicate predicate);

    OptionalLong findFirst();

    Stream<Long> boxed();

    DoubleStream asDoubleStream();

    public static LongStream empty() { return of(new long[0]); }

    public static LongStream of(long t) { return of(new long[] { t }); }

    public static LongStream of(final long... values) {
        return new LongPipeline(new PrimitiveIterator.OfLong() {
            int i;

            public boolean hasNext() { return i < values.length; }

            public long nextLong() {
                if (i >= values.length) throw new NoSuchElementException();
                return values[i++];
            }
        });
    }

    public static LongStream range(final long startInclusive, final long endExclusive) {
        return new LongPipeline(new PrimitiveIterator.OfLong() {
            long i = startInclusive;

            public boolean hasNext() { return i < endExclusive; }

            public long nextLong() {
                if (i >= endExclusive) throw new NoSuchElementException();
                return i++;
            }
        });
    }

    public static LongStream rangeClosed(final long startInclusive, final long endInclusive) {
        return new LongPipeline(new PrimitiveIterator.OfLong() {
            long i = startInclusive;
            boolean done = startInclusive > endInclusive;

            public boolean hasNext() { return !done; }

            public long nextLong() {
                if (done) throw new NoSuchElementException();
                long r = i;
                if (i == endInclusive) done = true;
                else i++;
                return r;
            }
        });
    }

    public static LongStream iterate(final long seed, final LongUnaryOperator f) {
        return new LongPipeline(new PrimitiveIterator.OfLong() {
            long t;
            boolean started;

            public boolean hasNext() { return true; }

            public long nextLong() {
                t = started ? f.applyAsLong(t) : seed;
                started = true;
                return t;
            }
        });
    }
}
