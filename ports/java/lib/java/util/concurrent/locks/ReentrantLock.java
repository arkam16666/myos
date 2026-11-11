package java.util.concurrent.locks;

public class ReentrantLock implements Lock, java.io.Serializable {
    private int holds;

    public ReentrantLock() {}

    public ReentrantLock(boolean fair) {}

    public void lock() { holds++; }

    public void lockInterruptibly() { holds++; }

    public boolean tryLock() {
        holds++;
        return true;
    }

    public boolean tryLock(long time, java.util.concurrent.TimeUnit unit) { return tryLock(); }

    public void unlock() {
        if (holds == 0) throw new IllegalMonitorStateException();
        holds--;
    }

    public boolean isLocked() { return holds > 0; }

    public int getHoldCount() { return holds; }

    public boolean isHeldByCurrentThread() { return holds > 0; }
}
