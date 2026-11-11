package java.util.concurrent;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

/**
 * MyOS Java has one thread: a task runs to the end inside submit()/execute(),
 * so results are ready (and in order) when submit returns.
 */
public abstract class AbstractExecutorService implements ExecutorService {
    private boolean shutdown;

    public AbstractExecutorService() {}

    private void check() {
        if (shutdown) throw new RejectedExecutionException("executor has been shut down");
    }

    public void execute(Runnable command) {
        check();
        command.run();
    }

    public <T> Future<T> submit(Callable<T> task) {
        check();
        return DoneFuture.run(task);
    }

    public <T> Future<T> submit(final Runnable task, final T result) {
        return submit(new Callable<T>() {
            public T call() {
                task.run();
                return result;
            }
        });
    }

    public Future<?> submit(Runnable task) { return submit(task, null); }

    public <T> List<Future<T>> invokeAll(Collection<? extends Callable<T>> tasks) {
        List<Future<T>> r = new ArrayList<Future<T>>();
        for (Callable<T> t : tasks) r.add(submit(t));
        return r;
    }

    public <T> T invokeAny(Collection<? extends Callable<T>> tasks) throws InterruptedException, ExecutionException {
        ExecutionException last = null;
        for (Callable<T> t : tasks) {
            try {
                return submit(t).get();
            } catch (ExecutionException e) {
                last = e;
            }
        }
        throw last != null ? last : new ExecutionException(new IllegalArgumentException("no tasks"));
    }

    public void shutdown() { shutdown = true; }

    public List<Runnable> shutdownNow() {
        shutdown = true;
        return new ArrayList<Runnable>();
    }

    public boolean isShutdown() { return shutdown; }

    public boolean isTerminated() { return shutdown; }

    public boolean awaitTermination(long timeout, TimeUnit unit) { return true; }
}
