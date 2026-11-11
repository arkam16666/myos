package java.util;

public class TreeSet<E> extends AbstractSet<E> implements NavigableSet<E>, Cloneable, java.io.Serializable {
    private final TreeMap<E, Object> m;
    private static final Object PRESENT = new Object();

    @SuppressWarnings("unchecked")
    TreeSet(TreeMap<E, ?> map, boolean view) { m = (TreeMap<E, Object>) map; }

    public TreeSet() { m = new TreeMap<E, Object>(); }

    public TreeSet(Comparator<? super E> comparator) { m = new TreeMap<E, Object>(comparator); }

    public TreeSet(Collection<? extends E> c) {
        this(c instanceof SortedSet ? ((SortedSet<E>) c).comparator() : null);
        addAll(c);
    }

    public TreeSet(SortedSet<E> s) {
        this(s.comparator());
        addAll(s);
    }

    public Iterator<E> iterator() { return m.keyIterator(); }

    public Iterator<E> descendingIterator() { return m.descendingKeyIterator(); }

    public NavigableSet<E> descendingSet() {
        TreeSet<E> s = new TreeSet<E>(Collections.reverseOrder(m.comparator()));
        s.addAll(this);
        return s;
    }

    public int size() { return m.size(); }

    public boolean isEmpty() { return m.isEmpty(); }

    public boolean contains(Object o) { return m.containsKey(o); }

    public boolean add(E e) { return m.put(e, PRESENT) == null; }

    public boolean remove(Object o) { return m.remove(o) == PRESENT; }

    public void clear() { m.clear(); }

    private TreeSet<E> copyOf(NavigableMap<E, ?> part) {
        TreeSet<E> s = new TreeSet<E>(m.comparator());
        s.addAll(part.keySet());
        return s;
    }

    public NavigableSet<E> subSet(E fromElement, boolean fromInclusive, E toElement, boolean toInclusive) {
        return copyOf(m.subMap(fromElement, fromInclusive, toElement, toInclusive));
    }

    public NavigableSet<E> headSet(E toElement, boolean inclusive) { return copyOf(m.headMap(toElement, inclusive)); }

    public NavigableSet<E> tailSet(E fromElement, boolean inclusive) { return copyOf(m.tailMap(fromElement, inclusive)); }

    public SortedSet<E> subSet(E fromElement, E toElement) { return subSet(fromElement, true, toElement, false); }

    public SortedSet<E> headSet(E toElement) { return headSet(toElement, false); }

    public SortedSet<E> tailSet(E fromElement) { return tailSet(fromElement, true); }

    public Comparator<? super E> comparator() { return m.comparator(); }

    public E first() { return m.firstKey(); }

    public E last() { return m.lastKey(); }

    public E lower(E e) { return m.lowerKey(e); }

    public E floor(E e) { return m.floorKey(e); }

    public E ceiling(E e) { return m.ceilingKey(e); }

    public E higher(E e) { return m.higherKey(e); }

    public E pollFirst() {
        Map.Entry<E, ?> e = m.pollFirstEntry();
        return (e == null) ? null : e.getKey();
    }

    public E pollLast() {
        Map.Entry<E, ?> e = m.pollLastEntry();
        return (e == null) ? null : e.getKey();
    }

    public E getFirst() { return first(); }

    public E getLast() { return last(); }

    public E removeFirst() {
        E e = first();
        remove(e);
        return e;
    }

    public E removeLast() {
        E e = last();
        remove(e);
        return e;
    }

    public Object clone() { return new TreeSet<E>((SortedSet<E>) this); }
}
