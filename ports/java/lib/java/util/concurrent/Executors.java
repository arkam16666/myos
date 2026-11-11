package java.util.concurrent;

public class Executors {
    private Executors() {}

    static final class Direct extends AbstractExecutorService {}

    public static ExecutorService newFixedThreadPool(int nThreads) { return new Direct(); }

    public static ExecutorService newSingleThreadExecutor() { return new Direct(); }

    public static ExecutorService newCachedThreadPool() { return new Direct(); }

    public static ExecutorService newWorkStealingPool() { return new Direct(); }

    public static ExecutorService newVirtualThreadPerTaskExecutor() { return new Direct(); }

    public static ScheduledExecutorService newScheduledThreadPool(int corePoolSize) { return new Scheduled(); }

    public static ScheduledExecutorService newSingleThreadScheduledExecutor() { return new Scheduled(); }

    static final class Scheduled extends AbstractExecutorService implements ScheduledExecutorService {
        public ScheduledFuture<?> schedule(final Runnable command, long delay, TimeUnit unit) {
            try {
                unit.sleep(delay);
            } catch (InterruptedException e) {
                /* ignore */
            }
            command.run();
            return new ScheduledFuture<Object>() {
                public boolean cancel(boolean b) { return false; }

                public boolean isCancelled() { return false; }

                public boolean isDone() { return true; }

                public Object get() { return null; }

                public Object get(long t, TimeUnit u) { return null; }
            };
        }
    }

    public static <T> Callable<T> callable(final Runnable task, final T result) {
        return new Callable<T>() {
            public T call() {
                task.run();
                return result;
            }
        };
    }
}
