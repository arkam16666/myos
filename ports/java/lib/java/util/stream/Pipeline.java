package java.util.stream;

import java.util.*;
import java.util.function.*;

/* A stream is a lazy iterator; each operation wraps the one before. */
final class Pipeline<T> implements Stream<T> {
    final Iterator<T> it;
    private boolean used;

    Pipeline(Iterator<T> it) { this.it = it; }

    private Iterator<T> take() {
        if (used) throw new IllegalStateException("stream has already been operated upon or closed");
        used = true;
        return it;
    }

    public Iterator<T> iterator() { return take(); }

    public boolean isParallel() { return false; }

    public Stream<T> sequential() { return this; }

    public Stream<T> parallel() { return this; }

    public Stream<T> unordered() { return this; }

    public Stream<T> onClose(Runnable closeHandler) { return this; }

    public void close() {}

    /* an iterator that computes its next element ahead */
    static abstract class Ahead<E> implements Iterator<E> {
        private E nextValue;
        private int state;          /* 0 unknown, 1 ready, 2 done */

        abstract boolean advance();          /* set via found(); false at the end */

        final void found(E e) { nextValue = e; }

        public final boolean hasNext() {
            if (state == 0) state = advance() ? 1 : 2;
            return state == 1;
        }

        public final E next() {
            if (!hasNext()) throw new NoSuchElementException();
            state = 0;
            E e = nextValue;
            nextValue = null;
            return e;
        }
    }

    public Stream<T> filter(final Predicate<? super T> predicate) {
        final Iterator<T> src = take();
        return new Pipeline<T>(new Ahead<T>() {
            boolean advance() {
                while (src.hasNext()) {
                    T t = src.next();
                    if (predicate.test(t)) {
                        found(t);
                        return true;
                    }
                }
                return false;
            }
        });
    }

    public <R> Stream<R> map(final Function<? super T, ? extends R> mapper) {
        final Iterator<T> src = take();
        return new Pipeline<R>(new Iterator<R>() {
            public boolean hasNext() { return src.hasNext(); }

            public R next() { return mapper.apply(src.next()); }
        });
    }

    public IntStream mapToInt(final ToIntFunction<? super T> mapper) {
        final Iterator<T> src = take();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            public boolean hasNext() { return src.hasNext(); }

            public int nextInt() { return mapper.applyAsInt(src.next()); }
        });
    }

    public LongStream mapToLong(final ToLongFunction<? super T> mapper) {
        final Iterator<T> src = take();
        return new LongPipeline(new PrimitiveIterator.OfLong() {
            public boolean hasNext() { return src.hasNext(); }

            public long nextLong() { return mapper.applyAsLong(src.next()); }
        });
    }

    public DoubleStream mapToDouble(final ToDoubleFunction<? super T> mapper) {
        final Iterator<T> src = take();
        return new DoublePipeline(new PrimitiveIterator.OfDouble() {
            public boolean hasNext() { return src.hasNext(); }

            public double nextDouble() { return mapper.applyAsDouble(src.next()); }
        });
    }

    public <R> Stream<R> flatMap(final Function<? super T, ? extends Stream<? extends R>> mapper) {
        final Iterator<T> src = take();
        return new Pipeline<R>(new Ahead<R>() {
            Iterator<? extends R> cur;

            boolean advance() {
                for (;;) {
                    if (cur != null && cur.hasNext()) {
                        found(cur.next());
                        return true;
                    }
                    if (!src.hasNext()) return false;
                    Stream<? extends R> s = mapper.apply(src.next());
                    cur = s == null ? null : s.iterator();
                }
            }
        });
    }

    public IntStream flatMapToInt(final Function<? super T, ? extends IntStream> mapper) {
        final Iterator<T> src = take();
        return new IntPipeline(new PrimitiveIterator.OfInt() {
            PrimitiveIterator.OfInt cur;

            public boolean hasNext() {
                for (;;) {
                    if (cur != null && cur.hasNext()) return true;
                    if (!src.hasNext()) return false;
                    IntStream s = mapper.apply(src.next());
                    cur = s == null ? null : s.iterator();
                }
            }

            public int nextInt() {
                if (!hasNext()) throw new NoSuchElementException();
                return cur.nextInt();
            }
        });
    }

    public <R> Stream<R> mapMulti(BiConsumer<? super T, ? super Consumer<R>> mapper) {
        final ArrayList<R> out = new ArrayList<R>();
        Consumer<R> sink = new Consumer<R>() {
            public void accept(R r) { out.add(r); }
        };
        for (Iterator<T> src = take(); src.hasNext();) mapper.accept(src.next(), sink);
        return out.stream();
    }

    public Stream<T> distinct() {
        final Iterator<T> src = take();
        return new Pipeline<T>(new Ahead<T>() {
            final HashSet<T> seen = new HashSet<T>();

            boolean advance() {
                while (src.hasNext()) {
                    T t = src.next();
                    if (seen.add(t)) {
                        found(t);
                        return true;
                    }
                }
                return false;
            }
        });
    }

    public Stream<T> sorted() { return sorted(null); }

    public Stream<T> sorted(final Comparator<? super T> comparator) {
        final Iterator<T> src = take();
        return new Pipeline<T>(new Iterator<T>() {
            Iterator<T> sortedIt;

            Iterator<T> get() {
                if (sortedIt == null) {
                    ArrayList<T> l = new ArrayList<T>();
                    while (src.hasNext()) l.add(src.next());
                    l.sort(comparator);
                    sortedIt = l.iterator();
                }
                return sortedIt;
            }

            public boolean hasNext() { return get().hasNext(); }

            public T next() { return get().next(); }
        });
    }

    public Stream<T> peek(final Consumer<? super T> action) {
        final Iterator<T> src = take();
        return new Pipeline<T>(new Iterator<T>() {
            public boolean hasNext() { return src.hasNext(); }

            public T next() {
                T t = src.next();
                action.accept(t);
                return t;
            }
        });
    }

    public Stream<T> limit(final long maxSize) {
        if (maxSize < 0) throw new IllegalArgumentException(Long.toString(maxSize));
        final Iterator<T> src = take();
        return new Pipeline<T>(new Iterator<T>() {
            long n;

            public boolean hasNext() { return n < maxSize && src.hasNext(); }

            public T next() {
                if (n >= maxSize) throw new NoSuchElementException();
                n++;
                return src.next();
            }
        });
    }

    public Stream<T> skip(final long count) {
        if (count < 0) throw new IllegalArgumentException(Long.toString(count));
        final Iterator<T> src = take();
        return new Pipeline<T>(new Iterator<T>() {
            boolean skipped;

            void skipNow() {
                if (!skipped) {
                    skipped = true;
                    for (long i = 0; i < count && src.hasNext(); i++) src.next();
                }
            }

            public boolean hasNext() {
                skipNow();
                return src.hasNext();
            }

            public T next() {
                skipNow();
                return src.next();
            }
        });
    }

    public Stream<T> takeWhile(final Predicate<? super T> predicate) {
        final Iterator<T> src = take();
        return new Pipeline<T>(new Ahead<T>() {
            boolean done;

            boolean advance() {
                if (done || !src.hasNext()) return false;
                T t = src.next();
                if (!predicate.test(t)) {
                    done = true;
                    return false;
                }
                found(t);
                return true;
            }
        });
    }

    public Stream<T> dropWhile(final Predicate<? super T> predicate) {
        final Iterator<T> src = take();
        return new Pipeline<T>(new Ahead<T>() {
            boolean dropping = true;

            boolean advance() {
                while (src.hasNext()) {
                    T t = src.next();
                    if (dropping && predicate.test(t)) continue;
                    dropping = false;
                    found(t);
                    return true;
                }
                return false;
            }
        });
    }

    public void forEach(Consumer<? super T> action) {
        for (Iterator<T> src = take(); src.hasNext();) action.accept(src.next());
    }

    public void forEachOrdered(Consumer<? super T> action) { forEach(action); }

    public Object[] toArray() {
        ArrayList<T> l = new ArrayList<T>();
        for (Iterator<T> src = take(); src.hasNext();) l.add(src.next());
        return l.toArray();
    }

    public <A> A[] toArray(IntFunction<A[]> generator) {
        ArrayList<T> l = new ArrayList<T>();
        for (Iterator<T> src = take(); src.hasNext();) l.add(src.next());
        A[] a = generator.apply(l.size());
        return l.toArray(a);
    }

    public T reduce(T identity, BinaryOperator<T> accumulator) {
        T r = identity;
        for (Iterator<T> src = take(); src.hasNext();) r = accumulator.apply(r, src.next());
        return r;
    }

    public Optional<T> reduce(BinaryOperator<T> accumulator) {
        Iterator<T> src = take();
        if (!src.hasNext()) return Optional.empty();
        T r = src.next();
        while (src.hasNext()) r = accumulator.apply(r, src.next());
        return Optional.of(r);
    }

    public <U> U reduce(U identity, BiFunction<U, ? super T, U> accumulator, BinaryOperator<U> combiner) {
        U r = identity;
        for (Iterator<T> src = take(); src.hasNext();) r = accumulator.apply(r, src.next());
        return r;
    }

    public <R> R collect(Supplier<R> supplier, BiConsumer<R, ? super T> accumulator, BiConsumer<R, R> combiner) {
        R r = supplier.get();
        for (Iterator<T> src = take(); src.hasNext();) accumulator.accept(r, src.next());
        return r;
    }

    public <R, A> R collect(Collector<? super T, A, R> collector) { return Collectors.collectAll(collector, take()); }

    @SuppressWarnings("unchecked")
    public List<T> toList() {
        ArrayList<T> l = new ArrayList<T>();
        for (Iterator<T> src = take(); src.hasNext();) l.add(src.next());
        return Collections.unmodifiableList(l);
    }

    public Optional<T> min(Comparator<? super T> comparator) { return reduce(BinaryOperator.<T>minBy(comparator)); }

    public Optional<T> max(Comparator<? super T> comparator) { return reduce(BinaryOperator.<T>maxBy(comparator)); }

    public long count() {
        long n = 0;
        for (Iterator<T> src = take(); src.hasNext(); src.next()) n++;
        return n;
    }

    public boolean anyMatch(Predicate<? super T> predicate) {
        for (Iterator<T> src = take(); src.hasNext();) if (predicate.test(src.next())) return true;
        return false;
    }

    public boolean allMatch(Predicate<? super T> predicate) {
        for (Iterator<T> src = take(); src.hasNext();) if (!predicate.test(src.next())) return false;
        return true;
    }

    public boolean noneMatch(Predicate<? super T> predicate) { return !anyMatch(predicate); }

    public Optional<T> findFirst() {
        Iterator<T> src = take();
        return src.hasNext() ? Optional.of(src.next()) : Optional.<T>empty();
    }

    public Optional<T> findAny() { return findFirst(); }
}
