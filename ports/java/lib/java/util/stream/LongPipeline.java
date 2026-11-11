package java.util.stream;

import java.util.*;
import java.util.function.*;

final class LongPipeline implements LongStream {
    private final PrimitiveIterator.OfLong it;
    private boolean used;

    LongPipeline(PrimitiveIterator.OfLong it) { this.it = it; }

    private PrimitiveIterator.OfLong take() {
        if (used) throw new IllegalStateException("stream has already been operated upon or closed");
        used = true;
        return it;
    }

    public PrimitiveIterator.OfLong iterator() { return take(); }

    public boolean isParallel() { return false; }

    public LongStream sequential() { return this; }

    public LongStream parallel() { return this; }

    public LongStream unordered() { return this; }

    public LongStream onClose(Runnable closeHandler) { return this; }

    public void close() {}

    private long[] drain() {
        PrimitiveIterator.OfLong src = take();
        long[] a = new long[16];
        int n = 0;
        while (src.hasNext()) {
            if (n == a.length) a = Arrays.copyOf(a, n * 2);
            a[n++] = src.nextLong();
        }
        return Arrays.copyOf(a, n);
    }

    public LongStream filter(LongPredicate predicate) {
        long[] a = drain();
        long[] r = new long[a.length];
        int n = 0;
        for (long v : a) if (predicate.test(v)) r[n++] = v;
        return LongStream.of(Arrays.copyOf(r, n));
    }

    public LongStream map(final LongUnaryOperator mapper) {
        final PrimitiveIterator.OfLong src = take();
        return new LongPipeline(new PrimitiveIterator.OfLong() {
            public boolean hasNext() { return src.hasNext(); }

            public long nextLong() { return mapper.applyAsLong(src.nextLong()); }
        });
    }

    public <U> Stream<U> mapToObj(final LongFunction<? extends U> mapper) {
        final PrimitiveIterator.OfLong src = take();
        return new Pipeline<U>(new Iterator<U>() {
            public boolean hasNext() { return src.hasNext(); }

            public U next() { return mapper.apply(src.nextLong()); }
        });
    }

    public IntStream mapToInt(final LongToIntFunction mapper) {
        final PrimitiveIterator.OfLong src = take();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            public boolean hasNext() { return src.hasNext(); }

            public int nextInt() { return mapper.applyAsInt(src.nextLong()); }
        });
    }

    public DoubleStream mapToDouble(final LongToDoubleFunction mapper) {
        final PrimitiveIterator.OfLong src = take();
        return new DoublePipeline(new PrimitiveIterator.OfDouble() {
            public boolean hasNext() { return src.hasNext(); }

            public double nextDouble() { return mapper.applyAsDouble(src.nextLong()); }
        });
    }

    public LongStream sorted() {
        long[] a = drain();
        Arrays.sort(a);
        return LongStream.of(a);
    }

    public LongStream distinct() {
        LinkedHashSet<Long> s = new LinkedHashSet<Long>();
        for (long v : drain()) s.add(v);
        long[] r = new long[s.size()];
        int i = 0;
        for (Long v : s) r[i++] = v;
        return LongStream.of(r);
    }

    public LongStream limit(final long maxSize) {
        final PrimitiveIterator.OfLong src = take();
        return new LongPipeline(new PrimitiveIterator.OfLong() {
            long n;

            public boolean hasNext() { return n < maxSize && src.hasNext(); }

            public long nextLong() {
                n++;
                return src.nextLong();
            }
        });
    }

    public LongStream skip(long count) {
        long[] a = drain();
        int k = (int) Math.min(count, a.length);
        return LongStream.of(Arrays.copyOfRange(a, k, a.length));
    }

    public LongStream peek(final LongConsumer action) {
        final PrimitiveIterator.OfLong src = take();
        return new LongPipeline(new PrimitiveIterator.OfLong() {
            public boolean hasNext() { return src.hasNext(); }

            public long nextLong() {
                long v = src.nextLong();
                action.accept(v);
                return v;
            }
        });
    }

    public void forEach(LongConsumer action) {
        for (PrimitiveIterator.OfLong src = take(); src.hasNext();) action.accept(src.nextLong());
    }

    public long[] toArray() { return drain(); }

    public long reduce(long identity, LongBinaryOperator op) {
        long r = identity;
        for (PrimitiveIterator.OfLong src = take(); src.hasNext();) r = op.applyAsLong(r, src.nextLong());
        return r;
    }

    public OptionalLong reduce(LongBinaryOperator op) {
        PrimitiveIterator.OfLong src = take();
        if (!src.hasNext()) return OptionalLong.empty();
        long r = src.nextLong();
        while (src.hasNext()) r = op.applyAsLong(r, src.nextLong());
        return OptionalLong.of(r);
    }

    public long sum() {
        long s = 0;
        for (PrimitiveIterator.OfLong src = take(); src.hasNext();) s += src.nextLong();
        return s;
    }

    public OptionalLong min() {
        long[] a = drain();
        if (a.length == 0) return OptionalLong.empty();
        long r = a[0];
        for (long v : a) r = Math.min(r, v);
        return OptionalLong.of(r);
    }

    public OptionalLong max() {
        long[] a = drain();
        if (a.length == 0) return OptionalLong.empty();
        long r = a[0];
        for (long v : a) r = Math.max(r, v);
        return OptionalLong.of(r);
    }

    public long count() {
        long n = 0;
        for (PrimitiveIterator.OfLong src = take(); src.hasNext(); src.nextLong()) n++;
        return n;
    }

    public OptionalDouble average() {
        long[] a = drain();
        if (a.length == 0) return OptionalDouble.empty();
        long s = 0;
        for (long v : a) s += v;
        return OptionalDouble.of((double) s / a.length);
    }

    public LongSummaryStatistics summaryStatistics() {
        LongSummaryStatistics st = new LongSummaryStatistics();
        for (PrimitiveIterator.OfLong src = take(); src.hasNext();) st.accept(src.nextLong());
        return st;
    }

    public boolean anyMatch(LongPredicate predicate) {
        for (PrimitiveIterator.OfLong src = take(); src.hasNext();) if (predicate.test(src.nextLong())) return true;
        return false;
    }

    public boolean allMatch(LongPredicate predicate) {
        for (PrimitiveIterator.OfLong src = take(); src.hasNext();) if (!predicate.test(src.nextLong())) return false;
        return true;
    }

    public boolean noneMatch(LongPredicate predicate) { return !anyMatch(predicate); }

    public OptionalLong findFirst() {
        PrimitiveIterator.OfLong src = take();
        return src.hasNext() ? OptionalLong.of(src.nextLong()) : OptionalLong.empty();
    }

    public Stream<Long> boxed() {
        final PrimitiveIterator.OfLong src = take();
        return new Pipeline<Long>(new Iterator<Long>() {
            public boolean hasNext() { return src.hasNext(); }

            public Long next() { return src.nextLong(); }
        });
    }

    public DoubleStream asDoubleStream() {
        final PrimitiveIterator.OfLong src = take();
        return new DoublePipeline(new PrimitiveIterator.OfDouble() {
            public boolean hasNext() { return src.hasNext(); }

            public double nextDouble() { return src.nextLong(); }
        });
    }
}
