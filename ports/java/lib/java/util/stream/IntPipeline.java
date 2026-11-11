package java.util.stream;

import java.util.*;
import java.util.function.*;

final class IntPipeline implements IntStream {
    private final PrimitiveIterator.OfInt it;
    private boolean used;

    IntPipeline(PrimitiveIterator.OfInt it) { this.it = it; }

    private PrimitiveIterator.OfInt take() {
        if (used) throw new IllegalStateException("stream has already been operated upon or closed");
        used = true;
        return it;
    }

    public PrimitiveIterator.OfInt iterator() { return take(); }

    public boolean isParallel() { return false; }

    public IntStream sequential() { return this; }

    public IntStream parallel() { return this; }

    public IntStream unordered() { return this; }

    public IntStream onClose(Runnable closeHandler) { return this; }

    public void close() {}

    private int[] drain() {
        PrimitiveIterator.OfInt src = take();
        int[] a = new int[16];
        int n = 0;
        while (src.hasNext()) {
            if (n == a.length) a = Arrays.copyOf(a, n * 2);
            a[n++] = src.nextInt();
        }
        return Arrays.copyOf(a, n);
    }

    public IntStream filter(final IntPredicate predicate) {
        final PrimitiveIterator.OfInt src = take();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            int next;
            int state;

            public boolean hasNext() {
                if (state == 0) {
                    state = 2;
                    while (src.hasNext()) {
                        int v = src.nextInt();
                        if (predicate.test(v)) {
                            next = v;
                            state = 1;
                            break;
                        }
                    }
                }
                return state == 1;
            }

            public int nextInt() {
                if (!hasNext()) throw new NoSuchElementException();
                state = 0;
                return next;
            }
        });
    }

    public IntStream map(final IntUnaryOperator mapper) {
        final PrimitiveIterator.OfInt src = take();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            public boolean hasNext() { return src.hasNext(); }

            public int nextInt() { return mapper.applyAsInt(src.nextInt()); }
        });
    }

    public <U> Stream<U> mapToObj(final IntFunction<? extends U> mapper) {
        final PrimitiveIterator.OfInt src = take();
        return new Pipeline<U>(new Iterator<U>() {
            public boolean hasNext() { return src.hasNext(); }

            public U next() { return mapper.apply(src.nextInt()); }
        });
    }

    public LongStream mapToLong(final IntToLongFunction mapper) {
        final PrimitiveIterator.OfInt src = take();
        return new LongPipeline(new PrimitiveIterator.OfLong() {
            public boolean hasNext() { return src.hasNext(); }

            public long nextLong() { return mapper.applyAsLong(src.nextInt()); }
        });
    }

    public DoubleStream mapToDouble(final IntToDoubleFunction mapper) {
        final PrimitiveIterator.OfInt src = take();
        return new DoublePipeline(new PrimitiveIterator.OfDouble() {
            public boolean hasNext() { return src.hasNext(); }

            public double nextDouble() { return mapper.applyAsDouble(src.nextInt()); }
        });
    }

    public IntStream flatMap(final IntFunction<? extends IntStream> mapper) {
        final PrimitiveIterator.OfInt src = take();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            PrimitiveIterator.OfInt cur;

            public boolean hasNext() {
                for (;;) {
                    if (cur != null && cur.hasNext()) return true;
                    if (!src.hasNext()) return false;
                    IntStream s = mapper.apply(src.nextInt());
                    cur = s == null ? null : s.iterator();
                }
            }

            public int nextInt() {
                if (!hasNext()) throw new NoSuchElementException();
                return cur.nextInt();
            }
        });
    }

    public IntStream distinct() {
        int[] a = drain();
        LinkedHashSet<Integer> s = new LinkedHashSet<Integer>();
        for (int v : a) s.add(v);
        int[] r = new int[s.size()];
        int i = 0;
        for (Integer v : s) r[i++] = v;
        return IntStream.of(r);
    }

    public IntStream sorted() {
        int[] a = drain();
        Arrays.sort(a);
        return IntStream.of(a);
    }

    public IntStream peek(final IntConsumer action) {
        final PrimitiveIterator.OfInt src = take();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            public boolean hasNext() { return src.hasNext(); }

            public int nextInt() {
                int v = src.nextInt();
                action.accept(v);
                return v;
            }
        });
    }

    public IntStream limit(final long maxSize) {
        final PrimitiveIterator.OfInt src = take();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            long n;

            public boolean hasNext() { return n < maxSize && src.hasNext(); }

            public int nextInt() {
                if (n >= maxSize) throw new NoSuchElementException();
                n++;
                return src.nextInt();
            }
        });
    }

    public IntStream skip(final long count) {
        final PrimitiveIterator.OfInt src = take();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            boolean skipped;

            void skipNow() {
                if (!skipped) {
                    skipped = true;
                    for (long i = 0; i < count && src.hasNext(); i++) src.nextInt();
                }
            }

            public boolean hasNext() {
                skipNow();
                return src.hasNext();
            }

            public int nextInt() {
                skipNow();
                return src.nextInt();
            }
        });
    }

    public IntStream takeWhile(final IntPredicate predicate) {
        final PrimitiveIterator.OfInt src = take();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            int next, state;

            public boolean hasNext() {
                if (state == 0) {
                    state = 2;
                    if (src.hasNext()) {
                        int v = src.nextInt();
                        if (predicate.test(v)) {
                            next = v;
                            state = 1;
                        } else state = 3;
                    }
                }
                return state == 1;
            }

            public int nextInt() {
                if (!hasNext()) throw new NoSuchElementException();
                state = 0;
                return next;
            }
        });
    }

    public IntStream dropWhile(final IntPredicate predicate) {
        int[] a = drain();
        int i = 0;
        while (i < a.length && predicate.test(a[i])) i++;
        return IntStream.of(Arrays.copyOfRange(a, i, a.length));
    }

    public void forEach(IntConsumer action) {
        for (PrimitiveIterator.OfInt src = take(); src.hasNext();) action.accept(src.nextInt());
    }

    public void forEachOrdered(IntConsumer action) { forEach(action); }

    public int[] toArray() { return drain(); }

    public int reduce(int identity, IntBinaryOperator op) {
        int r = identity;
        for (PrimitiveIterator.OfInt src = take(); src.hasNext();) r = op.applyAsInt(r, src.nextInt());
        return r;
    }

    public OptionalInt reduce(IntBinaryOperator op) {
        PrimitiveIterator.OfInt src = take();
        if (!src.hasNext()) return OptionalInt.empty();
        int r = src.nextInt();
        while (src.hasNext()) r = op.applyAsInt(r, src.nextInt());
        return OptionalInt.of(r);
    }

    public <R> R collect(Supplier<R> supplier, ObjIntConsumer<R> accumulator, BiConsumer<R, R> combiner) {
        R r = supplier.get();
        for (PrimitiveIterator.OfInt src = take(); src.hasNext();) accumulator.accept(r, src.nextInt());
        return r;
    }

    public int sum() {
        int s = 0;
        for (PrimitiveIterator.OfInt src = take(); src.hasNext();) s += src.nextInt();
        return s;
    }

    public OptionalInt min() {
        PrimitiveIterator.OfInt src = take();
        if (!src.hasNext()) return OptionalInt.empty();
        int r = src.nextInt();
        while (src.hasNext()) r = Math.min(r, src.nextInt());
        return OptionalInt.of(r);
    }

    public OptionalInt max() {
        PrimitiveIterator.OfInt src = take();
        if (!src.hasNext()) return OptionalInt.empty();
        int r = src.nextInt();
        while (src.hasNext()) r = Math.max(r, src.nextInt());
        return OptionalInt.of(r);
    }

    public long count() {
        long n = 0;
        for (PrimitiveIterator.OfInt src = take(); src.hasNext(); src.nextInt()) n++;
        return n;
    }

    public OptionalDouble average() {
        long n = 0, s = 0;
        for (PrimitiveIterator.OfInt src = take(); src.hasNext(); n++) s += src.nextInt();
        return n == 0 ? OptionalDouble.empty() : OptionalDouble.of((double) s / n);
    }

    public IntSummaryStatistics summaryStatistics() {
        IntSummaryStatistics st = new IntSummaryStatistics();
        for (PrimitiveIterator.OfInt src = take(); src.hasNext();) st.accept(src.nextInt());
        return st;
    }

    public boolean anyMatch(IntPredicate predicate) {
        for (PrimitiveIterator.OfInt src = take(); src.hasNext();) if (predicate.test(src.nextInt())) return true;
        return false;
    }

    public boolean allMatch(IntPredicate predicate) {
        for (PrimitiveIterator.OfInt src = take(); src.hasNext();) if (!predicate.test(src.nextInt())) return false;
        return true;
    }

    public boolean noneMatch(IntPredicate predicate) { return !anyMatch(predicate); }

    public OptionalInt findFirst() {
        PrimitiveIterator.OfInt src = take();
        return src.hasNext() ? OptionalInt.of(src.nextInt()) : OptionalInt.empty();
    }

    public OptionalInt findAny() { return findFirst(); }

    public LongStream asLongStream() {
        final PrimitiveIterator.OfInt src = take();
        return new LongPipeline(new PrimitiveIterator.OfLong() {
            public boolean hasNext() { return src.hasNext(); }

            public long nextLong() { return src.nextInt(); }
        });
    }

    public DoubleStream asDoubleStream() {
        final PrimitiveIterator.OfInt src = take();
        return new DoublePipeline(new PrimitiveIterator.OfDouble() {
            public boolean hasNext() { return src.hasNext(); }

            public double nextDouble() { return src.nextInt(); }
        });
    }

    public Stream<Integer> boxed() {
        final PrimitiveIterator.OfInt src = take();
        return new Pipeline<Integer>(new Iterator<Integer>() {
            public boolean hasNext() { return src.hasNext(); }

            public Integer next() { return src.nextInt(); }
        });
    }
}
