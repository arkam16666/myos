package java.util.concurrent.atomic;

import java.util.function.LongBinaryOperator;
import java.util.function.LongUnaryOperator;

public class AtomicLong extends Number implements java.io.Serializable {
    private volatile long value;

    public AtomicLong(long initialValue) { value = initialValue; }

    public AtomicLong() {}

    public final long get() { return value; }

    public final void set(long newValue) { value = newValue; }

    public final long getAndSet(long newValue) {
        long old = value;
        value = newValue;
        return old;
    }

    public final boolean compareAndSet(long expect, long update) {
        if (value != expect) return false;
        value = update;
        return true;
    }

    public final long getAndIncrement() { return value++; }

    public final long getAndDecrement() { return value--; }

    public final long getAndAdd(long delta) {
        long old = value;
        value += delta;
        return old;
    }

    public final long incrementAndGet() { return ++value; }

    public final long decrementAndGet() { return --value; }

    public final long addAndGet(long delta) { return value += delta; }

    public final long updateAndGet(LongUnaryOperator f) { return value = f.applyAsLong(value); }

    public final long accumulateAndGet(long x, LongBinaryOperator f) { return value = f.applyAsLong(value, x); }

    public String toString() { return Long.toString(value); }

    public int intValue() { return (int) value; }

    public long longValue() { return value; }

    public float floatValue() { return value; }

    public double doubleValue() { return value; }
}
