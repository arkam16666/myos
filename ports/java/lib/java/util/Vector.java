package java.util;

public class Vector<E> extends ArrayList<E> {
    public Vector() { super(); }

    public Vector(int initialCapacity) { super(initialCapacity); }

    public Vector(int initialCapacity, int capacityIncrement) { super(initialCapacity); }

    public Vector(Collection<? extends E> c) { super(c); }

    public E elementAt(int index) { return get(index); }

    public E firstElement() {
        if (isEmpty()) throw new NoSuchElementException();
        return get(0);
    }

    public E lastElement() {
        if (isEmpty()) throw new NoSuchElementException();
        return get(size() - 1);
    }

    public void setElementAt(E obj, int index) { set(index, obj); }

    public void removeElementAt(int index) { remove(index); }

    public void insertElementAt(E obj, int index) { add(index, obj); }

    public void addElement(E obj) { add(obj); }

    public boolean removeElement(Object obj) { return remove(obj); }

    public void removeAllElements() { clear(); }

    public int capacity() { return elementData.length; }

    public void setSize(int newSize) {
        while (size() < newSize) add(null);
        while (size() > newSize) remove(size() - 1);
    }

    public Enumeration<E> elements() {
        final Iterator<E> it = iterator();
        return new Enumeration<E>() {
            public boolean hasMoreElements() { return it.hasNext(); }

            public E nextElement() { return it.next(); }
        };
    }
}
