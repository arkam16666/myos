package java.util.concurrent;

/* a task that has already run (MyOS Java runs submitted tasks right away) */
final class DoneFuture<V> implements Future<V> {
    private final V value;
    private final Throwable error;

    DoneFuture(V value, Throwable error) {
        this.value = value;
        this.error = error;
    }

    static <V> DoneFuture<V> run(Callable<V> task) {
        try {
            return new DoneFuture<V>(task.call(), null);
        } catch (Throwable t) {
            return new DoneFuture<V>(null, t);
        }
    }

    public boolean cancel(boolean mayInterruptIfRunning) { return false; }

    public boolean isCancelled() { return false; }

    public boolean isDone() { return true; }

    public V get() throws ExecutionException {
        if (error != null) throw new ExecutionException(error);
        return value;
    }

    public V get(long timeout, TimeUnit unit) throws ExecutionException { return get(); }
}
