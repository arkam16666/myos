package java.util;

public class Hashtable<K, V> extends HashMap<K, V> {
    public Hashtable() { super(11); }

    public Hashtable(int initialCapacity) { super(initialCapacity); }

    public Hashtable(Map<? extends K, ? extends V> t) { super(t); }

    public V put(K key, V value) {
        if (key == null || value == null) throw new NullPointerException();
        return super.put(key, value);
    }

    public Enumeration<K> keys() { return Collections.enumeration(keySet()); }

    public Enumeration<V> elements() { return Collections.enumeration(values()); }

    public boolean contains(Object value) { return containsValue(value); }
}
