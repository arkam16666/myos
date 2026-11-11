package java.util;

public class LinkedList<E> extends AbstractSequentialList<E> implements List<E>, Deque<E>, Cloneable, java.io.Serializable {
    private static class Node<E> {
        E item;
        Node<E> next, prev;

        Node(Node<E> prev, E element, Node<E> next) {
            this.item = element;
            this.next = next;
            this.prev = prev;
        }
    }

    transient int size = 0;
    transient Node<E> first, last;

    public LinkedList() {}

    public LinkedList(Collection<? extends E> c) { addAll(c); }

    private void linkLast(E e) {
        Node<E> l = last, n = new Node<E>(l, e, null);
        last = n;
        if (l == null) first = n;
        else l.next = n;
        size++;
        modCount++;
    }

    private void linkBefore(E e, Node<E> succ) {
        Node<E> pred = succ.prev, n = new Node<E>(pred, e, succ);
        succ.prev = n;
        if (pred == null) first = n;
        else pred.next = n;
        size++;
        modCount++;
    }

    private E unlink(Node<E> x) {
        E element = x.item;
        Node<E> next = x.next, prev = x.prev;
        if (prev == null) first = next;
        else prev.next = next;
        if (next == null) last = prev;
        else next.prev = prev;
        x.item = null;
        size--;
        modCount++;
        return element;
    }

    private Node<E> node(int index) {
        if (index < (size >> 1)) {
            Node<E> x = first;
            for (int i = 0; i < index; i++) x = x.next;
            return x;
        }
        Node<E> x = last;
        for (int i = size - 1; i > index; i--) x = x.prev;
        return x;
    }

    private void check(int index) {
        if (index < 0 || index >= size) throw new IndexOutOfBoundsException("Index: " + index + ", Size: " + size);
    }

    public E getFirst() {
        if (first == null) throw new NoSuchElementException();
        return first.item;
    }

    public E getLast() {
        if (last == null) throw new NoSuchElementException();
        return last.item;
    }

    public E removeFirst() {
        if (first == null) throw new NoSuchElementException();
        return unlink(first);
    }

    public E removeLast() {
        if (last == null) throw new NoSuchElementException();
        return unlink(last);
    }

    public void addFirst(E e) {
        if (first == null) linkLast(e);
        else linkBefore(e, first);
    }

    public void addLast(E e) { linkLast(e); }

    public boolean contains(Object o) { return indexOf(o) >= 0; }

    public int size() { return size; }

    public boolean add(E e) {
        linkLast(e);
        return true;
    }

    public boolean remove(Object o) {
        for (Node<E> x = first; x != null; x = x.next) {
            if (Objects.equals(o, x.item)) {
                unlink(x);
                return true;
            }
        }
        return false;
    }

    public boolean addAll(Collection<? extends E> c) {
        Object[] a = c.toArray();
        for (Object o : a) {
            @SuppressWarnings("unchecked")
            E e = (E) o;
            linkLast(e);
        }
        return a.length != 0;
    }

    public boolean addAll(int index, Collection<? extends E> c) {
        rangeCheckForAdd(index);
        Object[] a = c.toArray();
        if (index == size) return addAll(c);
        Node<E> succ = node(index);
        for (Object o : a) {
            @SuppressWarnings("unchecked")
            E e = (E) o;
            linkBefore(e, succ);
        }
        return a.length != 0;
    }

    public void clear() {
        first = last = null;
        size = 0;
        modCount++;
    }

    public E get(int index) {
        check(index);
        return node(index).item;
    }

    public E set(int index, E element) {
        check(index);
        Node<E> x = node(index);
        E old = x.item;
        x.item = element;
        return old;
    }

    public void add(int index, E element) {
        rangeCheckForAdd(index);
        if (index == size) linkLast(element);
        else linkBefore(element, node(index));
    }

    public E remove(int index) {
        check(index);
        return unlink(node(index));
    }

    public int indexOf(Object o) {
        int index = 0;
        for (Node<E> x = first; x != null; x = x.next, index++) if (Objects.equals(o, x.item)) return index;
        return -1;
    }

    public int lastIndexOf(Object o) {
        int index = size - 1;
        for (Node<E> x = last; x != null; x = x.prev, index--) if (Objects.equals(o, x.item)) return index;
        return -1;
    }

    public E peek() { return first == null ? null : first.item; }

    public E element() { return getFirst(); }

    public E poll() { return first == null ? null : unlink(first); }

    public E remove() { return removeFirst(); }

    public boolean offer(E e) { return add(e); }

    public boolean offerFirst(E e) {
        addFirst(e);
        return true;
    }

    public boolean offerLast(E e) {
        addLast(e);
        return true;
    }

    public E peekFirst() { return first == null ? null : first.item; }

    public E peekLast() { return last == null ? null : last.item; }

    public E pollFirst() { return first == null ? null : unlink(first); }

    public E pollLast() { return last == null ? null : unlink(last); }

    public void push(E e) { addFirst(e); }

    public E pop() { return removeFirst(); }

    public boolean removeFirstOccurrence(Object o) { return remove(o); }

    public boolean removeLastOccurrence(Object o) {
        for (Node<E> x = last; x != null; x = x.prev) {
            if (Objects.equals(o, x.item)) {
                unlink(x);
                return true;
            }
        }
        return false;
    }

    public ListIterator<E> listIterator(int index) {
        rangeCheckForAdd(index);
        return new ListItr(index);
    }

    public ListIterator<E> listIterator() { return new ListItr(0); }

    public Iterator<E> iterator() { return new ListItr(0); }

    private class ListItr implements ListIterator<E> {
        private Node<E> lastReturned, next;
        private int nextIndex, expectedModCount = modCount;

        ListItr(int index) {
            next = (index == size) ? null : node(index);
            nextIndex = index;
        }

        public boolean hasNext() { return nextIndex < size; }

        public E next() {
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            if (!hasNext()) throw new NoSuchElementException();
            lastReturned = next;
            next = next.next;
            nextIndex++;
            return lastReturned.item;
        }

        public boolean hasPrevious() { return nextIndex > 0; }

        public E previous() {
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            if (!hasPrevious()) throw new NoSuchElementException();
            lastReturned = next = (next == null) ? last : next.prev;
            nextIndex--;
            return lastReturned.item;
        }

        public int nextIndex() { return nextIndex; }

        public int previousIndex() { return nextIndex - 1; }

        public void remove() {
            if (modCount != expectedModCount) throw new ConcurrentModificationException();
            if (lastReturned == null) throw new IllegalStateException();
            Node<E> lastNext = lastReturned.next;
            unlink(lastReturned);
            if (next == lastReturned) next = lastNext;
            else nextIndex--;
            lastReturned = null;
            expectedModCount++;
        }

        public void set(E e) {
            if (lastReturned == null) throw new IllegalStateException();
            lastReturned.item = e;
        }

        public void add(E e) {
            lastReturned = null;
            if (next == null) linkLast(e);
            else linkBefore(e, next);
            nextIndex++;
            expectedModCount++;
        }
    }

    public Iterator<E> descendingIterator() {
        return new Iterator<E>() {
            private final ListItr itr = new ListItr(size());

            public boolean hasNext() { return itr.hasPrevious(); }

            public E next() { return itr.previous(); }

            public void remove() { itr.remove(); }
        };
    }

    public LinkedList<E> reversed() {
        LinkedList<E> r = new LinkedList<E>();
        for (Node<E> x = last; x != null; x = x.prev) r.add(x.item);
        return r;
    }

    public Object clone() { return new LinkedList<E>(this); }

    public Object[] toArray() {
        Object[] result = new Object[size];
        int i = 0;
        for (Node<E> x = first; x != null; x = x.next) result[i++] = x.item;
        return result;
    }
}
