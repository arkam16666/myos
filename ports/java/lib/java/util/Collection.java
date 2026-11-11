package java.util;

import java.util.function.IntFunction;
import java.util.function.Predicate;

public interface Collection<E> extends Iterable<E> {
    int size();

    boolean isEmpty();

    boolean contains(Object o);

    Iterator<E> iterator();

    Object[] toArray();

    <T> T[] toArray(T[] a);

    default <T> T[] toArray(IntFunction<T[]> generator) { return toArray(generator.apply(0)); }

    boolean add(E e);

    boolean remove(Object o);

    boolean containsAll(Collection<?> c);

    boolean addAll(Collection<? extends E> c);

    boolean removeAll(Collection<?> c);

    boolean retainAll(Collection<?> c);

    void clear();

    boolean equals(Object o);

    int hashCode();

    default boolean removeIf(Predicate<? super E> filter) {
        boolean removed = false;
        for (Iterator<E> it = iterator(); it.hasNext();) {
            if (filter.test(it.next())) {
                it.remove();
                removed = true;
            }
        }
        return removed;
    }

    default java.util.stream.Stream<E> stream() { return java.util.stream.Stream.fromIterator(iterator()); }

    default java.util.stream.Stream<E> parallelStream() { return stream(); }
}
