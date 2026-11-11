package java.util;

public class Collections {
    private Collections() {}

    static final Comparator<Object> NATURAL = new Comparator<Object>() {
        @SuppressWarnings("unchecked")
        public int compare(Object a, Object b) { return ((Comparable<Object>) a).compareTo(b); }
    };

    private static final Comparator<Object> REVERSE = new Comparator<Object>() {
        @SuppressWarnings("unchecked")
        public int compare(Object a, Object b) { return ((Comparable<Object>) b).compareTo(a); }
    };

    public static <T extends Comparable<? super T>> void sort(List<T> list) { list.sort(null); }

    public static <T> void sort(List<T> list, Comparator<? super T> c) { list.sort(c); }

    @SuppressWarnings("unchecked")
    public static <T> Comparator<T> reverseOrder() { return (Comparator<T>) REVERSE; }

    @SuppressWarnings("unchecked")
    public static <T> Comparator<T> reverseOrder(final Comparator<T> cmp) {
        if (cmp == null) return (Comparator<T>) REVERSE;
        if (cmp == REVERSE) return (Comparator<T>) NATURAL;
        return new Comparator<T>() {
            public int compare(T a, T b) { return cmp.compare(b, a); }
        };
    }

    public static void reverse(List<?> list) {
        @SuppressWarnings("unchecked")
        List<Object> l = (List<Object>) list;
        for (int i = 0, j = l.size() - 1; i < j; i++, j--) {
            Object t = l.get(i);
            l.set(i, l.get(j));
            l.set(j, t);
        }
    }

    private static Random r;

    public static void shuffle(List<?> list) {
        if (r == null) r = new Random();
        shuffle(list, r);
    }

    public static void shuffle(List<?> list, Random rnd) {
        for (int i = list.size(); i > 1; i--) swap(list, i - 1, rnd.nextInt(i));
    }

    @SuppressWarnings("unchecked")
    public static void swap(List<?> list, int i, int j) {
        final List<Object> l = (List<Object>) list;
        l.set(i, l.set(j, l.get(i)));
    }

    public static <T> void fill(List<? super T> list, T obj) {
        for (int i = 0; i < list.size(); i++) list.set(i, obj);
    }

    public static <T> void copy(List<? super T> dest, List<? extends T> src) {
        if (src.size() > dest.size()) throw new IndexOutOfBoundsException("Source does not fit in dest");
        for (int i = 0; i < src.size(); i++) dest.set(i, src.get(i));
    }

    public static void rotate(List<?> list, int distance) {
        int size = list.size();
        if (size == 0) return;
        distance = ((distance % size) + size) % size;
        if (distance == 0) return;
        @SuppressWarnings("unchecked")
        List<Object> l = (List<Object>) list;
        Object[] a = l.toArray();
        for (int i = 0; i < size; i++) l.set((i + distance) % size, a[i]);
    }

    @SuppressWarnings("unchecked")
    public static <T extends Object & Comparable<? super T>> T min(Collection<? extends T> coll) {
        return min(coll, (Comparator<T>) (Comparator<?>) NATURAL);
    }

    public static <T> T min(Collection<? extends T> coll, Comparator<? super T> comp) {
        if (comp == null) comp = Collections.<T>naturalUnchecked();
        Iterator<? extends T> i = coll.iterator();
        T candidate = i.next();
        while (i.hasNext()) {
            T next = i.next();
            if (comp.compare(next, candidate) < 0) candidate = next;
        }
        return candidate;
    }

    @SuppressWarnings("unchecked")
    public static <T extends Object & Comparable<? super T>> T max(Collection<? extends T> coll) {
        return max(coll, (Comparator<T>) (Comparator<?>) NATURAL);
    }

    public static <T> T max(Collection<? extends T> coll, Comparator<? super T> comp) {
        if (comp == null) comp = Collections.<T>naturalUnchecked();
        Iterator<? extends T> i = coll.iterator();
        T candidate = i.next();
        while (i.hasNext()) {
            T next = i.next();
            if (comp.compare(next, candidate) > 0) candidate = next;
        }
        return candidate;
    }

    @SuppressWarnings("unchecked")
    private static <T> Comparator<T> naturalUnchecked() { return (Comparator<T>) NATURAL; }

    public static int frequency(Collection<?> c, Object o) {
        int n = 0;
        for (Object e : c) if (Objects.equals(o, e)) n++;
        return n;
    }

    public static boolean disjoint(Collection<?> c1, Collection<?> c2) {
        for (Object e : c1) if (c2.contains(e)) return false;
        return true;
    }

    @SafeVarargs
    public static <T> boolean addAll(Collection<? super T> c, T... elements) {
        boolean result = false;
        for (T element : elements) result |= c.add(element);
        return result;
    }

    @SuppressWarnings("unchecked")
    public static <T> int binarySearch(List<? extends Comparable<? super T>> list, T key) {
        return binarySearch((List<T>) list, key, (Comparator<T>) (Comparator<?>) NATURAL);
    }

    public static <T> int binarySearch(List<? extends T> list, T key, Comparator<? super T> c) {
        if (c == null) c = Collections.<T>naturalUnchecked();
        int low = 0, high = list.size() - 1;
        while (low <= high) {
            int mid = (low + high) >>> 1;
            int cmp = c.compare(list.get(mid), key);
            if (cmp < 0) low = mid + 1;
            else if (cmp > 0) high = mid - 1;
            else return mid;
        }
        return -(low + 1);
    }

    public static <T> List<T> nCopies(int n, T o) {
        if (n < 0) throw new IllegalArgumentException("List length = " + n);
        ArrayList<T> l = new ArrayList<T>(n);
        for (int i = 0; i < n; i++) l.add(o);
        return unmodifiableList(l);
    }

    public static <T> Enumeration<T> enumeration(final Collection<T> c) {
        final Iterator<T> i = c.iterator();
        return new Enumeration<T>() {
            public boolean hasMoreElements() { return i.hasNext(); }

            public T nextElement() { return i.next(); }
        };
    }

    public static <T> ArrayList<T> list(Enumeration<T> e) {
        ArrayList<T> l = new ArrayList<T>();
        while (e.hasMoreElements()) l.add(e.nextElement());
        return l;
    }

    @SuppressWarnings("unchecked")
    public static <T> List<T> emptyList() { return (List<T>) ImmutableCollections.listOf(); }

    @SuppressWarnings("unchecked")
    public static <T> Set<T> emptySet() { return unmodifiableSet(new HashSet<T>()); }

    public static <K, V> Map<K, V> emptyMap() { return unmodifiableMap(new HashMap<K, V>()); }

    public static <T> Iterator<T> emptyIterator() { return new ArrayList<T>().iterator(); }

    public static <T> List<T> singletonList(T o) {
        ArrayList<T> l = new ArrayList<T>(1);
        l.add(o);
        return unmodifiableList(l);
    }

    public static <T> Set<T> singleton(T o) {
        HashSet<T> s = new HashSet<T>();
        s.add(o);
        return unmodifiableSet(s);
    }

    public static <K, V> Map<K, V> singletonMap(K key, V value) {
        HashMap<K, V> m = new HashMap<K, V>();
        m.put(key, value);
        return unmodifiableMap(m);
    }

    public static <T> Collection<T> unmodifiableCollection(Collection<? extends T> c) { return new UnmodifiableCollection<T>(c); }

    public static <T> List<T> unmodifiableList(List<? extends T> list) { return new UnmodifiableList<T>(list); }

    public static <T> Set<T> unmodifiableSet(Set<? extends T> s) { return new UnmodifiableSet<T>(s); }

    public static <T> SortedSet<T> unmodifiableSortedSet(SortedSet<T> s) { return s; }

    public static <K, V> Map<K, V> unmodifiableMap(Map<? extends K, ? extends V> m) { return new UnmodifiableMap<K, V>(m); }

    public static <K, V> SortedMap<K, V> unmodifiableSortedMap(SortedMap<K, ? extends V> m) {
        @SuppressWarnings("unchecked")
        SortedMap<K, V> r = (SortedMap<K, V>) m;
        return r;
    }

    public static <T> Collection<T> synchronizedCollection(Collection<T> c) { return c; }

    public static <T> List<T> synchronizedList(List<T> list) { return list; }

    public static <T> Set<T> synchronizedSet(Set<T> s) { return s; }

    public static <K, V> Map<K, V> synchronizedMap(Map<K, V> m) { return m; }

    static class UnmodifiableCollection<E> implements Collection<E>, java.io.Serializable {
        final Collection<? extends E> c;

        UnmodifiableCollection(Collection<? extends E> c) {
            if (c == null) throw new NullPointerException();
            this.c = c;
        }

        public int size() { return c.size(); }

        public boolean isEmpty() { return c.isEmpty(); }

        public boolean contains(Object o) { return c.contains(o); }

        public Object[] toArray() { return c.toArray(); }

        public <T> T[] toArray(T[] a) { return c.toArray(a); }

        public String toString() { return c.toString(); }

        public Iterator<E> iterator() {
            return new Iterator<E>() {
                private final Iterator<? extends E> i = c.iterator();

                public boolean hasNext() { return i.hasNext(); }

                public E next() { return i.next(); }

                public void remove() { throw new UnsupportedOperationException(); }
            };
        }

        public boolean add(E e) { throw new UnsupportedOperationException(); }

        public boolean remove(Object o) { throw new UnsupportedOperationException(); }

        public boolean containsAll(Collection<?> coll) { return c.containsAll(coll); }

        public boolean addAll(Collection<? extends E> coll) { throw new UnsupportedOperationException(); }

        public boolean removeAll(Collection<?> coll) { throw new UnsupportedOperationException(); }

        public boolean retainAll(Collection<?> coll) { throw new UnsupportedOperationException(); }

        public void clear() { throw new UnsupportedOperationException(); }

        public boolean removeIf(java.util.function.Predicate<? super E> filter) { throw new UnsupportedOperationException(); }
    }

    static class UnmodifiableSet<E> extends UnmodifiableCollection<E> implements Set<E> {
        UnmodifiableSet(Set<? extends E> s) { super(s); }

        public boolean equals(Object o) { return o == this || c.equals(o); }

        public int hashCode() { return c.hashCode(); }
    }

    static class UnmodifiableList<E> extends UnmodifiableCollection<E> implements List<E>, RandomAccess {
        final List<? extends E> list;

        UnmodifiableList(List<? extends E> list) {
            super(list);
            this.list = list;
        }

        public boolean equals(Object o) { return o == this || list.equals(o); }

        public int hashCode() { return list.hashCode(); }

        public E get(int index) { return list.get(index); }

        public E set(int index, E element) { throw new UnsupportedOperationException(); }

        public void add(int index, E element) { throw new UnsupportedOperationException(); }

        public E remove(int index) { throw new UnsupportedOperationException(); }

        public int indexOf(Object o) { return list.indexOf(o); }

        public int lastIndexOf(Object o) { return list.lastIndexOf(o); }

        public boolean addAll(int index, Collection<? extends E> c) { throw new UnsupportedOperationException(); }

        public void replaceAll(java.util.function.UnaryOperator<E> operator) { throw new UnsupportedOperationException(); }

        public void sort(Comparator<? super E> c) { throw new UnsupportedOperationException(); }

        public ListIterator<E> listIterator() { return listIterator(0); }

        public ListIterator<E> listIterator(final int index) {
            return new ListIterator<E>() {
                private final ListIterator<? extends E> i = list.listIterator(index);

                public boolean hasNext() { return i.hasNext(); }

                public E next() { return i.next(); }

                public boolean hasPrevious() { return i.hasPrevious(); }

                public E previous() { return i.previous(); }

                public int nextIndex() { return i.nextIndex(); }

                public int previousIndex() { return i.previousIndex(); }

                public void remove() { throw new UnsupportedOperationException(); }

                public void set(E e) { throw new UnsupportedOperationException(); }

                public void add(E e) { throw new UnsupportedOperationException(); }
            };
        }

        public List<E> subList(int fromIndex, int toIndex) {
            return new UnmodifiableList<E>(list.subList(fromIndex, toIndex));
        }
    }

    static class UnmodifiableMap<K, V> implements Map<K, V>, java.io.Serializable {
        private final Map<? extends K, ? extends V> m;

        UnmodifiableMap(Map<? extends K, ? extends V> m) {
            if (m == null) throw new NullPointerException();
            this.m = m;
        }

        public int size() { return m.size(); }

        public boolean isEmpty() { return m.isEmpty(); }

        public boolean containsKey(Object key) { return m.containsKey(key); }

        public boolean containsValue(Object val) { return m.containsValue(val); }

        public V get(Object key) { return m.get(key); }

        public V put(K key, V value) { throw new UnsupportedOperationException(); }

        public V remove(Object key) { throw new UnsupportedOperationException(); }

        public void putAll(Map<? extends K, ? extends V> map) { throw new UnsupportedOperationException(); }

        public void clear() { throw new UnsupportedOperationException(); }

        @SuppressWarnings("unchecked")
        public Set<K> keySet() { return unmodifiableSet((Set<K>) m.keySet()); }

        @SuppressWarnings("unchecked")
        public Set<Map.Entry<K, V>> entrySet() {
            Set<Map.Entry<K, V>> s = (Set<Map.Entry<K, V>>) (Set<?>) m.entrySet();
            return unmodifiableSet(s);
        }

        @SuppressWarnings("unchecked")
        public Collection<V> values() { return unmodifiableCollection((Collection<V>) m.values()); }

        public boolean equals(Object o) { return o == this || m.equals(o); }

        public int hashCode() { return m.hashCode(); }

        public String toString() { return m.toString(); }

        public V putIfAbsent(K key, V value) { throw new UnsupportedOperationException(); }

        public V merge(K key, V value, java.util.function.BiFunction<? super V, ? super V, ? extends V> f) {
            throw new UnsupportedOperationException();
        }

        public V computeIfAbsent(K key, java.util.function.Function<? super K, ? extends V> f) {
            throw new UnsupportedOperationException();
        }

        public V compute(K key, java.util.function.BiFunction<? super K, ? super V, ? extends V> f) {
            throw new UnsupportedOperationException();
        }
    }
}
