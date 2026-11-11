package java.util.concurrent;

public class CountDownLatch {
    private long count;

    public CountDownLatch(int count) {
        if (count < 0) throw new IllegalArgumentException("count < 0");
        this.count = count;
    }

    public void await() throws InterruptedException {
        if (count > 0) throw new IllegalStateException("CountDownLatch.await() would wait forever: MyOS Java runs one thread");
    }

    public boolean await(long timeout, TimeUnit unit) throws InterruptedException { return count == 0; }

    public void countDown() { if (count > 0) count--; }

    public long getCount() { return count; }
}
