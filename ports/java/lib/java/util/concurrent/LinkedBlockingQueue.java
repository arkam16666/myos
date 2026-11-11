package java.util.concurrent;

public class LinkedBlockingQueue<E> extends java.util.LinkedList<E> implements BlockingQueue<E> {
    public LinkedBlockingQueue() {}

    public LinkedBlockingQueue(int capacity) {}

    public void put(E e) { add(e); }

    public E take() {
        if (isEmpty()) throw new IllegalStateException("take() on an empty queue would wait forever: MyOS Java runs one thread");
        return removeFirst();
    }
}
