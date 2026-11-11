package java.util;

/* An AVL tree. headMap/tailMap/subMap/descendingMap return copies, not views. */
public class TreeMap<K, V> extends AbstractMap<K, V> implements NavigableMap<K, V>, Cloneable, java.io.Serializable {
    static final class Entry<K, V> implements Map.Entry<K, V> {
        K key;
        V value;
        Entry<K, V> left, right, parent;
        int height = 1;

        Entry(K key, V value, Entry<K, V> parent) {
            this.key = key;
            this.value = value;
            this.parent = parent;
        }

        public K getKey() { return key; }

        public V getValue() { return value; }

        public V setValue(V value) {
            V old = this.value;
            this.value = value;
            return old;
        }

        public boolean equals(Object o) {
            if (!(o instanceof Map.Entry)) return false;
            Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
            return Objects.equals(key, e.getKey()) && Objects.equals(value, e.getValue());
        }

        public int hashCode() { return Objects.hashCode(key) ^ Objects.hashCode(value); }

        public String toString() { return key + "=" + value; }
    }

    private final Comparator<? super K> comparator;
    private transient Entry<K, V> root;
    private transient int size, modCount;

    public TreeMap() { comparator = null; }

    public TreeMap(Comparator<? super K> comparator) { this.comparator = comparator; }

    public TreeMap(Map<? extends K, ? extends V> m) {
        comparator = (m instanceof SortedMap) ? ((SortedMap<K, ? extends V>) m).comparator() : null;
        putAll(m);
    }

    @SuppressWarnings("unchecked")
    final int compare(Object k1, Object k2) {
        return comparator == null ? ((Comparable<Object>) k1).compareTo(k2) : comparator.compare((K) k1, (K) k2);
    }

    public int size() { return size; }

    public Comparator<? super K> comparator() { return comparator; }

    final Entry<K, V> getEntry(Object key) {
        if (key == null && comparator == null) throw new NullPointerException();
        Entry<K, V> p = root;
        while (p != null) {
            int c = compare(key, p.key);
            if (c < 0) p = p.left;
            else if (c > 0) p = p.right;
            else return p;
        }
        return null;
    }

    public boolean containsKey(Object key) { return getEntry(key) != null; }

    public V get(Object key) {
        Entry<K, V> p = getEntry(key);
        return p == null ? null : p.value;
    }

    private static int h(Entry<?, ?> e) { return e == null ? 0 : e.height; }

    private static void fix(Entry<?, ?> e) { e.height = Math.max(h(e.left), h(e.right)) + 1; }

    private void replaceChild(Entry<K, V> parent, Entry<K, V> old, Entry<K, V> nw) {
        if (parent == null) root = nw;
        else if (parent.left == old) parent.left = nw;
        else parent.right = nw;
        if (nw != null) nw.parent = parent;
    }

    private Entry<K, V> rotateRight(Entry<K, V> y) {
        Entry<K, V> x = y.left;
        replaceChild(y.parent, y, x);
        y.left = x.right;
        if (x.right != null) x.right.parent = y;
        x.right = y;
        y.parent = x;
        fix(y);
        fix(x);
        return x;
    }

    private Entry<K, V> rotateLeft(Entry<K, V> x) {
        Entry<K, V> y = x.right;
        replaceChild(x.parent, x, y);
        x.right = y.left;
        if (y.left != null) y.left.parent = x;
        y.left = x;
        x.parent = y;
        fix(x);
        fix(y);
        return y;
    }

    private void rebalance(Entry<K, V> n) {
        while (n != null) {
            fix(n);
            int bal = h(n.left) - h(n.right);
            if (bal > 1) {
                if (h(n.left.left) < h(n.left.right)) rotateLeft(n.left);
                n = rotateRight(n);
            } else if (bal < -1) {
                if (h(n.right.right) < h(n.right.left)) rotateRight(n.right);
                n = rotateLeft(n);
            }
            n = n.parent;
        }
    }

    public V put(K key, V value) {
        Entry<K, V> t = root;
        if (t == null) {
            compare(key, key);
            root = new Entry<K, V>(key, value, null);
            size = 1;
            modCount++;
            return null;
        }
        Entry<K, V> parent;
        int c;
        do {
            parent = t;
            c = compare(key, t.key);
            if (c < 0) t = t.left;
            else if (c > 0) t = t.right;
            else return t.setValue(value);
        } while (t != null);
        Entry<K, V> e = new Entry<K, V>(key, value, parent);
        if (c < 0) parent.left = e;
        else parent.right = e;
        size++;
        modCount++;
        rebalance(parent);
        return null;
    }

    public V remove(Object key) {
        Entry<K, V> p = getEntry(key);
        if (p == null) return null;
        V old = p.value;
        deleteEntry(p);
        return old;
    }

    private void deleteEntry(Entry<K, V> p) {
        modCount++;
        size--;
        if (p.left != null && p.right != null) {
            /* swap p with its successor s in the tree structure (not the contents) */
            Entry<K, V> s = p.right;
            while (s.left != null) s = s.left;
            swapNodes(p, s);
        }
        Entry<K, V> child = p.left != null ? p.left : p.right;
        Entry<K, V> parent = p.parent;
        replaceChild(parent, p, child);
        p.left = p.right = p.parent = null;
        rebalance(parent);
    }

    private void swapNodes(Entry<K, V> a, Entry<K, V> b) {
        /* b is in a's right subtree and has no left child */
        Entry<K, V> ap = a.parent, al = a.left, ar = a.right;
        Entry<K, V> bp = b.parent, br = b.right;
        int ah = a.height;
        a.height = b.height;
        b.height = ah;
        replaceChild(ap, a, b);
        b.left = al;
        if (al != null) al.parent = b;
        if (bp == a) {
            b.right = a;
            a.parent = b;
        } else {
            b.right = ar;
            if (ar != null) ar.parent = b;
            bp.left = a;
            a.parent = bp;
        }
        a.left = null;
        a.right = br;
        if (br != null) br.parent = a;
    }

    public void clear() {
        modCount++;
        size = 0;
        root = null;
    }

    static <K, V> Entry<K, V> successor(Entry<K, V> t) {
        if (t == null) return null;
        if (t.right != null) {
            Entry<K, V> p = t.right;
            while (p.left != null) p = p.left;
            return p;
        }
        Entry<K, V> p = t.parent, ch = t;
        while (p != null && ch == p.right) {
            ch = p;
            p = p.parent;
        }
        return p;
    }

    static <K, V> Entry<K, V> predecessor(Entry<K, V> t) {
        if (t == null) return null;
        if (t.left != null) {
            Entry<K, V> p = t.left;
            while (p.right != null) p = p.right;
            return p;
        }
        Entry<K, V> p = t.parent, ch = t;
        while (p != null && ch == p.left) {
            ch = p;
            p = p.parent;
        }
        return p;
    }

    final Entry<K, V> firstNode() {
        Entry<K, V> p = root;
        if (p != null) while (p.left != null) p = p.left;
        return p;
    }

    final Entry<K, V> lastNode() {
        Entry<K, V> p = root;
        if (p != null) while (p.right != null) p = p.right;
        return p;
    }

    /* the entry closest to key: mode 0 = ceiling, 1 = higher, 2 = floor, 3 = lower */
    final Entry<K, V> find(Object key, int mode) {
        Entry<K, V> p = root, best = null;
        while (p != null) {
            int c = compare(key, p.key);
            if (c == 0 && (mode == 0 || mode == 2)) return p;
            if (mode <= 1) {
                if (c < 0) {
                    best = p;
                    p = p.left;
                } else p = p.right;
            } else {
                if (c > 0) {
                    best = p;
                    p = p.right;
                } else p = p.left;
            }
        }
        return best;
    }

    private static <K, V> Map.Entry<K, V> export(Entry<K, V> e) {
        return e == null ? null : new AbstractMap.SimpleImmutableEntry<K, V>(e.key, e.value);
    }

    private static <K> K key(Entry<K, ?> e) {
        if (e == null) throw new NoSuchElementException();
        return e.key;
    }

    private static <K> K keyOrNull(Entry<K, ?> e) { return e == null ? null : e.key; }

    public K firstKey() { return key(firstNode()); }

    public K lastKey() { return key(lastNode()); }

    public Map.Entry<K, V> firstEntry() { return export(firstNode()); }

    public Map.Entry<K, V> lastEntry() { return export(lastNode()); }

    public Map.Entry<K, V> pollFirstEntry() {
        Entry<K, V> p = firstNode();
        Map.Entry<K, V> r = export(p);
        if (p != null) deleteEntry(p);
        return r;
    }

    public Map.Entry<K, V> pollLastEntry() {
        Entry<K, V> p = lastNode();
        Map.Entry<K, V> r = export(p);
        if (p != null) deleteEntry(p);
        return r;
    }

    public Map.Entry<K, V> lowerEntry(K key) { return export(find(key, 3)); }

    public K lowerKey(K key) { return keyOrNull(find(key, 3)); }

    public Map.Entry<K, V> floorEntry(K key) { return export(find(key, 2)); }

    public K floorKey(K key) { return keyOrNull(find(key, 2)); }

    public Map.Entry<K, V> ceilingEntry(K key) { return export(find(key, 0)); }

    public K ceilingKey(K key) { return keyOrNull(find(key, 0)); }

    public Map.Entry<K, V> higherEntry(K key) { return export(find(key, 1)); }

    public K higherKey(K key) { return keyOrNull(find(key, 1)); }

    private TreeMap<K, V> range(Object lo, boolean loIncl, Object hi, boolean hiIncl, boolean useLo, boolean useHi) {
        TreeMap<K, V> m = new TreeMap<K, V>(comparator);
        for (Entry<K, V> e = firstNode(); e != null; e = successor(e)) {
            if (useLo) {
                int c = compare(e.key, lo);
                if (c < 0 || (c == 0 && !loIncl)) continue;
            }
            if (useHi) {
                int c = compare(e.key, hi);
                if (c > 0 || (c == 0 && !hiIncl)) break;
            }
            m.put(e.key, e.value);
        }
        return m;
    }

    public NavigableMap<K, V> subMap(K fromKey, boolean fromInclusive, K toKey, boolean toInclusive) {
        return range(fromKey, fromInclusive, toKey, toInclusive, true, true);
    }

    public NavigableMap<K, V> headMap(K toKey, boolean inclusive) { return range(null, false, toKey, inclusive, false, true); }

    public NavigableMap<K, V> tailMap(K fromKey, boolean inclusive) { return range(fromKey, inclusive, null, false, true, false); }

    public SortedMap<K, V> subMap(K fromKey, K toKey) { return subMap(fromKey, true, toKey, false); }

    public SortedMap<K, V> headMap(K toKey) { return headMap(toKey, false); }

    public SortedMap<K, V> tailMap(K fromKey) { return tailMap(fromKey, true); }

    public NavigableMap<K, V> descendingMap() {
        TreeMap<K, V> m = new TreeMap<K, V>(Collections.reverseOrder(comparator));
        m.putAll(this);
        return m;
    }

    public NavigableSet<K> navigableKeySet() { return new TreeSet<K>(this, true); }

    public NavigableSet<K> descendingKeySet() { return descendingMap().navigableKeySet(); }

    public Set<K> keySet() { return navigableKeySet(); }

    abstract class Itr<T> implements Iterator<T> {
        Entry<K, V> next = firstNode(), lastReturned;
        int expectedModCount = modCount;

        public boolean hasNext() { return next != null; }

        Entry<K, V> nextEntry() {
            Entry<K, V> e = next;
            if (e == null) throw new NoSuchElementException();
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            next = successor(e);
            lastReturned = e;
            return e;
        }

        public void remove() {
            if (lastReturned == null) throw new IllegalStateException();
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            deleteEntry(lastReturned);
            expectedModCount = modCount;
            lastReturned = null;
        }
    }

    Iterator<K> keyIterator() {
        return new Itr<K>() {
            public K next() { return nextEntry().key; }
        };
    }

    Iterator<K> descendingKeyIterator() {
        return new Iterator<K>() {
            Entry<K, V> next = lastNode(), lastReturned;

            public boolean hasNext() { return next != null; }

            public K next() {
                Entry<K, V> e = next;
                if (e == null) throw new NoSuchElementException();
                next = predecessor(e);
                lastReturned = e;
                return e.key;
            }

            public void remove() {
                if (lastReturned == null) throw new IllegalStateException();
                deleteEntry(lastReturned);
                lastReturned = null;
            }
        };
    }

    public Collection<V> values() {
        return new AbstractCollection<V>() {
            public Iterator<V> iterator() {
                return new Itr<V>() {
                    public V next() { return nextEntry().value; }
                };
            }

            public int size() { return size; }

            public void clear() { TreeMap.this.clear(); }
        };
    }

    public Set<Map.Entry<K, V>> entrySet() {
        return new AbstractSet<Map.Entry<K, V>>() {
            public Iterator<Map.Entry<K, V>> iterator() {
                return new Itr<Map.Entry<K, V>>() {
                    public Map.Entry<K, V> next() { return nextEntry(); }
                };
            }

            public int size() { return size; }

            public void clear() { TreeMap.this.clear(); }

            public boolean contains(Object o) {
                if (!(o instanceof Map.Entry)) return false;
                Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
                Entry<K, V> p = getEntry(e.getKey());
                return p != null && Objects.equals(p.value, e.getValue());
            }

            public boolean remove(Object o) {
                if (!contains(o)) return false;
                deleteEntry(getEntry(((Map.Entry<?, ?>) o).getKey()));
                return true;
            }
        };
    }

    public void forEach(java.util.function.BiConsumer<? super K, ? super V> action) {
        int mc = modCount;
        for (Entry<K, V> e = firstNode(); e != null; e = successor(e)) {
            action.accept(e.key, e.value);
            if (mc != modCount) throw new ConcurrentModificationException();
        }
    }

    public Object clone() { return new TreeMap<K, V>(this); }
}
