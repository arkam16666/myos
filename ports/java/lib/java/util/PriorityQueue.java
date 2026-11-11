package java.util;

public class PriorityQueue<E> extends AbstractQueue<E> implements java.io.Serializable {
    Object[] queue;
    int size;
    private final Comparator<? super E> comparator;
    int modCount;

    public PriorityQueue() { this(11, null); }

    public PriorityQueue(int initialCapacity) { this(initialCapacity, null); }

    public PriorityQueue(Comparator<? super E> comparator) { this(11, comparator); }

    public PriorityQueue(int initialCapacity, Comparator<? super E> comparator) {
        if (initialCapacity < 1) throw new IllegalArgumentException();
        this.queue = new Object[initialCapacity];
        this.comparator = comparator;
    }

    @SuppressWarnings("unchecked")
    public PriorityQueue(Collection<? extends E> c) {
        if (c instanceof SortedSet) comparator = (Comparator<? super E>) ((SortedSet<? extends E>) c).comparator();
        else if (c instanceof PriorityQueue) comparator = (Comparator<? super E>) ((PriorityQueue<? extends E>) c).comparator();
        else comparator = null;
        queue = c.toArray();
        size = queue.length;
        if (queue.length == 0) queue = new Object[1];
        for (int i = (size >>> 1) - 1; i >= 0; i--) siftDown(i, queue[i]);
    }

    public PriorityQueue(PriorityQueue<? extends E> c) { this((Collection<? extends E>) c); }

    public PriorityQueue(SortedSet<? extends E> c) { this((Collection<? extends E>) c); }

    @SuppressWarnings("unchecked")
    private int cmp(Object a, Object b) {
        return comparator == null ? ((Comparable<Object>) a).compareTo(b) : comparator.compare((E) a, (E) b);
    }

    public boolean add(E e) { return offer(e); }

    public boolean offer(E e) {
        if (e == null) throw new NullPointerException();
        modCount++;
        int i = size;
        if (i >= queue.length) queue = Arrays.copyOf(queue, queue.length < 64 ? queue.length * 2 + 2 : queue.length + (queue.length >> 1));
        siftUp(i, e);
        size = i + 1;
        return true;
    }

    @SuppressWarnings("unchecked")
    public E peek() { return size == 0 ? null : (E) queue[0]; }

    private int indexOf(Object o) {
        if (o != null) for (int i = 0; i < size; i++) if (o.equals(queue[i])) return i;
        return -1;
    }

    public boolean remove(Object o) {
        int i = indexOf(o);
        if (i == -1) return false;
        removeAt(i);
        return true;
    }

    public boolean contains(Object o) { return indexOf(o) >= 0; }

    public Object[] toArray() { return Arrays.copyOf(queue, size); }

    public Iterator<E> iterator() {
        return new Iterator<E>() {
            int cursor, lastRet = -1, expectedModCount = modCount;
            ArrayDeque<E> forgetMeNot;
            E lastRetElt;

            public boolean hasNext() { return cursor < size || (forgetMeNot != null && !forgetMeNot.isEmpty()); }

            @SuppressWarnings("unchecked")
            public E next() {
                if (expectedModCount != modCount) throw new ConcurrentModificationException();
                if (cursor < size) return (E) queue[lastRet = cursor++];
                if (forgetMeNot != null) {
                    lastRet = -1;
                    lastRetElt = forgetMeNot.poll();
                    if (lastRetElt != null) return lastRetElt;
                }
                throw new NoSuchElementException();
            }

            public void remove() {
                if (expectedModCount != modCount) throw new ConcurrentModificationException();
                if (lastRet != -1) {
                    E moved = PriorityQueue.this.removeAt(lastRet);
                    lastRet = -1;
                    if (moved == null) cursor--;
                    else {
                        if (forgetMeNot == null) forgetMeNot = new ArrayDeque<E>();
                        forgetMeNot.add(moved);
                    }
                } else if (lastRetElt != null) {
                    PriorityQueue.this.removeEq(lastRetElt);
                    lastRetElt = null;
                } else throw new IllegalStateException();
                expectedModCount = modCount;
            }
        };
    }

    void removeEq(Object o) {
        for (int i = 0; i < size; i++) {
            if (o == queue[i]) {
                removeAt(i);
                break;
            }
        }
    }

    public int size() { return size; }

    public void clear() {
        modCount++;
        for (int i = 0; i < size; i++) queue[i] = null;
        size = 0;
    }

    @SuppressWarnings("unchecked")
    public E poll() {
        if (size == 0) return null;
        int s = --size;
        modCount++;
        E result = (E) queue[0];
        Object x = queue[s];
        queue[s] = null;
        if (s != 0) siftDown(0, x);
        return result;
    }

    @SuppressWarnings("unchecked")
    E removeAt(int i) {
        modCount++;
        int s = --size;
        if (s == i) queue[i] = null;
        else {
            Object moved = queue[s];
            queue[s] = null;
            siftDown(i, moved);
            if (queue[i] == moved) {
                siftUp(i, moved);
                if (queue[i] != moved) return (E) moved;
            }
        }
        return null;
    }

    private void siftUp(int k, Object x) {
        while (k > 0) {
            int parent = (k - 1) >>> 1;
            Object e = queue[parent];
            if (cmp(x, e) >= 0) break;
            queue[k] = e;
            k = parent;
        }
        queue[k] = x;
    }

    private void siftDown(int k, Object x) {
        int half = size >>> 1;
        while (k < half) {
            int child = (k << 1) + 1;
            Object c = queue[child];
            int right = child + 1;
            if (right < size && cmp(c, queue[right]) > 0) c = queue[child = right];
            if (cmp(x, c) <= 0) break;
            queue[k] = c;
            k = child;
        }
        queue[k] = x;
    }

    public Comparator<? super E> comparator() { return comparator; }
}
