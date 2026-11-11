package java.util.concurrent.atomic;

import java.util.function.BinaryOperator;
import java.util.function.UnaryOperator;

public class AtomicReference<V> implements java.io.Serializable {
    private volatile V value;

    public AtomicReference(V initialValue) { value = initialValue; }

    public AtomicReference() {}

    public final V get() { return value; }

    public final void set(V newValue) { value = newValue; }

    public final V getAndSet(V newValue) {
        V old = value;
        value = newValue;
        return old;
    }

    public final boolean compareAndSet(V expect, V update) {
        if (value != expect) return false;
        value = update;
        return true;
    }

    public final V updateAndGet(UnaryOperator<V> f) { return value = f.apply(value); }

    public final V getAndUpdate(UnaryOperator<V> f) {
        V old = value;
        value = f.apply(old);
        return old;
    }

    public final V accumulateAndGet(V x, BinaryOperator<V> f) { return value = f.apply(value, x); }

    public String toString() { return String.valueOf(value); }
}
