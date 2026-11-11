package java.util;

public class Stack<E> extends Vector<E> {
    public Stack() {}

    public E push(E item) {
        add(item);
        return item;
    }

    public synchronized E pop() {
        E obj = peek();
        remove(size() - 1);
        return obj;
    }

    public synchronized E peek() {
        int len = size();
        if (len == 0) throw new EmptyStackException();
        return get(len - 1);
    }

    public boolean empty() { return size() == 0; }

    public synchronized int search(Object o) {
        int i = lastIndexOf(o);
        return i >= 0 ? size() - i : -1;
    }
}
