package java.util.stream;

import java.util.*;
import java.util.function.*;

final class DoublePipeline implements DoubleStream {
    private final PrimitiveIterator.OfDouble it;
    private boolean used;

    DoublePipeline(PrimitiveIterator.OfDouble it) { this.it = it; }

    private PrimitiveIterator.OfDouble take() {
        if (used) throw new IllegalStateException("stream has already been operated upon or closed");
        used = true;
        return it;
    }

    public PrimitiveIterator.OfDouble iterator() { return take(); }

    public boolean isParallel() { return false; }

    public DoubleStream sequential() { return this; }

    public DoubleStream parallel() { return this; }

    public DoubleStream unordered() { return this; }

    public DoubleStream onClose(Runnable closeHandler) { return this; }

    public void close() {}

    private double[] drain() {
        PrimitiveIterator.OfDouble src = take();
        double[] a = new double[16];
        int n = 0;
        while (src.hasNext()) {
            if (n == a.length) a = Arrays.copyOf(a, n * 2);
            a[n++] = src.nextDouble();
        }
        return Arrays.copyOf(a, n);
    }

    public DoubleStream filter(DoublePredicate predicate) {
        double[] a = drain();
        double[] r = new double[a.length];
        int n = 0;
        for (double v : a) if (predicate.test(v)) r[n++] = v;
        return DoubleStream.of(Arrays.copyOf(r, n));
    }

    public DoubleStream map(final DoubleUnaryOperator mapper) {
        final PrimitiveIterator.OfDouble src = take();
        return new DoublePipeline(new PrimitiveIterator.OfDouble() {
            public boolean hasNext() { return src.hasNext(); }

            public double nextDouble() { return mapper.applyAsDouble(src.nextDouble()); }
        });
    }

    public <U> Stream<U> mapToObj(final DoubleFunction<? extends U> mapper) {
        final PrimitiveIterator.OfDouble src = take();
        return new Pipeline<U>(new Iterator<U>() {
            public boolean hasNext() { return src.hasNext(); }

            public U next() { return mapper.apply(src.nextDouble()); }
        });
    }

    public IntStream mapToInt(final DoubleToIntFunction mapper) {
        final PrimitiveIterator.OfDouble src = take();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            public boolean hasNext() { return src.hasNext(); }

            public int nextInt() { return mapper.applyAsInt(src.nextDouble()); }
        });
    }

    public LongStream mapToLong(final DoubleToLongFunction mapper) {
        final PrimitiveIterator.OfDouble src = take();
        return new LongPipeline(new PrimitiveIterator.OfLong() {
            public boolean hasNext() { return src.hasNext(); }

            public long nextLong() { return mapper.applyAsLong(src.nextDouble()); }
        });
    }

    public DoubleStream sorted() {
        double[] a = drain();
        Arrays.sort(a);
        return DoubleStream.of(a);
    }

    public DoubleStream distinct() {
        LinkedHashSet<Double> s = new LinkedHashSet<Double>();
        for (double v : drain()) s.add(v);
        double[] r = new double[s.size()];
        int i = 0;
        for (Double v : s) r[i++] = v;
        return DoubleStream.of(r);
    }

    public DoubleStream limit(final long maxSize) {
        final PrimitiveIterator.OfDouble src = take();
        return new DoublePipeline(new PrimitiveIterator.OfDouble() {
            long n;

            public boolean hasNext() { return n < maxSize && src.hasNext(); }

            public double nextDouble() {
                n++;
                return src.nextDouble();
            }
        });
    }

    public DoubleStream skip(long count) {
        double[] a = drain();
        int k = (int) Math.min(count, a.length);
        return DoubleStream.of(Arrays.copyOfRange(a, k, a.length));
    }

    public DoubleStream peek(final DoubleConsumer action) {
        final PrimitiveIterator.OfDouble src = take();
        return new DoublePipeline(new PrimitiveIterator.OfDouble() {
            public boolean hasNext() { return src.hasNext(); }

            public double nextDouble() {
                double v = src.nextDouble();
                action.accept(v);
                return v;
            }
        });
    }

    public void forEach(DoubleConsumer action) {
        for (PrimitiveIterator.OfDouble src = take(); src.hasNext();) action.accept(src.nextDouble());
    }

    public double[] toArray() { return drain(); }

    public double reduce(double identity, DoubleBinaryOperator op) {
        double r = identity;
        for (PrimitiveIterator.OfDouble src = take(); src.hasNext();) r = op.applyAsDouble(r, src.nextDouble());
        return r;
    }

    public OptionalDouble reduce(DoubleBinaryOperator op) {
        PrimitiveIterator.OfDouble src = take();
        if (!src.hasNext()) return OptionalDouble.empty();
        double r = src.nextDouble();
        while (src.hasNext()) r = op.applyAsDouble(r, src.nextDouble());
        return OptionalDouble.of(r);
    }

    public double sum() {
        double s = 0;
        for (PrimitiveIterator.OfDouble src = take(); src.hasNext();) s += src.nextDouble();
        return s;
    }

    public OptionalDouble min() {
        double[] a = drain();
        if (a.length == 0) return OptionalDouble.empty();
        double r = a[0];
        for (double v : a) r = Math.min(r, v);
        return OptionalDouble.of(r);
    }

    public OptionalDouble max() {
        double[] a = drain();
        if (a.length == 0) return OptionalDouble.empty();
        double r = a[0];
        for (double v : a) r = Math.max(r, v);
        return OptionalDouble.of(r);
    }

    public long count() {
        long n = 0;
        for (PrimitiveIterator.OfDouble src = take(); src.hasNext(); src.nextDouble()) n++;
        return n;
    }

    public OptionalDouble average() {
        double[] a = drain();
        if (a.length == 0) return OptionalDouble.empty();
        double s = 0;
        for (double v : a) s += v;
        return OptionalDouble.of(s / a.length);
    }

    public DoubleSummaryStatistics summaryStatistics() {
        DoubleSummaryStatistics st = new DoubleSummaryStatistics();
        for (PrimitiveIterator.OfDouble src = take(); src.hasNext();) st.accept(src.nextDouble());
        return st;
    }

    public boolean anyMatch(DoublePredicate predicate) {
        for (PrimitiveIterator.OfDouble src = take(); src.hasNext();) if (predicate.test(src.nextDouble())) return true;
        return false;
    }

    public boolean allMatch(DoublePredicate predicate) {
        for (PrimitiveIterator.OfDouble src = take(); src.hasNext();) if (!predicate.test(src.nextDouble())) return false;
        return true;
    }

    public boolean noneMatch(DoublePredicate predicate) { return !anyMatch(predicate); }

    public OptionalDouble findFirst() {
        PrimitiveIterator.OfDouble src = take();
        return src.hasNext() ? OptionalDouble.of(src.nextDouble()) : OptionalDouble.empty();
    }

    public Stream<Double> boxed() {
        final PrimitiveIterator.OfDouble src = take();
        return new Pipeline<Double>(new Iterator<Double>() {
            public boolean hasNext() { return src.hasNext(); }

            public Double next() { return src.nextDouble(); }
        });
    }
}
