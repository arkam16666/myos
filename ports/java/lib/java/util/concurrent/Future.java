package java.util.concurrent;

public interface Future<V> {
    boolean cancel(boolean mayInterruptIfRunning);

    boolean isCancelled();

    boolean isDone();

    V get() throws InterruptedException, ExecutionException;

    V get(long timeout, TimeUnit unit) throws InterruptedException, ExecutionException, TimeoutException;

    default V resultNow() {
        try {
            return get();
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }
}
