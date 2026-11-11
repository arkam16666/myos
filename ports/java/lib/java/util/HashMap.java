package java.util;

import java.util.function.BiConsumer;
import java.util.function.BiFunction;
import java.util.function.Function;

/* Same table layout and hashing as OpenJDK's HashMap (without tree bins), so
 * iteration order matches what Java programmers see on a PC. */
public class HashMap<K, V> extends AbstractMap<K, V> implements Map<K, V>, Cloneable, java.io.Serializable {
    static class Node<K, V> implements Map.Entry<K, V> {
        final int hash;
        final K key;
        V value;
        Node<K, V> next;

        Node(int hash, K key, V value, Node<K, V> next) {
            this.hash = hash;
            this.key = key;
            this.value = value;
            this.next = next;
        }

        public final K getKey() { return key; }

        public final V getValue() { return value; }

        public final String toString() { return key + "=" + value; }

        public final int hashCode() { return Objects.hashCode(key) ^ Objects.hashCode(value); }

        public final V setValue(V newValue) {
            V old = value;
            value = newValue;
            return old;
        }

        public final boolean equals(Object o) {
            if (o == this) return true;
            if (!(o instanceof Map.Entry)) return false;
            Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
            return Objects.equals(key, e.getKey()) && Objects.equals(value, e.getValue());
        }
    }

    static final int hash(Object key) {
        int h;
        return (key == null) ? 0 : (h = key.hashCode()) ^ (h >>> 16);
    }

    static final int tableSizeFor(int cap) {
        int n = -1 >>> Integer.numberOfLeadingZeros(cap - 1);
        return (n < 0) ? 1 : (n >= (1 << 30)) ? (1 << 30) : n + 1;
    }

    transient Node<K, V>[] table;
    transient int size;
    transient int modCount;
    int threshold;
    final float loadFactor;

    public HashMap(int initialCapacity, float loadFactor) {
        if (initialCapacity < 0) throw new IllegalArgumentException("Illegal initial capacity: " + initialCapacity);
        if (initialCapacity > (1 << 30)) initialCapacity = 1 << 30;
        if (loadFactor <= 0 || loadFactor != loadFactor) throw new IllegalArgumentException("Illegal load factor: " + loadFactor);
        this.loadFactor = loadFactor;
        this.threshold = tableSizeFor(initialCapacity);
    }

    public HashMap(int initialCapacity) { this(initialCapacity, 0.75f); }

    public HashMap() { this.loadFactor = 0.75f; }

    public HashMap(Map<? extends K, ? extends V> m) {
        this.loadFactor = 0.75f;
        putMapEntries(m);
    }

    final void putMapEntries(Map<? extends K, ? extends V> m) {
        int s = m.size();
        if (s > 0) {
            if (table == null) {
                double dt = Math.ceil(s / (double) loadFactor);
                int t = ((dt < (double) (1 << 30)) ? (int) dt : (1 << 30));
                if (t > threshold) threshold = tableSizeFor(t);
            } else {
                while (s > threshold && table.length < (1 << 30)) resize();
            }
            for (Map.Entry<? extends K, ? extends V> e : m.entrySet()) putVal(hash(e.getKey()), e.getKey(), e.getValue(), false);
        }
    }

    public int size() { return size; }

    public boolean isEmpty() { return size == 0; }

    public V get(Object key) {
        Node<K, V> e = getNode(key);
        return e == null ? null : e.value;
    }

    final Node<K, V> getNode(Object key) {
        Node<K, V>[] tab = table;
        if (tab == null) return null;
        int h = hash(key);
        for (Node<K, V> e = tab[(tab.length - 1) & h]; e != null; e = e.next) {
            Object k;
            if (e.hash == h && ((k = e.key) == key || (key != null && key.equals(k)))) return e;
        }
        return null;
    }

    public boolean containsKey(Object key) { return getNode(key) != null; }

    public V put(K key, V value) { return putVal(hash(key), key, value, false); }

    final V putVal(int hash, K key, V value, boolean onlyIfAbsent) {
        Node<K, V>[] tab = table;
        if (tab == null || tab.length == 0) tab = resize();
        int n = tab.length, i = (n - 1) & hash;
        Node<K, V> p = tab[i];
        if (p == null) tab[i] = newNode(hash, key, value, null);
        else {
            Node<K, V> e = null;
            for (;;) {
                Object k;
                if (p.hash == hash && ((k = p.key) == key || (key != null && key.equals(k)))) {
                    e = p;
                    break;
                }
                if (p.next == null) {
                    p.next = newNode(hash, key, value, null);
                    break;
                }
                p = p.next;
            }
            if (e != null) {
                V old = e.value;
                if (!onlyIfAbsent || old == null) e.value = value;
                afterNodeAccess(e);
                return old;
            }
        }
        ++modCount;
        if (++size > threshold) resize();
        afterNodeInsertion(true);
        return null;
    }

    @SuppressWarnings("unchecked")
    final Node<K, V>[] resize() {
        Node<K, V>[] oldTab = table;
        int oldCap = (oldTab == null) ? 0 : oldTab.length;
        int oldThr = threshold;
        int newCap, newThr = 0;
        if (oldCap > 0) {
            if (oldCap >= (1 << 30)) {
                threshold = Integer.MAX_VALUE;
                return oldTab;
            } else if ((newCap = oldCap << 1) < (1 << 30) && oldCap >= 16) newThr = oldThr << 1;
        } else if (oldThr > 0) newCap = oldThr;
        else {
            newCap = 16;
            newThr = 12;
        }
        if (newThr == 0) {
            float ft = (float) newCap * loadFactor;
            newThr = (newCap < (1 << 30) && ft < (float) (1 << 30) ? (int) ft : Integer.MAX_VALUE);
        }
        threshold = newThr;
        Node<K, V>[] newTab = (Node<K, V>[]) new Node[newCap];
        table = newTab;
        if (oldTab != null) {
            for (int j = 0; j < oldCap; ++j) {
                Node<K, V> e = oldTab[j];
                if (e == null) continue;
                oldTab[j] = null;
                if (e.next == null) newTab[e.hash & (newCap - 1)] = e;
                else {
                    Node<K, V> loHead = null, loTail = null, hiHead = null, hiTail = null, next;
                    do {
                        next = e.next;
                        if ((e.hash & oldCap) == 0) {
                            if (loTail == null) loHead = e;
                            else loTail.next = e;
                            loTail = e;
                        } else {
                            if (hiTail == null) hiHead = e;
                            else hiTail.next = e;
                            hiTail = e;
                        }
                    } while ((e = next) != null);
                    if (loTail != null) {
                        loTail.next = null;
                        newTab[j] = loHead;
                    }
                    if (hiTail != null) {
                        hiTail.next = null;
                        newTab[j + oldCap] = hiHead;
                    }
                }
            }
        }
        return newTab;
    }

    public void putAll(Map<? extends K, ? extends V> m) { putMapEntries(m); }

    public V remove(Object key) {
        Node<K, V> e = removeNode(hash(key), key);
        return e == null ? null : e.value;
    }

    final Node<K, V> removeNode(int hash, Object key) {
        Node<K, V>[] tab = table;
        if (tab == null) return null;
        int index = (tab.length - 1) & hash;
        Node<K, V> prev = null;
        for (Node<K, V> e = tab[index]; e != null; prev = e, e = e.next) {
            Object k;
            if (e.hash == hash && ((k = e.key) == key || (key != null && key.equals(k)))) {
                if (prev == null) tab[index] = e.next;
                else prev.next = e.next;
                ++modCount;
                --size;
                afterNodeRemoval(e);
                return e;
            }
        }
        return null;
    }

    public void clear() {
        modCount++;
        if (table != null && size > 0) {
            size = 0;
            for (int i = 0; i < table.length; ++i) table[i] = null;
        }
        afterClear();
    }

    public boolean containsValue(Object value) {
        for (Map.Entry<K, V> e : entrySet()) if (Objects.equals(value, e.getValue())) return true;
        return false;
    }

    Node<K, V> newNode(int hash, K key, V value, Node<K, V> next) { return new Node<K, V>(hash, key, value, next); }

    void afterNodeAccess(Node<K, V> p) {}

    void afterNodeInsertion(boolean evict) {}

    void afterNodeRemoval(Node<K, V> p) {}

    void afterClear() {}

    /* iteration over the nodes; LinkedHashMap overrides */
    Iterator<Node<K, V>> nodeIterator() { return new HashIterator(); }

    final class HashIterator implements Iterator<Node<K, V>> {
        Node<K, V> next, current;
        int expectedModCount, index;

        HashIterator() {
            expectedModCount = modCount;
            Node<K, V>[] t = table;
            if (t != null && size > 0) {
                while (index < t.length && (next = t[index++]) == null) {}
            }
        }

        public final boolean hasNext() { return next != null; }

        public final Node<K, V> next() {
            Node<K, V>[] t;
            Node<K, V> e = next;
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            if (e == null) throw new NoSuchElementException();
            if ((next = (current = e).next) == null && (t = table) != null) {
                while (index < t.length && (next = t[index++]) == null) {}
            }
            return e;
        }

        public final void remove() {
            Node<K, V> p = current;
            if (p == null) throw new IllegalStateException();
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            current = null;
            removeNode(p.hash, p.key);
            expectedModCount = modCount;
        }
    }

    public Set<K> keySet() {
        return new AbstractSet<K>() {
            public int size() { return size; }

            public void clear() { HashMap.this.clear(); }

            public boolean contains(Object o) { return containsKey(o); }

            public boolean remove(Object key) { return removeNode(hash(key), key) != null; }

            public Iterator<K> iterator() {
                final Iterator<Node<K, V>> it = nodeIterator();
                return new Iterator<K>() {
                    public boolean hasNext() { return it.hasNext(); }

                    public K next() { return it.next().key; }

                    public void remove() { it.remove(); }
                };
            }
        };
    }

    public Collection<V> values() {
        return new AbstractCollection<V>() {
            public int size() { return size; }

            public void clear() { HashMap.this.clear(); }

            public boolean contains(Object o) { return containsValue(o); }

            public Iterator<V> iterator() {
                final Iterator<Node<K, V>> it = nodeIterator();
                return new Iterator<V>() {
                    public boolean hasNext() { return it.hasNext(); }

                    public V next() { return it.next().value; }

                    public void remove() { it.remove(); }
                };
            }
        };
    }

    public Set<Map.Entry<K, V>> entrySet() {
        return new AbstractSet<Map.Entry<K, V>>() {
            public int size() { return size; }

            public void clear() { HashMap.this.clear(); }

            public boolean contains(Object o) {
                if (!(o instanceof Map.Entry)) return false;
                Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
                Node<K, V> candidate = getNode(e.getKey());
                return candidate != null && candidate.equals(e);
            }

            public boolean remove(Object o) {
                if (contains(o)) {
                    Object key = ((Map.Entry<?, ?>) o).getKey();
                    return removeNode(hash(key), key) != null;
                }
                return false;
            }

            public Iterator<Map.Entry<K, V>> iterator() {
                final Iterator<Node<K, V>> it = nodeIterator();
                return new Iterator<Map.Entry<K, V>>() {
                    public boolean hasNext() { return it.hasNext(); }

                    public Map.Entry<K, V> next() { return it.next(); }

                    public void remove() { it.remove(); }
                };
            }
        };
    }

    public V getOrDefault(Object key, V defaultValue) {
        Node<K, V> e = getNode(key);
        return e == null ? defaultValue : e.value;
    }

    public V putIfAbsent(K key, V value) { return putVal(hash(key), key, value, true); }

    public V computeIfAbsent(K key, Function<? super K, ? extends V> mappingFunction) {
        Node<K, V> e = getNode(key);
        if (e != null && e.value != null) {
            afterNodeAccess(e);
            return e.value;
        }
        int mc = modCount;
        V v = mappingFunction.apply(key);
        if (mc != modCount) throw new ConcurrentModificationException();
        if (v == null) return null;
        if (e != null) {
            e.value = v;
            afterNodeAccess(e);
        } else putVal(hash(key), key, v, false);
        return v;
    }

    public V merge(K key, V value, BiFunction<? super V, ? super V, ? extends V> remappingFunction) {
        if (value == null || remappingFunction == null) throw new NullPointerException();
        Node<K, V> e = getNode(key);
        if (e != null) {
            V v = e.value != null ? remappingFunction.apply(e.value, value) : value;
            if (v != null) {
                e.value = v;
                afterNodeAccess(e);
            } else removeNode(hash(key), key);
            return v;
        }
        putVal(hash(key), key, value, false);
        return value;
    }

    public void forEach(BiConsumer<? super K, ? super V> action) {
        int mc = modCount;
        for (Iterator<Node<K, V>> it = nodeIterator(); it.hasNext();) {
            Node<K, V> e = it.next();
            action.accept(e.key, e.value);
        }
        if (modCount != mc) throw new ConcurrentModificationException();
    }

    @SuppressWarnings("unchecked")
    public Object clone() {
        HashMap<K, V> m = new HashMap<K, V>(Math.max((int) (size / loadFactor) + 1, 16), loadFactor);
        m.putMapEntries(this);
        return m;
    }
}
