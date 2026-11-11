package java.util.concurrent;

public class CopyOnWriteArrayList<E> extends java.util.ArrayList<E> {
    public CopyOnWriteArrayList() { super(); }

    public CopyOnWriteArrayList(java.util.Collection<? extends E> c) { super(c); }
}
