package java.util.stream;

import java.util.*;
import java.util.function.*;

public interface DoubleStream extends BaseStream<Double, DoubleStream> {
    PrimitiveIterator.OfDouble iterator();

    DoubleStream filter(DoublePredicate predicate);

    DoubleStream map(DoubleUnaryOperator mapper);

    <U> Stream<U> mapToObj(DoubleFunction<? extends U> mapper);

    IntStream mapToInt(DoubleToIntFunction mapper);

    LongStream mapToLong(DoubleToLongFunction mapper);

    DoubleStream sorted();

    DoubleStream distinct();

    DoubleStream limit(long maxSize);

    DoubleStream skip(long n);

    DoubleStream peek(DoubleConsumer action);

    void forEach(DoubleConsumer action);

    double[] toArray();

    double reduce(double identity, DoubleBinaryOperator op);

    OptionalDouble reduce(DoubleBinaryOperator op);

    double sum();

    OptionalDouble min();

    OptionalDouble max();

    long count();

    OptionalDouble average();

    DoubleSummaryStatistics summaryStatistics();

    boolean anyMatch(DoublePredicate predicate);

    boolean allMatch(DoublePredicate predicate);

    boolean noneMatch(DoublePredicate predicate);

    OptionalDouble findFirst();

    Stream<Double> boxed();

    public static DoubleStream empty() { return of(new double[0]); }

    public static DoubleStream of(double t) { return of(new double[] { t }); }

    public static DoubleStream of(final double... values) {
        return new DoublePipeline(new PrimitiveIterator.OfDouble() {
            int i;

            public boolean hasNext() { return i < values.length; }

            public double nextDouble() {
                if (i >= values.length) throw new NoSuchElementException();
                return values[i++];
            }
        });
    }

    public static DoubleStream iterate(final double seed, final DoubleUnaryOperator f) {
        return new DoublePipeline(new PrimitiveIterator.OfDouble() {
            double t;
            boolean started;

            public boolean hasNext() { return true; }

            public double nextDouble() {
                t = started ? f.applyAsDouble(t) : seed;
                started = true;
                return t;
            }
        });
    }

    public static DoubleStream generate(final DoubleSupplier s) {
        return new DoublePipeline(new PrimitiveIterator.OfDouble() {
            public boolean hasNext() { return true; }

            public double nextDouble() { return s.getAsDouble(); }
        });
    }
}
