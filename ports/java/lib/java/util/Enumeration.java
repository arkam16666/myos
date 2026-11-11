package java.util;

public interface Enumeration<E> {
    boolean hasMoreElements();

    E nextElement();

    default Iterator<E> asIterator() {
        final Enumeration<E> self = this;
        return new Iterator<E>() {
            public boolean hasNext() { return self.hasMoreElements(); }

            public E next() { return self.nextElement(); }
        };
    }
}
