package java.util.concurrent;

public interface BlockingQueue<E> extends java.util.Queue<E> {
    void put(E e) throws InterruptedException;

    E take() throws InterruptedException;
}
