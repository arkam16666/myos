package java.util;

public interface SequencedCollection<E> extends Collection<E> {
    default SequencedCollection<E> reversed() { throw new UnsupportedOperationException(); }

    default void addFirst(E e) { throw new UnsupportedOperationException(); }

    default void addLast(E e) { throw new UnsupportedOperationException(); }

    default E getFirst() { return iterator().next(); }

    default E getLast() {
        Iterator<E> it = iterator();
        E e = it.next();
        while (it.hasNext()) e = it.next();
        return e;
    }

    default E removeFirst() {
        Iterator<E> it = iterator();
        E e = it.next();
        it.remove();
        return e;
    }

    default E removeLast() { throw new UnsupportedOperationException(); }
}
