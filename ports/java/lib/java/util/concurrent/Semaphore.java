package java.util.concurrent;

public class Semaphore {
    private int permits;

    public Semaphore(int permits) { this.permits = permits; }

    public Semaphore(int permits, boolean fair) { this.permits = permits; }

    public void acquire() throws InterruptedException { permits--; }

    public boolean tryAcquire() {
        if (permits <= 0) return false;
        permits--;
        return true;
    }

    public void release() { permits++; }

    public int availablePermits() { return permits; }
}
