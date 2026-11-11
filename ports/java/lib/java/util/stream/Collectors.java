package java.util.stream;

import java.util.*;
import java.util.function.*;

public final class Collectors {
    private Collectors() {}

    /* every collector here: make a container, add elements, finish */
    static abstract class Coll<T, A, R> implements Collector<T, A, R> {
        abstract A make();

        abstract void add(A a, T t);

        abstract R finish(A a);

        public Supplier<A> supplier() {
            return new Supplier<A>() {
                public A get() { return make(); }
            };
        }

        public BiConsumer<A, T> accumulator() {
            return new BiConsumer<A, T>() {
                public void accept(A a, T t) { add(a, t); }
            };
        }

        public BinaryOperator<A> combiner() {
            return new BinaryOperator<A>() {
                public A apply(A a, A b) { throw new UnsupportedOperationException("parallel streams"); }
            };
        }

        public Function<A, R> finisher() {
            return new Function<A, R>() {
                public R apply(A a) { return finish(a); }
            };
        }

        public Set<Collector.Characteristics> characteristics() { return new HashSet<Collector.Characteristics>(); }
    }

    /* run a collector over a sequence of elements (used by grouping collectors) */
    @SuppressWarnings("unchecked")
    static <T, A, R> R collectAll(Collector<? super T, A, R> c, Iterator<T> it) {
        A a = c.supplier().get();
        BiConsumer<A, ? super T> acc = c.accumulator();
        while (it.hasNext()) acc.accept(a, it.next());
        return c.finisher().apply(a);
    }

    public static <T, C extends Collection<T>> Collector<T, ?, C> toCollection(final Supplier<C> collectionFactory) {
        return new Coll<T, C, C>() {
            C make() { return collectionFactory.get(); }

            void add(C a, T t) { a.add(t); }

            C finish(C a) { return a; }
        };
    }

    public static <T> Collector<T, ?, List<T>> toList() {
        return new Coll<T, List<T>, List<T>>() {
            List<T> make() { return new ArrayList<T>(); }

            void add(List<T> a, T t) { a.add(t); }

            List<T> finish(List<T> a) { return a; }
        };
    }

    public static <T> Collector<T, ?, List<T>> toUnmodifiableList() {
        return new Coll<T, List<T>, List<T>>() {
            List<T> make() { return new ArrayList<T>(); }

            void add(List<T> a, T t) { a.add(Objects.requireNonNull(t)); }

            List<T> finish(List<T> a) { return Collections.unmodifiableList(a); }
        };
    }

    public static <T> Collector<T, ?, Set<T>> toSet() {
        return new Coll<T, Set<T>, Set<T>>() {
            Set<T> make() { return new HashSet<T>(); }

            void add(Set<T> a, T t) { a.add(t); }

            Set<T> finish(Set<T> a) { return a; }
        };
    }

    public static <T> Collector<T, ?, Set<T>> toUnmodifiableSet() {
        return new Coll<T, Set<T>, Set<T>>() {
            Set<T> make() { return new HashSet<T>(); }

            void add(Set<T> a, T t) { a.add(Objects.requireNonNull(t)); }

            Set<T> finish(Set<T> a) { return Collections.unmodifiableSet(a); }
        };
    }

    public static Collector<CharSequence, ?, String> joining() { return joining("", "", ""); }

    public static Collector<CharSequence, ?, String> joining(CharSequence delimiter) { return joining(delimiter, "", ""); }

    public static Collector<CharSequence, ?, String> joining(final CharSequence delimiter, final CharSequence prefix,
                                                             final CharSequence suffix) {
        return new Coll<CharSequence, StringJoiner, String>() {
            StringJoiner make() { return new StringJoiner(delimiter, prefix, suffix); }

            void add(StringJoiner a, CharSequence t) { a.add(t); }

            String finish(StringJoiner a) { return a.toString(); }
        };
    }

    public static <T, U, A, R> Collector<T, ?, R> mapping(final Function<? super T, ? extends U> mapper,
                                                          final Collector<? super U, A, R> downstream) {
        return new Coll<T, A, R>() {
            A make() { return downstream.supplier().get(); }

            void add(A a, T t) { downstream.accumulator().accept(a, mapper.apply(t)); }

            R finish(A a) { return downstream.finisher().apply(a); }
        };
    }

    public static <T, A, R> Collector<T, ?, R> filtering(final Predicate<? super T> predicate,
                                                         final Collector<? super T, A, R> downstream) {
        return new Coll<T, A, R>() {
            A make() { return downstream.supplier().get(); }

            void add(A a, T t) { if (predicate.test(t)) downstream.accumulator().accept(a, t); }

            R finish(A a) { return downstream.finisher().apply(a); }
        };
    }

    public static <T, U, A, R> Collector<T, ?, R> flatMapping(final Function<? super T, ? extends Stream<? extends U>> mapper,
                                                              final Collector<? super U, A, R> downstream) {
        return new Coll<T, A, R>() {
            A make() { return downstream.supplier().get(); }

            void add(A a, T t) {
                Stream<? extends U> s = mapper.apply(t);
                if (s == null) return;
                for (Iterator<? extends U> it = s.iterator(); it.hasNext();) downstream.accumulator().accept(a, it.next());
            }

            R finish(A a) { return downstream.finisher().apply(a); }
        };
    }

    public static <T, A, R, RR> Collector<T, A, RR> collectingAndThen(final Collector<T, A, R> downstream,
                                                                     final Function<R, RR> finisher) {
        return new Coll<T, A, RR>() {
            A make() { return downstream.supplier().get(); }

            void add(A a, T t) { downstream.accumulator().accept(a, t); }

            RR finish(A a) { return finisher.apply(downstream.finisher().apply(a)); }
        };
    }

    public static <T> Collector<T, ?, Long> counting() {
        return new Coll<T, long[], Long>() {
            long[] make() { return new long[1]; }

            void add(long[] a, T t) { a[0]++; }

            Long finish(long[] a) { return a[0]; }
        };
    }

    public static <T> Collector<T, ?, Optional<T>> minBy(final Comparator<? super T> comparator) {
        return reducing(BinaryOperator.<T>minBy(comparator));
    }

    public static <T> Collector<T, ?, Optional<T>> maxBy(final Comparator<? super T> comparator) {
        return reducing(BinaryOperator.<T>maxBy(comparator));
    }

    public static <T> Collector<T, ?, Integer> summingInt(final ToIntFunction<? super T> mapper) {
        return new Coll<T, int[], Integer>() {
            int[] make() { return new int[1]; }

            void add(int[] a, T t) { a[0] += mapper.applyAsInt(t); }

            Integer finish(int[] a) { return a[0]; }
        };
    }

    public static <T> Collector<T, ?, Long> summingLong(final ToLongFunction<? super T> mapper) {
        return new Coll<T, long[], Long>() {
            long[] make() { return new long[1]; }

            void add(long[] a, T t) { a[0] += mapper.applyAsLong(t); }

            Long finish(long[] a) { return a[0]; }
        };
    }

    public static <T> Collector<T, ?, Double> summingDouble(final ToDoubleFunction<? super T> mapper) {
        return new Coll<T, double[], Double>() {
            double[] make() { return new double[1]; }

            void add(double[] a, T t) { a[0] += mapper.applyAsDouble(t); }

            Double finish(double[] a) { return a[0]; }
        };
    }

    public static <T> Collector<T, ?, Double> averagingInt(final ToIntFunction<? super T> mapper) {
        return new Coll<T, long[], Double>() {
            long[] make() { return new long[2]; }

            void add(long[] a, T t) {
                a[0] += mapper.applyAsInt(t);
                a[1]++;
            }

            Double finish(long[] a) { return a[1] == 0 ? 0.0d : (double) a[0] / a[1]; }
        };
    }

    public static <T> Collector<T, ?, Double> averagingLong(final ToLongFunction<? super T> mapper) {
        return new Coll<T, long[], Double>() {
            long[] make() { return new long[2]; }

            void add(long[] a, T t) {
                a[0] += mapper.applyAsLong(t);
                a[1]++;
            }

            Double finish(long[] a) { return a[1] == 0 ? 0.0d : (double) a[0] / a[1]; }
        };
    }

    public static <T> Collector<T, ?, Double> averagingDouble(final ToDoubleFunction<? super T> mapper) {
        return new Coll<T, double[], Double>() {
            double[] make() { return new double[2]; }

            void add(double[] a, T t) {
                a[0] += mapper.applyAsDouble(t);
                a[1]++;
            }

            Double finish(double[] a) { return a[1] == 0 ? 0.0d : a[0] / a[1]; }
        };
    }

    public static <T> Collector<T, ?, IntSummaryStatistics> summarizingInt(final ToIntFunction<? super T> mapper) {
        return new Coll<T, IntSummaryStatistics, IntSummaryStatistics>() {
            IntSummaryStatistics make() { return new IntSummaryStatistics(); }

            void add(IntSummaryStatistics a, T t) { a.accept(mapper.applyAsInt(t)); }

            IntSummaryStatistics finish(IntSummaryStatistics a) { return a; }
        };
    }

    public static <T> Collector<T, ?, DoubleSummaryStatistics> summarizingDouble(final ToDoubleFunction<? super T> mapper) {
        return new Coll<T, DoubleSummaryStatistics, DoubleSummaryStatistics>() {
            DoubleSummaryStatistics make() { return new DoubleSummaryStatistics(); }

            void add(DoubleSummaryStatistics a, T t) { a.accept(mapper.applyAsDouble(t)); }

            DoubleSummaryStatistics finish(DoubleSummaryStatistics a) { return a; }
        };
    }

    public static <T> Collector<T, ?, T> reducing(final T identity, final BinaryOperator<T> op) {
        return new Coll<T, Object[], T>() {
            Object[] make() { return new Object[] { identity }; }

            @SuppressWarnings("unchecked")
            void add(Object[] a, T t) { a[0] = op.apply((T) a[0], t); }

            @SuppressWarnings("unchecked")
            T finish(Object[] a) { return (T) a[0]; }
        };
    }

    public static <T> Collector<T, ?, Optional<T>> reducing(final BinaryOperator<T> op) {
        return new Coll<T, Object[], Optional<T>>() {
            Object[] make() { return new Object[2]; }

            @SuppressWarnings("unchecked")
            void add(Object[] a, T t) {
                if (a[1] == null) {
                    a[0] = t;
                    a[1] = Boolean.TRUE;
                } else a[0] = op.apply((T) a[0], t);
            }

            @SuppressWarnings("unchecked")
            Optional<T> finish(Object[] a) { return a[1] == null ? Optional.<T>empty() : Optional.of((T) a[0]); }
        };
    }

    public static <T, U> Collector<T, ?, U> reducing(final U identity, final Function<? super T, ? extends U> mapper,
                                                     final BinaryOperator<U> op) {
        return new Coll<T, Object[], U>() {
            Object[] make() { return new Object[] { identity }; }

            @SuppressWarnings("unchecked")
            void add(Object[] a, T t) { a[0] = op.apply((U) a[0], mapper.apply(t)); }

            @SuppressWarnings("unchecked")
            U finish(Object[] a) { return (U) a[0]; }
        };
    }

    public static <T, K> Collector<T, ?, Map<K, List<T>>> groupingBy(Function<? super T, ? extends K> classifier) {
        return groupingBy(classifier, Collectors.<T>toList());
    }

    public static <T, K, A, D> Collector<T, ?, Map<K, D>> groupingBy(Function<? super T, ? extends K> classifier,
                                                                    Collector<? super T, A, D> downstream) {
        return groupingBy(classifier, new Supplier<Map<K, D>>() {
            public Map<K, D> get() { return new HashMap<K, D>(); }
        }, downstream);
    }

    public static <T, K, D, A, M extends Map<K, D>> Collector<T, ?, M> groupingBy(final Function<? super T, ? extends K> classifier,
                                                                                 final Supplier<M> mapFactory,
                                                                                 final Collector<? super T, A, D> downstream) {
        return new Coll<T, Map<K, A>, M>() {
            Map<K, A> make() { return new LinkedHashMap<K, A>(); }

            void add(Map<K, A> m, T t) {
                K key = Objects.requireNonNull(classifier.apply(t), "element cannot be mapped to a null key");
                A container = m.get(key);
                if (container == null) {
                    container = downstream.supplier().get();
                    m.put(key, container);
                }
                downstream.accumulator().accept(container, t);
            }

            M finish(Map<K, A> m) {
                M result = mapFactory.get();
                for (Map.Entry<K, A> e : m.entrySet()) result.put(e.getKey(), downstream.finisher().apply(e.getValue()));
                return result;
            }
        };
    }

    public static <T> Collector<T, ?, Map<Boolean, List<T>>> partitioningBy(Predicate<? super T> predicate) {
        return partitioningBy(predicate, Collectors.<T>toList());
    }

    public static <T, D, A> Collector<T, ?, Map<Boolean, D>> partitioningBy(final Predicate<? super T> predicate,
                                                                           final Collector<? super T, A, D> downstream) {
        return new Coll<T, Object[], Map<Boolean, D>>() {
            Object[] make() { return new Object[] { downstream.supplier().get(), downstream.supplier().get() }; }

            @SuppressWarnings("unchecked")
            void add(Object[] a, T t) { downstream.accumulator().accept((A) a[predicate.test(t) ? 1 : 0], t); }

            @SuppressWarnings("unchecked")
            Map<Boolean, D> finish(Object[] a) {
                Map<Boolean, D> m = new LinkedHashMap<Boolean, D>();
                m.put(false, downstream.finisher().apply((A) a[0]));
                m.put(true, downstream.finisher().apply((A) a[1]));
                return m;
            }
        };
    }

    public static <T, K, U> Collector<T, ?, Map<K, U>> toMap(final Function<? super T, ? extends K> keyMapper,
                                                            final Function<? super T, ? extends U> valueMapper) {
        return new Coll<T, Map<K, U>, Map<K, U>>() {
            Map<K, U> make() { return new HashMap<K, U>(); }

            void add(Map<K, U> m, T t) {
                K k = keyMapper.apply(t);
                U v = Objects.requireNonNull(valueMapper.apply(t));
                U u = m.putIfAbsent(k, v);
                if (u != null)
                    throw new IllegalStateException("Duplicate key " + k + " (attempted merging values " + u + " and " + v + ")");
            }

            Map<K, U> finish(Map<K, U> m) { return m; }
        };
    }

    public static <T, K, U> Collector<T, ?, Map<K, U>> toMap(Function<? super T, ? extends K> keyMapper,
                                                            Function<? super T, ? extends U> valueMapper,
                                                            BinaryOperator<U> mergeFunction) {
        return toMap(keyMapper, valueMapper, mergeFunction, new Supplier<Map<K, U>>() {
            public Map<K, U> get() { return new HashMap<K, U>(); }
        });
    }

    public static <T, K, U, M extends Map<K, U>> Collector<T, ?, M> toMap(final Function<? super T, ? extends K> keyMapper,
                                                                         final Function<? super T, ? extends U> valueMapper,
                                                                         final BinaryOperator<U> mergeFunction,
                                                                         final Supplier<M> mapFactory) {
        return new Coll<T, M, M>() {
            M make() { return mapFactory.get(); }

            void add(M m, T t) { m.merge(keyMapper.apply(t), valueMapper.apply(t), mergeFunction); }

            M finish(M m) { return m; }
        };
    }

    public static <T, K, U> Collector<T, ?, Map<K, U>> toUnmodifiableMap(Function<? super T, ? extends K> keyMapper,
                                                                        Function<? super T, ? extends U> valueMapper) {
        return collectingAndThen(Collectors.<T, K, U>toMap(keyMapper, valueMapper), new Function<Map<K, U>, Map<K, U>>() {
            public Map<K, U> apply(Map<K, U> m) { return Collections.unmodifiableMap(m); }
        });
    }
}
