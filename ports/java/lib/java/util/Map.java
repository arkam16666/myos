package java.util;

import java.util.function.BiConsumer;
import java.util.function.BiFunction;
import java.util.function.Function;

public interface Map<K, V> {
    int size();

    boolean isEmpty();

    boolean containsKey(Object key);

    boolean containsValue(Object value);

    V get(Object key);

    V put(K key, V value);

    V remove(Object key);

    void putAll(Map<? extends K, ? extends V> m);

    void clear();

    Set<K> keySet();

    Collection<V> values();

    Set<Map.Entry<K, V>> entrySet();

    boolean equals(Object o);

    int hashCode();

    interface Entry<K, V> {
        K getKey();

        V getValue();

        V setValue(V value);

        boolean equals(Object o);

        int hashCode();

        @SuppressWarnings("unchecked")
        public static <K extends Comparable<? super K>, V> Comparator<Map.Entry<K, V>> comparingByKey() {
            return new Comparator<Map.Entry<K, V>>() {
                public int compare(Map.Entry<K, V> a, Map.Entry<K, V> b) { return a.getKey().compareTo(b.getKey()); }
            };
        }

        public static <K, V extends Comparable<? super V>> Comparator<Map.Entry<K, V>> comparingByValue() {
            return new Comparator<Map.Entry<K, V>>() {
                public int compare(Map.Entry<K, V> a, Map.Entry<K, V> b) { return a.getValue().compareTo(b.getValue()); }
            };
        }

        public static <K, V> Comparator<Map.Entry<K, V>> comparingByKey(final Comparator<? super K> cmp) {
            return new Comparator<Map.Entry<K, V>>() {
                public int compare(Map.Entry<K, V> a, Map.Entry<K, V> b) { return cmp.compare(a.getKey(), b.getKey()); }
            };
        }

        public static <K, V> Comparator<Map.Entry<K, V>> comparingByValue(final Comparator<? super V> cmp) {
            return new Comparator<Map.Entry<K, V>>() {
                public int compare(Map.Entry<K, V> a, Map.Entry<K, V> b) { return cmp.compare(a.getValue(), b.getValue()); }
            };
        }

        @SuppressWarnings("unchecked")
        public static <K, V> Map.Entry<K, V> copyOf(Map.Entry<? extends K, ? extends V> e) {
            return new AbstractMap.SimpleImmutableEntry<K, V>(e.getKey(), e.getValue());
        }
    }

    default V getOrDefault(Object key, V defaultValue) {
        V v = get(key);
        return (v != null || containsKey(key)) ? v : defaultValue;
    }

    default void forEach(BiConsumer<? super K, ? super V> action) {
        for (Map.Entry<K, V> e : entrySet()) action.accept(e.getKey(), e.getValue());
    }

    default void replaceAll(BiFunction<? super K, ? super V, ? extends V> function) {
        for (Map.Entry<K, V> e : entrySet()) e.setValue(function.apply(e.getKey(), e.getValue()));
    }

    default V putIfAbsent(K key, V value) {
        V v = get(key);
        if (v == null) v = put(key, value);
        return v;
    }

    default boolean remove(Object key, Object value) {
        Object cur = get(key);
        if (!Objects.equals(cur, value) || (cur == null && !containsKey(key))) return false;
        remove(key);
        return true;
    }

    default boolean replace(K key, V oldValue, V newValue) {
        Object cur = get(key);
        if (!Objects.equals(cur, oldValue) || (cur == null && !containsKey(key))) return false;
        put(key, newValue);
        return true;
    }

    default V replace(K key, V value) {
        V cur = get(key);
        if (cur != null || containsKey(key)) cur = put(key, value);
        return cur;
    }

    default V computeIfAbsent(K key, Function<? super K, ? extends V> mappingFunction) {
        V v = get(key);
        if (v == null) {
            V nv = mappingFunction.apply(key);
            if (nv != null) {
                put(key, nv);
                return nv;
            }
        }
        return v;
    }

    default V computeIfPresent(K key, BiFunction<? super K, ? super V, ? extends V> remappingFunction) {
        V old = get(key);
        if (old != null) {
            V nv = remappingFunction.apply(key, old);
            if (nv != null) {
                put(key, nv);
                return nv;
            }
            remove(key);
        }
        return null;
    }

    default V compute(K key, BiFunction<? super K, ? super V, ? extends V> remappingFunction) {
        V old = get(key);
        V nv = remappingFunction.apply(key, old);
        if (nv == null) {
            if (old != null || containsKey(key)) remove(key);
            return null;
        }
        put(key, nv);
        return nv;
    }

    default V merge(K key, V value, BiFunction<? super V, ? super V, ? extends V> remappingFunction) {
        V old = get(key);
        V nv = (old == null) ? value : remappingFunction.apply(old, value);
        if (nv == null) remove(key);
        else put(key, nv);
        return nv;
    }

    static <K, V> Map<K, V> of() { return ImmutableCollections.mapOf(); }

    static <K, V> Map<K, V> of(K k1, V v1) { return ImmutableCollections.mapOf(k1, v1); }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2) { return ImmutableCollections.mapOf(k1, v1, k2, v2); }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3) { return ImmutableCollections.mapOf(k1, v1, k2, v2, k3, v3); }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4) {
        return ImmutableCollections.mapOf(k1, v1, k2, v2, k3, v3, k4, v4);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4, K k5, V v5) {
        return ImmutableCollections.mapOf(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4, K k5, V v5, K k6, V v6) {
        return ImmutableCollections.mapOf(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5, k6, v6);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4, K k5, V v5, K k6, V v6, K k7, V v7) {
        return ImmutableCollections.mapOf(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5, k6, v6, k7, v7);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4, K k5, V v5, K k6, V v6, K k7, V v7,
                               K k8, V v8) {
        return ImmutableCollections.mapOf(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5, k6, v6, k7, v7, k8, v8);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4, K k5, V v5, K k6, V v6, K k7, V v7,
                               K k8, V v8, K k9, V v9) {
        return ImmutableCollections.mapOf(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5, k6, v6, k7, v7, k8, v8, k9, v9);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4, K k5, V v5, K k6, V v6, K k7, V v7,
                               K k8, V v8, K k9, V v9, K k10, V v10) {
        return ImmutableCollections.mapOf(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5, k6, v6, k7, v7, k8, v8, k9, v9, k10, v10);
    }

    @SafeVarargs
    @SuppressWarnings("unchecked")
    static <K, V> Map<K, V> ofEntries(Map.Entry<? extends K, ? extends V>... entries) {
        Object[] kv = new Object[entries.length * 2];
        for (int i = 0; i < entries.length; i++) {
            kv[2 * i] = entries[i].getKey();
            kv[2 * i + 1] = entries[i].getValue();
        }
        return (Map<K, V>) ImmutableCollections.mapOf(kv);
    }

    static <K, V> Map.Entry<K, V> entry(K k, V v) {
        if (k == null || v == null) throw new NullPointerException();
        return new AbstractMap.SimpleImmutableEntry<K, V>(k, v);
    }

    @SuppressWarnings("unchecked")
    static <K, V> Map<K, V> copyOf(Map<? extends K, ? extends V> map) {
        Object[] kv = new Object[map.size() * 2];
        int i = 0;
        for (Map.Entry<? extends K, ? extends V> e : map.entrySet()) {
            kv[i++] = e.getKey();
            kv[i++] = e.getValue();
        }
        return (Map<K, V>) ImmutableCollections.mapOf(kv);
    }
}
