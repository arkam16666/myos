package java.util;

public class ArrayDeque<E> extends AbstractCollection<E> implements Deque<E>, Cloneable, java.io.Serializable {
    Object[] elements;
    int head, size;

    public ArrayDeque() { elements = new Object[16]; }

    public ArrayDeque(int numElements) { elements = new Object[Math.max(numElements, 1) + 1]; }

    public ArrayDeque(Collection<? extends E> c) {
        this(c.size());
        addAll(c);
    }

    private void grow() {
        Object[] a = new Object[elements.length * 2];
        for (int i = 0; i < size; i++) a[i] = elements[(head + i) % elements.length];
        elements = a;
        head = 0;
    }

    private int idx(int i) { return (head + i) % elements.length; }

    public void addFirst(E e) {
        if (e == null) throw new NullPointerException();
        if (size == elements.length) grow();
        head = (head - 1 + elements.length) % elements.length;
        elements[head] = e;
        size++;
    }

    public void addLast(E e) {
        if (e == null) throw new NullPointerException();
        if (size == elements.length) grow();
        elements[idx(size)] = e;
        size++;
    }

    public boolean offerFirst(E e) {
        addFirst(e);
        return true;
    }

    public boolean offerLast(E e) {
        addLast(e);
        return true;
    }

    public E removeFirst() {
        E e = pollFirst();
        if (e == null) throw new NoSuchElementException();
        return e;
    }

    public E removeLast() {
        E e = pollLast();
        if (e == null) throw new NoSuchElementException();
        return e;
    }

    @SuppressWarnings("unchecked")
    public E pollFirst() {
        if (size == 0) return null;
        E e = (E) elements[head];
        elements[head] = null;
        head = (head + 1) % elements.length;
        size--;
        return e;
    }

    @SuppressWarnings("unchecked")
    public E pollLast() {
        if (size == 0) return null;
        int i = idx(size - 1);
        E e = (E) elements[i];
        elements[i] = null;
        size--;
        return e;
    }

    public E getFirst() {
        if (size == 0) throw new NoSuchElementException();
        return peekFirst();
    }

    public E getLast() {
        if (size == 0) throw new NoSuchElementException();
        return peekLast();
    }

    @SuppressWarnings("unchecked")
    public E peekFirst() { return size == 0 ? null : (E) elements[head]; }

    @SuppressWarnings("unchecked")
    public E peekLast() { return size == 0 ? null : (E) elements[idx(size - 1)]; }

    @SuppressWarnings("unchecked")
    E at(int i) { return (E) elements[idx(i)]; }

    void removeAt(int i) {
        for (int k = i; k < size - 1; k++) elements[idx(k)] = elements[idx(k + 1)];
        elements[idx(size - 1)] = null;
        size--;
    }

    public boolean removeFirstOccurrence(Object o) {
        for (int i = 0; i < size; i++) {
            if (at(i).equals(o)) {
                removeAt(i);
                return true;
            }
        }
        return false;
    }

    public boolean removeLastOccurrence(Object o) {
        for (int i = size - 1; i >= 0; i--) {
            if (at(i).equals(o)) {
                removeAt(i);
                return true;
            }
        }
        return false;
    }

    public boolean add(E e) {
        addLast(e);
        return true;
    }

    public boolean offer(E e) { return offerLast(e); }

    public E remove() { return removeFirst(); }

    public E poll() { return pollFirst(); }

    public E element() { return getFirst(); }

    public E peek() { return peekFirst(); }

    public void push(E e) { addFirst(e); }

    public E pop() { return removeFirst(); }

    public int size() { return size; }

    public boolean isEmpty() { return size == 0; }

    public Iterator<E> iterator() {
        return new Iterator<E>() {
            int i, last = -1;

            public boolean hasNext() { return i < size; }

            public E next() {
                if (i >= size) throw new NoSuchElementException();
                last = i;
                return at(i++);
            }

            public void remove() {
                if (last < 0) throw new IllegalStateException();
                removeAt(last);
                i = last;
                last = -1;
            }
        };
    }

    public Iterator<E> descendingIterator() {
        return new Iterator<E>() {
            int i = size - 1, last = -1;

            public boolean hasNext() { return i >= 0; }

            public E next() {
                if (i < 0) throw new NoSuchElementException();
                last = i;
                return at(i--);
            }

            public void remove() {
                if (last < 0) throw new IllegalStateException();
                removeAt(last);
                last = -1;
            }
        };
    }

    public boolean contains(Object o) {
        if (o == null) return false;
        for (int i = 0; i < size; i++) if (o.equals(at(i))) return true;
        return false;
    }

    public boolean remove(Object o) { return removeFirstOccurrence(o); }

    public void clear() {
        for (int i = 0; i < elements.length; i++) elements[i] = null;
        head = size = 0;
    }

    public Object[] toArray() {
        Object[] a = new Object[size];
        for (int i = 0; i < size; i++) a[i] = at(i);
        return a;
    }

    public ArrayDeque<E> clone() { return new ArrayDeque<E>(this); }
}
