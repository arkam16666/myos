package java.util.concurrent;

/** One thread on MyOS: a HashMap that refuses nulls, like the real one. */
public class ConcurrentHashMap<K, V> extends java.util.HashMap<K, V> implements ConcurrentMap<K, V> {
    public ConcurrentHashMap() { super(); }

    public ConcurrentHashMap(int initialCapacity) { super(initialCapacity); }

    public ConcurrentHashMap(java.util.Map<? extends K, ? extends V> m) { super(m); }

    public V put(K key, V value) {
        if (key == null || value == null) throw new NullPointerException();
        return super.put(key, value);
    }

    public long mappingCount() { return size(); }
}
