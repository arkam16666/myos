package java.util.concurrent.locks;

public class ReentrantReadWriteLock implements ReadWriteLock, java.io.Serializable {
    private final ReentrantLock r = new ReentrantLock(), w = new ReentrantLock();

    public ReentrantReadWriteLock() {}

    public ReentrantReadWriteLock(boolean fair) {}

    public Lock readLock() { return r; }

    public Lock writeLock() { return w; }
}
