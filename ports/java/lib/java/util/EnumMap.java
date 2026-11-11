package java.util;

/* keys kept in ordinal order, like OpenJDK's EnumMap */
public class EnumMap<K extends Enum<K>, V> extends AbstractMap<K, V> implements java.io.Serializable, Cloneable {
    private final Class<K> keyType;
    private final K[] keys;
    private final Object[] vals;
    private final boolean[] has;
    private int size;

    public EnumMap(Class<K> keyType) {
        this.keyType = keyType;
        keys = keyType.getEnumConstants();
        vals = new Object[keys.length];
        has = new boolean[keys.length];
    }

    public EnumMap(EnumMap<K, ? extends V> m) {
        this(m.keyType);
        putAll(m);
    }

    @SuppressWarnings("unchecked")
    public EnumMap(Map<K, ? extends V> m) {
        if (m instanceof EnumMap) {
            keyType = ((EnumMap<K, ?>) m).keyType;
        } else {
            if (m.isEmpty()) throw new IllegalArgumentException("Specified map is empty");
            keyType = m.keySet().iterator().next().getDeclaringClass();
        }
        keys = keyType.getEnumConstants();
        vals = new Object[keys.length];
        has = new boolean[keys.length];
        putAll(m);
    }

    private int idx(Object key) {
        if (key == null) throw new NullPointerException();
        if (!(key instanceof Enum) || ((Enum<?>) key).getDeclaringClass() != keyType) return -1;
        return ((Enum<?>) key).ordinal();
    }

    public int size() { return size; }

    public boolean containsKey(Object key) {
        int i = key == null ? -1 : idx(key);
        return i >= 0 && has[i];
    }

    @SuppressWarnings("unchecked")
    public V get(Object key) {
        int i = key == null ? -1 : idx(key);
        return i >= 0 ? (V) vals[i] : null;
    }

    @SuppressWarnings("unchecked")
    public V put(K key, V value) {
        int i = idx(key);
        V old = (V) vals[i];
        vals[i] = value;
        if (!has[i]) {
            has[i] = true;
            size++;
        }
        return old;
    }

    @SuppressWarnings("unchecked")
    public V remove(Object key) {
        int i = key == null ? -1 : idx(key);
        if (i < 0 || !has[i]) return null;
        V old = (V) vals[i];
        vals[i] = null;
        has[i] = false;
        size--;
        return old;
    }

    public void clear() {
        Arrays.fill(vals, null);
        Arrays.fill(has, false);
        size = 0;
    }

    public Set<Map.Entry<K, V>> entrySet() {
        return new AbstractSet<Map.Entry<K, V>>() {
            public int size() { return size; }

            public Iterator<Map.Entry<K, V>> iterator() {
                return new Iterator<Map.Entry<K, V>>() {
                    int next = advance(0), last = -1;

                    int advance(int i) {
                        while (i < keys.length && !has[i]) i++;
                        return i;
                    }

                    public boolean hasNext() { return next < keys.length; }

                    public Map.Entry<K, V> next() {
                        if (next >= keys.length) throw new NoSuchElementException();
                        final int i = last = next;
                        next = advance(next + 1);
                        return new Map.Entry<K, V>() {
                            public K getKey() { return keys[i]; }

                            @SuppressWarnings("unchecked")
                            public V getValue() { return (V) vals[i]; }

                            @SuppressWarnings("unchecked")
                            public V setValue(V v) {
                                V old = (V) vals[i];
                                vals[i] = v;
                                return old;
                            }

                            public boolean equals(Object o) {
                                if (!(o instanceof Map.Entry)) return false;
                                Map.Entry<?, ?> e = (Map.Entry<?, ?>) o;
                                return keys[i] == e.getKey() && Objects.equals(vals[i], e.getValue());
                            }

                            public int hashCode() { return keys[i].hashCode() ^ Objects.hashCode(vals[i]); }

                            public String toString() { return keys[i] + "=" + vals[i]; }
                        };
                    }

                    public void remove() {
                        if (last < 0) throw new IllegalStateException();
                        EnumMap.this.remove(keys[last]);
                        last = -1;
                    }
                };
            }
        };
    }

    public EnumMap<K, V> clone() { return new EnumMap<K, V>(this); }
}
