package java.util;

import java.util.function.UnaryOperator;

public interface List<E> extends SequencedCollection<E> {
    E get(int index);

    E set(int index, E element);

    void add(int index, E element);

    E remove(int index);

    int indexOf(Object o);

    int lastIndexOf(Object o);

    ListIterator<E> listIterator();

    ListIterator<E> listIterator(int index);

    List<E> subList(int fromIndex, int toIndex);

    boolean addAll(int index, Collection<? extends E> c);

    @SuppressWarnings("unchecked")
    default void sort(Comparator<? super E> c) {
        Object[] a = toArray();
        Arrays.sort(a, (Comparator<Object>) c);
        ListIterator<E> i = listIterator();
        for (Object e : a) {
            i.next();
            i.set((E) e);
        }
    }

    default void replaceAll(UnaryOperator<E> operator) {
        ListIterator<E> li = listIterator();
        while (li.hasNext()) li.set(operator.apply(li.next()));
    }

    default E getFirst() {
        if (isEmpty()) throw new NoSuchElementException();
        return get(0);
    }

    default E getLast() {
        if (isEmpty()) throw new NoSuchElementException();
        return get(size() - 1);
    }

    default E removeFirst() {
        if (isEmpty()) throw new NoSuchElementException();
        return remove(0);
    }

    default E removeLast() {
        if (isEmpty()) throw new NoSuchElementException();
        return remove(size() - 1);
    }

    default void addFirst(E e) { add(0, e); }

    default void addLast(E e) { add(e); }

    default List<E> reversed() {
        ArrayList<E> r = new ArrayList<E>(this);
        Collections.reverse(r);
        return r;
    }

    static <E> List<E> of() { return ImmutableCollections.listOf(); }

    static <E> List<E> of(E e1) { return ImmutableCollections.listOf(e1); }

    static <E> List<E> of(E e1, E e2) { return ImmutableCollections.listOf(e1, e2); }

    static <E> List<E> of(E e1, E e2, E e3) { return ImmutableCollections.listOf(e1, e2, e3); }

    static <E> List<E> of(E e1, E e2, E e3, E e4) { return ImmutableCollections.listOf(e1, e2, e3, e4); }

    static <E> List<E> of(E e1, E e2, E e3, E e4, E e5) { return ImmutableCollections.listOf(e1, e2, e3, e4, e5); }

    static <E> List<E> of(E e1, E e2, E e3, E e4, E e5, E e6) { return ImmutableCollections.listOf(e1, e2, e3, e4, e5, e6); }

    static <E> List<E> of(E e1, E e2, E e3, E e4, E e5, E e6, E e7) {
        return ImmutableCollections.listOf(e1, e2, e3, e4, e5, e6, e7);
    }

    static <E> List<E> of(E e1, E e2, E e3, E e4, E e5, E e6, E e7, E e8) {
        return ImmutableCollections.listOf(e1, e2, e3, e4, e5, e6, e7, e8);
    }

    static <E> List<E> of(E e1, E e2, E e3, E e4, E e5, E e6, E e7, E e8, E e9) {
        return ImmutableCollections.listOf(e1, e2, e3, e4, e5, e6, e7, e8, e9);
    }

    static <E> List<E> of(E e1, E e2, E e3, E e4, E e5, E e6, E e7, E e8, E e9, E e10) {
        return ImmutableCollections.listOf(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10);
    }

    @SafeVarargs
    static <E> List<E> of(E... elements) { return ImmutableCollections.listOf(elements); }

    @SuppressWarnings("unchecked")
    static <E> List<E> copyOf(Collection<? extends E> coll) { return (List<E>) ImmutableCollections.listOf(coll.toArray()); }
}
