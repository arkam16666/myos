package java.util.concurrent.atomic;

import java.util.function.IntBinaryOperator;
import java.util.function.IntUnaryOperator;

public class AtomicInteger extends Number implements java.io.Serializable {
    private volatile int value;

    public AtomicInteger(int initialValue) { value = initialValue; }

    public AtomicInteger() {}

    public final int get() { return value; }

    public final void set(int newValue) { value = newValue; }

    public final void lazySet(int newValue) { value = newValue; }

    public final int getAndSet(int newValue) {
        int old = value;
        value = newValue;
        return old;
    }

    public final boolean compareAndSet(int expect, int update) {
        if (value != expect) return false;
        value = update;
        return true;
    }

    public final int getAndIncrement() { return value++; }

    public final int getAndDecrement() { return value--; }

    public final int getAndAdd(int delta) {
        int old = value;
        value += delta;
        return old;
    }

    public final int incrementAndGet() { return ++value; }

    public final int decrementAndGet() { return --value; }

    public final int addAndGet(int delta) { return value += delta; }

    public final int getAndUpdate(IntUnaryOperator f) {
        int old = value;
        value = f.applyAsInt(old);
        return old;
    }

    public final int updateAndGet(IntUnaryOperator f) { return value = f.applyAsInt(value); }

    public final int getAndAccumulate(int x, IntBinaryOperator f) {
        int old = value;
        value = f.applyAsInt(old, x);
        return old;
    }

    public final int accumulateAndGet(int x, IntBinaryOperator f) { return value = f.applyAsInt(value, x); }

    public String toString() { return Integer.toString(value); }

    public int intValue() { return value; }

    public long longValue() { return value; }

    public float floatValue() { return value; }

    public double doubleValue() { return value; }
}
