package java.util.concurrent;

import java.util.function.*;

/** Runs every stage right away (one thread), so join() never waits. */
public class CompletableFuture<T> implements Future<T>, CompletionStage<T> {
    private T value;
    private Throwable error;
    private boolean done;

    public CompletableFuture() {}

    private static <U> CompletableFuture<U> of(U v, Throwable t) {
        CompletableFuture<U> f = new CompletableFuture<U>();
        f.value = v;
        f.error = t;
        f.done = true;
        return f;
    }

    public static <U> CompletableFuture<U> supplyAsync(Supplier<U> supplier) {
        try {
            return of(supplier.get(), null);
        } catch (Throwable t) {
            return of(null, new CompletionException(t));
        }
    }

    public static <U> CompletableFuture<U> supplyAsync(Supplier<U> supplier, Executor executor) { return supplyAsync(supplier); }

    public static CompletableFuture<Void> runAsync(Runnable runnable) {
        try {
            runnable.run();
            return of(null, null);
        } catch (Throwable t) {
            return of(null, new CompletionException(t));
        }
    }

    public static CompletableFuture<Void> runAsync(Runnable runnable, Executor executor) { return runAsync(runnable); }

    public static <U> CompletableFuture<U> completedFuture(U value) { return of(value, null); }

    public static CompletableFuture<Void> allOf(CompletableFuture<?>... cfs) {
        for (CompletableFuture<?> f : cfs) if (f.error != null) return of(null, f.error);
        return of(null, null);
    }

    public static CompletableFuture<Object> anyOf(CompletableFuture<?>... cfs) {
        if (cfs.length == 0) return new CompletableFuture<Object>();
        return of((Object) cfs[0].value, cfs[0].error);
    }

    public boolean complete(T v) {
        if (done) return false;
        value = v;
        done = true;
        return true;
    }

    public boolean completeExceptionally(Throwable ex) {
        if (done) return false;
        error = ex;
        done = true;
        return true;
    }

    public <U> CompletableFuture<U> thenApply(Function<? super T, ? extends U> fn) {
        if (error != null) return of(null, error);
        try {
            return of(fn.apply(value), null);
        } catch (Throwable t) {
            return of(null, new CompletionException(t));
        }
    }

    public <U> CompletableFuture<U> thenApplyAsync(Function<? super T, ? extends U> fn) { return thenApply(fn); }

    public CompletableFuture<Void> thenAccept(Consumer<? super T> action) {
        if (error != null) return of(null, error);
        try {
            action.accept(value);
            return of(null, null);
        } catch (Throwable t) {
            return of(null, new CompletionException(t));
        }
    }

    public CompletableFuture<Void> thenRun(Runnable action) {
        if (error != null) return of(null, error);
        action.run();
        return of(null, null);
    }

    public CompletableFuture<T> toCompletableFuture() { return this; }

    public <U> CompletableFuture<U> thenCompose(Function<? super T, ? extends CompletionStage<U>> fn) {
        if (error != null) return of(null, error);
        return fn.apply(value).toCompletableFuture();
    }

    public <U, V> CompletableFuture<V> thenCombine(CompletionStage<? extends U> other, BiFunction<? super T, ? super U, ? extends V> fn) {
        CompletableFuture<? extends U> o = other.toCompletableFuture();
        if (error != null) return of(null, error);
        if (o.error != null) return of(null, o.error);
        return of(fn.apply(value, o.value), null);
    }

    public <U> CompletableFuture<Void> thenAcceptBoth(CompletionStage<? extends U> other, BiConsumer<? super T, ? super U> action) {
        CompletableFuture<? extends U> o = other.toCompletableFuture();
        if (error != null) return of(null, error);
        if (o.error != null) return of(null, o.error);
        action.accept(value, o.value);
        return of(null, null);
    }

    public CompletableFuture<T> exceptionally(Function<Throwable, ? extends T> fn) {
        if (error == null) return this;
        return of(fn.apply(error), null);
    }

    public <U> CompletableFuture<U> handle(BiFunction<? super T, Throwable, ? extends U> fn) { return of(fn.apply(value, error), null); }

    public CompletableFuture<T> whenComplete(BiConsumer<? super T, ? super Throwable> action) {
        action.accept(value, error);
        return this;
    }

    public T join() {
        if (error != null) throw error instanceof CompletionException ? (CompletionException) error : new CompletionException(error);
        return value;
    }

    public T get() throws ExecutionException {
        if (error != null) throw new ExecutionException(error instanceof CompletionException && error.getCause() != null ? error.getCause() : error);
        return value;
    }

    public T get(long timeout, TimeUnit unit) throws ExecutionException { return get(); }

    public T getNow(T valueIfAbsent) { return done ? join() : valueIfAbsent; }

    public boolean cancel(boolean mayInterruptIfRunning) { return false; }

    public boolean isCancelled() { return false; }

    public boolean isDone() { return done; }

    public boolean isCompletedExceptionally() { return error != null; }
}
