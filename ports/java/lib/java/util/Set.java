package java.util;

public interface Set<E> extends Collection<E> {
    static <E> Set<E> of() { return ImmutableCollections.setOf(); }

    static <E> Set<E> of(E e1) { return ImmutableCollections.setOf(e1); }

    static <E> Set<E> of(E e1, E e2) { return ImmutableCollections.setOf(e1, e2); }

    static <E> Set<E> of(E e1, E e2, E e3) { return ImmutableCollections.setOf(e1, e2, e3); }

    static <E> Set<E> of(E e1, E e2, E e3, E e4) { return ImmutableCollections.setOf(e1, e2, e3, e4); }

    static <E> Set<E> of(E e1, E e2, E e3, E e4, E e5) { return ImmutableCollections.setOf(e1, e2, e3, e4, e5); }

    static <E> Set<E> of(E e1, E e2, E e3, E e4, E e5, E e6) { return ImmutableCollections.setOf(e1, e2, e3, e4, e5, e6); }

    static <E> Set<E> of(E e1, E e2, E e3, E e4, E e5, E e6, E e7) {
        return ImmutableCollections.setOf(e1, e2, e3, e4, e5, e6, e7);
    }

    static <E> Set<E> of(E e1, E e2, E e3, E e4, E e5, E e6, E e7, E e8) {
        return ImmutableCollections.setOf(e1, e2, e3, e4, e5, e6, e7, e8);
    }

    static <E> Set<E> of(E e1, E e2, E e3, E e4, E e5, E e6, E e7, E e8, E e9) {
        return ImmutableCollections.setOf(e1, e2, e3, e4, e5, e6, e7, e8, e9);
    }

    static <E> Set<E> of(E e1, E e2, E e3, E e4, E e5, E e6, E e7, E e8, E e9, E e10) {
        return ImmutableCollections.setOf(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10);
    }

    @SafeVarargs
    static <E> Set<E> of(E... elements) { return ImmutableCollections.setOf(elements); }

    @SuppressWarnings("unchecked")
    static <E> Set<E> copyOf(Collection<? extends E> coll) {
        LinkedHashSet<E> s = new LinkedHashSet<E>(coll);
        return (Set<E>) ImmutableCollections.setOf(s.toArray());
    }
}
