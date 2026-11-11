package java.util;

/** List.of, Set.of, Map.of: unmodifiable, no nulls. */
final class ImmutableCollections {
    private ImmutableCollections() {}

    static UnsupportedOperationException uoe() { return new UnsupportedOperationException(); }

    static <E> List<E> listOf(Object... a) {
        Object[] c = new Object[a.length];
        for (int i = 0; i < a.length; i++) c[i] = Objects.requireNonNull(a[i]);
        return new ListN<E>(c);
    }

    static final class ListN<E> extends AbstractList<E> implements RandomAccess {
        private final Object[] a;

        ListN(Object[] a) { this.a = a; }

        @SuppressWarnings("unchecked")
        public E get(int index) {
            if (index < 0 || index >= a.length) throw new IndexOutOfBoundsException("Index " + index + " out of bounds for length " + a.length);
            return (E) a[index];
        }

        public int size() { return a.length; }

        public boolean add(E e) { throw uoe(); }

        public void add(int i, E e) { throw uoe(); }

        public E remove(int i) { throw uoe(); }

        public boolean remove(Object o) { throw uoe(); }

        public E set(int i, E e) { throw uoe(); }

        public void clear() { throw uoe(); }

        public boolean addAll(Collection<? extends E> c) { throw uoe(); }

        public boolean removeAll(Collection<?> c) { throw uoe(); }

        public boolean retainAll(Collection<?> c) { throw uoe(); }

        public boolean removeIf(java.util.function.Predicate<? super E> f) { throw uoe(); }

        public void sort(Comparator<? super E> c) { throw uoe(); }

        public void replaceAll(java.util.function.UnaryOperator<E> op) { throw uoe(); }

        public Object[] toArray() { return a.clone(); }
    }

    @SuppressWarnings("unchecked")
    static <E> Set<E> setOf(Object... a) {
        LinkedHashSet<E> s = new LinkedHashSet<E>();
        for (Object e : a) {
            if (!s.add((E) Objects.requireNonNull(e))) throw new IllegalArgumentException("duplicate element: " + e);
        }
        return Collections.unmodifiableSet(s);
    }

    @SuppressWarnings("unchecked")
    static <K, V> Map<K, V> mapOf(Object... kv) {
        LinkedHashMap<K, V> m = new LinkedHashMap<K, V>();
        for (int i = 0; i + 1 < kv.length; i += 2) {
            Object k = Objects.requireNonNull(kv[i]), v = Objects.requireNonNull(kv[i + 1]);
            if (m.put((K) k, (V) v) != null || m.size() * 2 < i + 2) throw new IllegalArgumentException("duplicate key: " + k);
        }
        return Collections.unmodifiableMap(m);
    }
}
