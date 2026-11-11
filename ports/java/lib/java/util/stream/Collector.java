package java.util.stream;

import java.util.Set;
import java.util.function.BiConsumer;
import java.util.function.BinaryOperator;
import java.util.function.Function;
import java.util.function.Supplier;

public interface Collector<T, A, R> {
    Supplier<A> supplier();

    BiConsumer<A, T> accumulator();

    BinaryOperator<A> combiner();

    Function<A, R> finisher();

    Set<Characteristics> characteristics();

    enum Characteristics { CONCURRENT, UNORDERED, IDENTITY_FINISH }

    @SuppressWarnings("unchecked")
    public static <T, R> Collector<T, R, R> of(final Supplier<R> supplier, final BiConsumer<R, T> accumulator,
                                               BinaryOperator<R> combiner, Characteristics... characteristics) {
        return new Collectors.Coll<T, R, R>() {
            R make() { return supplier.get(); }

            void add(R a, T t) { accumulator.accept(a, t); }

            R finish(R a) { return a; }
        };
    }

    public static <T, A, R> Collector<T, A, R> of(final Supplier<A> supplier, final BiConsumer<A, T> accumulator,
                                                  BinaryOperator<A> combiner, final Function<A, R> finisher,
                                                  Characteristics... characteristics) {
        return new Collectors.Coll<T, A, R>() {
            A make() { return supplier.get(); }

            void add(A a, T t) { accumulator.accept(a, t); }

            R finish(A a) { return finisher.apply(a); }
        };
    }
}
