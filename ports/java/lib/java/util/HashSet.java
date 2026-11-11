package java.util;

public class HashSet<E> extends AbstractSet<E> implements Set<E>, Cloneable, java.io.Serializable {
    transient HashMap<E, Object> map;
    static final Object PRESENT = new Object();

    public HashSet() { map = new HashMap<E, Object>(); }

    public HashSet(Collection<? extends E> c) {
        map = new HashMap<E, Object>(Math.max((int) (c.size() / .75f) + 1, 16));
        addAll(c);
    }

    public HashSet(int initialCapacity, float loadFactor) { map = new HashMap<E, Object>(initialCapacity, loadFactor); }

    public HashSet(int initialCapacity) { map = new HashMap<E, Object>(initialCapacity); }

    HashSet(int initialCapacity, float loadFactor, boolean dummy) { map = new LinkedHashMap<E, Object>(initialCapacity, loadFactor); }

    public Iterator<E> iterator() { return map.keySet().iterator(); }

    public int size() { return map.size(); }

    public boolean isEmpty() { return map.isEmpty(); }

    public boolean contains(Object o) { return map.containsKey(o); }

    public boolean add(E e) { return map.put(e, PRESENT) == null; }

    public boolean remove(Object o) { return map.remove(o) == PRESENT; }

    public void clear() { map.clear(); }

    @SuppressWarnings("unchecked")
    public Object clone() {
        HashSet<E> s = new HashSet<E>(Math.max(size() * 2, 16));
        s.addAll(this);
        return s;
    }
}
